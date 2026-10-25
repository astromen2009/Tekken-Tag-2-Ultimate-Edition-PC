"""TTT2 Ultimate Launcher: the Windows players' window for Tekken Tag Tournament 2 Ultimate Edition.

Folders: the game's install folder and Tekken 7's, chosen with Browse and remembered (settings.ini beside the
launcher). Nothing the launcher or the game writes goes elsewhere: the launcher keeps its settings, downloads,
temporary files and the game installer's kit (kit\\, where the game program is built) in its own folder, and the
game keeps its saves, settings, shader cache and the added fighters in <game>\\data (runtime/src/host/portable.h;
made with what earlier versions kept in %APPDATA%\\ttt2, copied there).

The game: Play; Update all, which takes the newest Windows installer from the releases of the repository it was
built from (launcher.ini, written by .github/workflows/windows-launcher.yml), installs it quietly into kit\\,
rebuilds the game program from the install folder itself (ttt2-setup.exe GAME GAME: no .wua needed) when another
kit built it, and updates the launcher too; Install the game, which runs the installer's dialogs for the .wua.

Tekken 7's fighters: the character importer (tools/characters/import_character.py) runs as a second process of this
same program (`--run-import ARGS...`), so that its signal handling has a main thread of its own and Cancel can end
it; this window reads its --progress lines. Tekken 7 starts minimised for a minute or two while its movesets are
saved. `--self-test` imports everything the frozen program needs (the CI checks it).

Frozen with PyInstaller (windows-launcher.yml), with t7helper.exe and launcher.ini beside it.

Written for this project (MIT); no third-party code.
"""
import json
import os
import queue
import re
import shutil
import subprocess
import sys
import threading

FROZEN = getattr(sys, 'frozen', False)
HERE = os.path.dirname(os.path.abspath(sys.executable if FROZEN else __file__))
if not FROZEN:      # (run from the repository: the importer's package beside it)
    sys.path.insert(0, os.path.join(HERE, '..', '..', 'tools', 'characters'))

SPANISH = False
try:
    import ctypes
    SPANISH = (ctypes.windll.kernel32.GetUserDefaultUILanguage() & 0x3FF) == 0x0A
except (AttributeError, OSError):
    pass


def tr(english, spanish):
    return spanish if SPANISH else english


# The importer's likeliest messages, in Spanish (by how they start).
SPANISH_ERRORS = (
    ('Steam is not running', 'Steam no está abierto y no se pudo abrir: instala Steam (store.steampowered.com) e inicia '
                             'sesión con la cuenta con la que compraste Tekken 7, y vuelve a intentarlo.'),
    ('Tekken 7 is running', 'Tekken 7 está abierto: ciérralo primero.'),
    ('Tekken 7 did not start', 'Tekken 7 no arrancó. Comprueba que Steam está abierto con la sesión iniciada en la cuenta '
                               'con la que lo compraste, y que el juego arranca desde Steam.'),
    ('none of the fighters asked for is owned', 'Tu cuenta de Steam no tiene ninguno de los luchadores elegidos (son DLC).'),
    ('the helper', 'El ayudante no pudo hablar con Tekken 7 (quizá un antivirus lo bloqueó, o es otra versión del juego).'),
)


def spanish(message):
    if SPANISH:
        for start, text in SPANISH_ERRORS:
            if message.startswith(start):
                return text + '\n\n(' + message + ')'
    return message


# ---------------------------------------------------------------- where things are

# Everything beside the launcher (the folder its installer was told).
SETTINGS = os.path.join(HERE, 'settings.ini')
KIT = os.path.join(HERE, 'kit')                 # the game installer (windows/installer.iss), installed by the launcher
DOWNLOADS = os.path.join(HERE, 'downloads')
TMP = os.path.join(HERE, 'tmp')
GAME_PROGRAM = 'Tekken Tag Tournament 2.exe'
DEFAULT_REPOSITORY = 'astromen2009/Tekken-Tag-2-Ultimate-Edition-PC'
# what earlier versions kept on the system drive (copied into the game's data folder; their build leftovers deleted)
OLD_DATA = os.path.join(os.environ.get('APPDATA', ''), 'ttt2')
OLD_KIT = os.path.join(os.environ.get('LOCALAPPDATA', ''), 'Programs', 'TTT2 Installer')
OLD_WORK = [os.path.join(os.environ.get('LOCALAPPDATA', ''), 'ttt2', name) for name in ('setup-work', 'setup-check')]


def read_ini(path):
    """{key: value} of `key = value` lines (kit.ini, setup.ini, launcher.ini, settings.ini); {} without the file."""
    out = {}
    try:
        with open(path, encoding='utf-8', errors='replace') as f:
            for text in f:
                key, eq, value = text.partition('=')
                if eq and not text.lstrip().startswith('#'):
                    out[key.strip()] = value.strip()
    except OSError:
        pass
    return out


def write_settings(**values):
    settings = read_ini(SETTINGS)
    settings.update(values)
    try:
        with open(SETTINGS, 'w', encoding='utf-8') as f:
            f.write('# TTT2 Ultimate Launcher\'s folders\n')
            for key, value in settings.items():
                f.write('%s = %s\n' % (key, value))
    except OSError:
        pass


def launcher_info():
    for folder in (HERE, os.path.join(HERE, '_internal')):
        info = read_ini(os.path.join(folder, 'launcher.ini'))
        if info:
            return info
    return {}


def helper_exe():
    for folder in (HERE, os.path.join(HERE, '_internal')):
        path = os.path.join(folder, 't7helper.exe')
        if os.path.isfile(path):
            return path
    return None


def game_folder(folder):
    """The install folder in or at `folder` (the one with Tekken Tag Tournament 2.exe), or None."""
    if not folder:
        return None
    for f in (folder, os.path.join(folder, 'Tekken Tag Tournament 2')):
        if os.path.isfile(os.path.join(f, GAME_PROGRAM)):
            return os.path.normpath(f)
    return None


def initial_game():
    """The game folder chosen before, else the one an earlier version recorded, else ''."""
    chosen = read_ini(SETTINGS).get('game', '')
    if chosen:
        return chosen
    try:
        with open(os.path.join(OLD_DATA, 'installed'), encoding='utf-8') as f:
            return game_folder(f.readline().strip()) or ''
    except OSError:
        return ''


def initial_tekken7():
    """The Tekken 7 folder chosen before, else Steam's, else ''."""
    from ttt2char.tekken7 import autodump
    chosen = autodump.tekken7_folder(read_ini(SETTINGS).get('tekken7', ''))
    if chosen:
        return chosen
    try:
        return autodump.tekken7_folder(autodump.tekken7_library()[2]) or ''
    except Exception:           # (no Steam library with it: the player chooses)
        return ''


def ensure_data(game):
    """<game>\\data, the game's own data folder (as `ttt2 --install` makes it), made with what earlier versions kept in
    %APPDATA%\\ttt2 (copied)."""
    data = os.path.join(game, 'data')
    if not os.path.isdir(data):
        os.makedirs(data, exist_ok=True)
        for name in ('save', 'characters', 'display.ini', 'keyboard.ini'):
            old = os.path.join(OLD_DATA, name)
            try:
                if os.path.isdir(old):
                    shutil.copytree(old, os.path.join(data, name), dirs_exist_ok=True)
                elif os.path.isfile(old):
                    shutil.copy2(old, os.path.join(data, name))
            except OSError:
                pass
    return data


def child_env(**extra):
    """The environment of what the launcher starts: temporary files in the launcher's own folder."""
    os.makedirs(TMP, exist_ok=True)
    env = dict(os.environ, TEMP=TMP, TMP=TMP, PYTHONIOENCODING='utf-8', PYTHONUNBUFFERED='1')
    env.update(extra)
    return env


def kit_version():
    return read_ini(os.path.join(KIT, 'kit.ini')).get('version', '')


def built_version(game):
    return read_ini(os.path.join(game, 'data', 'setup.ini')).get('built', '') if game else ''


def version_key(version):
    return tuple(int(n) for n in re.findall(r'\d+', version or ''))


# ---------------------------------------------------------------- updates

def latest_releases(repository):
    """{'windows': (version, installer url), 'launcher': (...)}: the newest published release of each, by its tag
    (windows-v<version>, launcher-v<version>) with its installer attached."""
    import urllib.request
    request = urllib.request.Request('https://api.github.com/repos/%s/releases?per_page=50' % repository,
                                     headers={'Accept': 'application/vnd.github+json', 'User-Agent': 'ttt2-launcher'})
    with urllib.request.urlopen(request, timeout=30) as response:
        releases = json.load(response)
    out = {}
    for kind, prefix, asset_prefix in (('windows', 'windows-v', 'TTT2-Setup-Windows-'),
                                       ('launcher', 'launcher-v', 'TTT2-Launcher-Windows-')):
        best = None
        for release in releases:
            tag = release.get('tag_name', '')
            if release.get('draft') or release.get('prerelease') or not tag.startswith(prefix):
                continue
            assets = [a for a in release.get('assets', [])
                      if a['name'].startswith(asset_prefix) and a['name'].endswith('.exe')]
            version = tag[len(prefix):]
            if assets and (best is None or version_key(version) > version_key(best[0])):
                best = (version, assets[0]['browser_download_url'])
        if best:
            out[kind] = best
    return out


def download(url, progress):
    import urllib.request
    os.makedirs(DOWNLOADS, exist_ok=True)
    path = os.path.join(DOWNLOADS, os.path.basename(url))
    request = urllib.request.Request(url, headers={'User-Agent': 'ttt2-launcher'})
    with urllib.request.urlopen(request, timeout=60) as response, open(path + '.part', 'wb') as out:
        total = int(response.headers.get('Content-Length') or 0)
        done = 0
        while True:
            block = response.read(1 << 20)
            if not block:
                break
            out.write(block)
            done += len(block)
            progress(done, total)
    os.replace(path + '.part', path)
    return path


def quiet_install(installer, folder):
    """Runs an Inno Setup installer without its pages, into `folder`; -> whether it succeeded."""
    return subprocess.run([installer, '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', '/DIR=' + folder],
                          env=child_env()).returncode == 0


def remove_old_build_files():
    """What earlier versions left on the system drive that the launcher's kit replaces: their kit and their build's
    work folders (not the saves: those were copied, and stay)."""
    for folder in [OLD_KIT] + OLD_WORK:
        if folder and os.path.isdir(folder) and os.path.normcase(os.path.abspath(folder)) != os.path.normcase(KIT):
            shutil.rmtree(folder, ignore_errors=True)


# ---------------------------------------------------------------- the import process

def attach_pipes():
    """A windowed PyInstaller program has no sys.stdout; the pipe its starter gave it is there."""
    if sys.stdout is None or sys.stderr is None:
        import msvcrt
        for number, name in ((-11, 'stdout'), (-12, 'stderr')):
            handle = ctypes.windll.kernel32.GetStdHandle(number)
            fd = msvcrt.open_osfhandle(handle, os.O_WRONLY | os.O_TEXT)
            setattr(sys, name, open(fd, 'w', encoding='utf-8', errors='replace', buffering=1))


def run_import(args):
    """--run-import: import_character's main() with `args`."""
    attach_pipes()
    import import_character
    sys.argv = ['import_character'] + args
    import_character.main()


def self_test():
    """--self-test: everything the frozen program loads at some point is in it."""
    attach_pipes()
    import import_character  # noqa: F401
    import tkinter  # noqa: F401
    import numpy  # noqa: F401
    from PIL import Image  # noqa: F401
    from ttt2char import importer, pack, shared  # noqa: F401
    from ttt2char.characters import CHARACTERS, definition
    from ttt2char.tekken7 import autodump, memdump  # noqa: F401
    for name in CHARACTERS:
        definition(name)
    print('self-test: %d fighters, helper %s, info %s' % (len(CHARACTERS), helper_exe(), launcher_info()))


# ---------------------------------------------------------------- the window

def window():
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
    from ttt2char.characters import CHARACTERS, definition

    root = tk.Tk()
    root.title('TTT2 Ultimate Launcher - Tekken Tag Tournament 2 Ultimate Edition')
    root.minsize(760, 600)
    info = launcher_info()
    repository = info.get('repository') or DEFAULT_REPOSITORY
    events = queue.Queue()              # (what, value) from the background threads, handled in the window's
    title = 'TTT2 Ultimate Launcher'

    # ------------------------------------------------ the folders
    folders = ttk.LabelFrame(root, text=tr('Folders', 'Carpetas'), padding=10)
    folders.pack(fill='x', padx=12, pady=(12, 0))
    folders.columnconfigure(1, weight=1)
    game = tk.StringVar(value=initial_game())
    tekken7 = tk.StringVar(value=initial_tekken7())

    def browse_game():
        chosen = filedialog.askdirectory(title=tr('The folder of Tekken Tag Tournament 2 (or where to install it)',
                                                  'La carpeta de Tekken Tag Tournament 2 (o dónde instalarlo)'),
                                         initialdir=game.get() or None, mustexist=True)
        if chosen:
            chosen = os.path.normpath(chosen)
            game.set(game_folder(chosen) or chosen)
            write_settings(game=game.get())
            refresh_game()

    def browse_tekken7():
        from ttt2char.tekken7 import autodump
        chosen = filedialog.askdirectory(title=tr('The folder of Tekken 7 (with TEKKEN 7.exe)',
                                                  'La carpeta de Tekken 7 (con TEKKEN 7.exe)'),
                                         initialdir=tekken7.get() or None, mustexist=True)
        if not chosen:
            return
        found = autodump.tekken7_folder(os.path.normpath(chosen))
        if not found:
            messagebox.showwarning(title, tr('There is no TEKKEN 7.exe in %s. Choose the folder you installed Tekken 7 in.',
                                             'No hay ningún TEKKEN 7.exe en %s. Elige la carpeta donde instalaste Tekken 7.')
                                   % os.path.normpath(chosen))
            return
        tekken7.set(found)
        write_settings(tekken7=found)

    for row, (label, variable, command) in enumerate((
            ('Tekken Tag Tournament 2:', game, browse_game), ('Tekken 7:', tekken7, browse_tekken7))):
        ttk.Label(folders, text=label).grid(row=row, column=0, sticky='w', pady=3)
        ttk.Entry(folders, textvariable=variable).grid(row=row, column=1, sticky='ew', padx=6, pady=3)
        ttk.Button(folders, text=tr('Browse...', 'Examinar...'), command=command).grid(row=row, column=2, pady=3)

    # ------------------------------------------------ the game
    top = ttk.LabelFrame(root, text=tr('The game', 'El juego'), padding=10)
    top.pack(fill='x', padx=12, pady=(10, 0))
    game_status = tk.StringVar()
    update_status = tk.StringVar(value=tr('Looking for updates...', 'Buscando actualizaciones...'))
    ttk.Label(top, textvariable=game_status, font=('Segoe UI', 10, 'bold'), wraplength=720,
              justify='left').pack(anchor='w')
    ttk.Label(top, textvariable=update_status, wraplength=720, justify='left').pack(anchor='w', pady=(2, 8))
    game_buttons = ttk.Frame(top)
    game_buttons.pack(anchor='w')
    latest = {}

    def refresh_game():
        folder = game_folder(game.get())
        built, kit = built_version(folder), kit_version()
        if folder:
            game_status.set(tr('Installed in %s', 'Instalado en %s') % folder +
                            (tr(' (built with installer %s)', ' (compilado con el instalador %s)') % built if built else ''))
            play_button.configure(state='normal')
        else:
            game_status.set(tr('Not installed in this folder: choose its folder above, or press Install the game.',
                               'No está instalado en esta carpeta: elige su carpeta arriba, o pulsa Instalar el juego.'))
            play_button.configure(state='disabled')
        newest = latest.get('windows', ('', ''))[0]
        notes = []
        if newest and version_key(newest) > version_key(kit):
            notes.append(tr('A new version of the game installer is out (%s).',
                            'Hay una versión nueva del instalador del juego (%s).') % newest)
        elif folder and kit and built != kit:
            notes.append(tr('The game program can be rebuilt with installer %s.',
                            'El programa del juego se puede recompilar con el instalador %s.') % kit)
        launcher_newest = latest.get('launcher', ('', ''))[0]
        if FROZEN and launcher_newest and version_key(launcher_newest) > version_key(info.get('version', '')):
            notes.append(tr('A new version of this launcher is out (%s).', 'Hay una versión nueva de este lanzador (%s).')
                         % launcher_newest)
        if latest:
            update_status.set(' '.join(notes) + tr(' Press Update all.', ' Pulsa Actualizar todo.') if notes
                              else tr('Everything is up to date.', 'Todo está actualizado.'))

    def check_updates():
        def work():
            try:
                events.put(('latest', latest_releases(repository)))
            except Exception as error:
                events.put(('status', tr('Could not look for updates (%s).', 'No se pudo buscar actualizaciones (%s).')
                            % error))
        threading.Thread(target=work, daemon=True).start()

    def play():
        folder = game_folder(game.get())
        if folder:
            subprocess.Popen([os.path.join(folder, GAME_PROGRAM)], cwd=folder)

    def ensure_kit(wanted):
        """The newest game installer's kit in kit\\ (downloaded when it is newer than the one there)."""
        newest = wanted.get('windows')
        have = os.path.isfile(os.path.join(KIT, 'ttt2-setup.exe'))
        if newest and (version_key(newest[0]) > version_key(kit_version()) or not have):
            events.put(('status', tr('Downloading the game installer %s...', 'Descargando el instalador del juego %s...')
                        % newest[0]))
            path = download(newest[1], lambda done, total: events.put(('status', tr(
                'Downloading the game installer %s: %d%%', 'Descargando el instalador del juego %s: %d%%')
                % (newest[0], 100 * done // max(total, 1)))))
            events.put(('status', tr('Installing the game installer %s...', 'Instalando el instalador del juego %s...')
                        % newest[0]))
            if not quiet_install(path, KIT):
                raise RuntimeError(tr('the game installer did not install', 'el instalador del juego no se instaló'))
            os.remove(path)
            remove_old_build_files()
        if not os.path.isfile(os.path.join(KIT, 'ttt2-setup.exe')):
            raise RuntimeError(tr('no game installer is published yet', 'todavía no hay ningún instalador del juego publicado'))

    def in_background(work):
        busy_game(True)

        def run():
            try:
                work()
                events.put(('done', None))
            except Exception as error:
                events.put(('failed', str(error)))
        threading.Thread(target=run, daemon=True).start()

    def update_all():
        folder = game_folder(game.get())

        def work():
            wanted = latest_releases(repository)
            events.put(('latest', wanted))
            ensure_kit(wanted)
            if folder:
                ensure_data(folder)
                if built_version(folder) != kit_version():
                    events.put(('status', tr('Rebuilding the game program (its window shows the progress)...',
                                             'Recompilando el programa del juego (su ventana muestra el progreso)...')))
                    # (from the install folder itself: the .wua is not needed again)
                    code = subprocess.run([os.path.join(KIT, 'ttt2-setup.exe'), folder, folder], env=child_env()).returncode
                    if code != 0:
                        raise RuntimeError(tr('rebuilding the game program failed',
                                              'falló la recompilación del programa del juego'))
            mine = wanted.get('launcher')
            if mine and FROZEN and version_key(mine[0]) > version_key(info.get('version', '')):
                events.put(('status', tr('Downloading the launcher %s...', 'Descargando el lanzador %s...') % mine[0]))
                events.put(('relaunch', download(mine[1], lambda done, total: None)))
        in_background(work)

    def install_game():
        target = game.get()

        def work():
            ensure_kit(latest_releases(repository))
            events.put(('status', tr('Installing the game: follow the installer\'s windows.',
                                     'Instalando el juego: sigue las ventanas del instalador.')))
            command = [os.path.join(KIT, 'ttt2-setup.exe')] + (['--folder', target] if target else [])
            subprocess.run(command, env=child_env())
            installed = game_folder(target) if target else None
            if installed:
                events.put(('game', installed))
        in_background(work)

    play_button = ttk.Button(game_buttons, text=tr('Play', 'Jugar'), command=play)
    update_button = ttk.Button(game_buttons, text=tr('Update all', 'Actualizar todo'), command=update_all)
    install_button = ttk.Button(game_buttons, text=tr('Install the game...', 'Instalar el juego...'), command=install_game)
    for b in (play_button, update_button, install_button):
        b.pack(side='left', padx=(0, 8))

    def busy_game(on):
        for b in (update_button, install_button):
            b.configure(state='disabled' if on else 'normal')

    def handle_events():
        try:
            while True:
                what, value = events.get_nowait()
                if what == 'latest':
                    latest.clear()
                    latest.update(value)
                    refresh_game()
                elif what == 'status':
                    update_status.set(value)
                elif what == 'game':
                    game.set(value)
                    write_settings(game=value)
                elif what == 'done':
                    busy_game(False)
                    refresh_game()
                elif what == 'failed':
                    busy_game(False)
                    refresh_game()
                    messagebox.showerror(title, tr('It failed: %s', 'Ha fallado: %s') % value)
                elif what == 'relaunch':
                    # (the installer replaces this program: it runs once this one has ended, into the same folder,
                    # then starts it again)
                    subprocess.Popen('cmd /c timeout /t 2 /nobreak >nul & "%s" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART '
                                     '/SP- "/DIR=%s" & start "" "%s"' % (value, HERE, sys.executable),
                                     creationflags=subprocess.CREATE_NO_WINDOW, env=child_env())
                    root.destroy()
                    return
        except queue.Empty:
            pass
        root.after(200, handle_events)

    # ------------------------------------------------ Tekken 7's fighters
    frame = ttk.LabelFrame(root, text=tr('Tekken 7 fighters', 'Luchadores de Tekken 7'), padding=10)
    frame.pack(fill='both', expand=True, padx=12, pady=10)
    frame.columnconfigure(0, weight=1)
    ttk.Label(frame, wraplength=720, justify='left', text=tr(
        'From your own Tekken 7 (its folder above). Steam must be installed and signed in to the account that bought '
        'it (only the DLC fighters it owns are added). Tekken 7 opens minimised for a minute or two and closes again: '
        'leave it alone. The packs are Namco\'s assets: keep them for yourself.',
        'A partir de tu propio Tekken 7 (su carpeta, arriba). Steam tiene que estar instalado y con la sesión iniciada '
        'en la cuenta con la que lo compraste (solo se añaden los luchadores de DLC que tenga). Tekken 7 se abre '
        'minimizado un minuto o dos y se cierra solo: no lo toques. Los paquetes son material de Namco: solo para '
        'ti.')).grid(row=0, column=0, sticky='w')

    box = ttk.Frame(frame)
    box.grid(row=1, column=0, sticky='ew', pady=(8, 0))
    chosen = {}
    for n, name in enumerate(CHARACTERS):
        chosen[name] = tk.BooleanVar(value=True)
        ttk.Checkbutton(box, text=definition(name).NAME, variable=chosen[name]).grid(row=n // 5, column=n % 5,
                                                                                      sticky='w', padx=(0, 10))

    def set_all(value):
        for v in chosen.values():
            v.set(value)

    buttons = ttk.Frame(frame)
    buttons.grid(row=2, column=0, sticky='ew', pady=(8, 0))
    ttk.Button(buttons, text=tr('All', 'Todos'), command=lambda: set_all(True)).pack(side='left')
    ttk.Button(buttons, text=tr('None', 'Ninguno'), command=lambda: set_all(False)).pack(side='left', padx=6)
    status = tk.StringVar(value='')
    ttk.Label(frame, textvariable=status, font=('Segoe UI', 10, 'bold')).grid(row=3, column=0, sticky='w', pady=(8, 0))
    log = tk.Text(frame, height=6, wrap='word', state='disabled')
    log.grid(row=4, column=0, sticky='nsew', pady=(4, 0))
    frame.rowconfigure(4, weight=1)
    state = {'process': None, 'run': None}
    lines = queue.Queue()

    def append(text):
        log.configure(state='normal')
        log.insert('end', text + '\n')
        log.see('end')
        log.configure(state='disabled')

    def busy(on):
        for b in (add_button, remove_button):
            b.configure(state='disabled' if on else 'normal')
        cancel_button.configure(state='normal' if on else 'disabled')

    def start(remove):
        from ttt2char.tekken7 import autodump
        names = [n for n, v in chosen.items() if v.get()]
        if not names:
            messagebox.showwarning(title, tr('Choose at least one fighter.', 'Elige al menos un luchador.'))
            return
        folder = game_folder(game.get())
        if not folder:
            messagebox.showwarning(title, tr(
                'Choose the folder Tekken Tag Tournament 2 is installed in (above), or install it first.',
                'Elige arriba la carpeta donde está instalado Tekken Tag Tournament 2, o instálalo primero.'))
            return
        t7 = autodump.tekken7_folder(tekken7.get())
        if not remove:
            if not t7:
                messagebox.showwarning(title, tr('Choose the folder of Tekken 7 above (the one with TEKKEN 7.exe).',
                                                 'Elige arriba la carpeta de Tekken 7 (la que tiene TEKKEN 7.exe).'))
                return
            write_settings(tekken7=t7)
            try:
                steam = bool(autodump._windows_steam())
            except Exception:
                steam = True            # (not Windows: the importer tells)
            if not steam:
                messagebox.showwarning(title, tr(
                    'Tekken 7 needs Steam: install it (store.steampowered.com) and sign in with the account that bought '
                    'Tekken 7. Nothing needs downloading again: the folder you chose is used as it is.',
                    'Tekken 7 necesita Steam: instálalo (store.steampowered.com) e inicia sesión con la cuenta con la que '
                    'compraste Tekken 7. No hace falta volver a descargar nada: se usa la carpeta que elegiste tal cual.'))
                return
        # (all of it in the game's data folder)
        data = ensure_data(folder)
        cache = os.path.join(data, 'tekken7', 'cache')
        state['run'] = os.path.join(cache, 'run')
        args = names + ['--game', folder, '--progress', '--out', os.path.join(data, 'characters'),
                        '--movesets', os.path.join(data, 'tekken7', 'movesets'),
                        '--files', os.path.join(data, 'tekken7', 'files')]
        args += ['--remove'] if remove else ['--tekken7', t7]
        command = ([sys.executable, '--run-import'] if FROZEN else [sys.executable, os.path.abspath(__file__),
                                                                    '--run-import']) + args
        env = child_env(TTT2_TEKKEN7=t7 or '', TTT2_CACHE=cache, TTT2_CHARACTERS=os.path.join(data, 'characters'))
        helper = helper_exe()
        if helper:
            env['TTT2_T7HELPER'] = helper
        log.configure(state='normal')
        log.delete('1.0', 'end')
        log.configure(state='disabled')
        status.set(tr('Working...', 'Trabajando...'))
        busy(True)
        state['process'] = subprocess.Popen(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                            stdin=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW,
                                            encoding='utf-8', errors='replace')

        def read(process):
            for text in process.stdout:
                lines.put(text.rstrip('\n'))
            lines.put(None)
        threading.Thread(target=read, args=(state['process'],), daemon=True).start()

    failed = []

    def poll():
        try:
            while True:
                text = lines.get_nowait()
                if text is None:
                    finish()
                    break
                if text.startswith('@step '):
                    words = text.split(' ')
                    if words[1] == 'fighter':
                        status.set(tr('Importing %s (%s of %s)', 'Importando %s (%s de %s)') % tuple(words[2:5]))
                    elif words[1] == 'shared':
                        status.set(tr('Making what the fighters share...', 'Creando lo que comparten los luchadores...'))
                    else:
                        status.set(' '.join(words[2:]))
                elif text.startswith('@error '):
                    failed.append(text[7:])
                elif text.startswith('@skipped '):
                    append(tr('Skipped: ', 'Omitido: ') + text[9:])
                elif not text.startswith('@'):
                    append(text)
        except queue.Empty:
            pass
        root.after(100, poll)

    def finish():
        process = state['process']
        code = process.wait() if process else 1
        state['process'] = None
        busy(False)
        if code == 0:
            status.set(tr('Done.', 'Hecho.'))
            messagebox.showinfo(title, tr('Done. Start Tekken Tag Tournament 2: the fighters are on the select screen.',
                                          'Hecho. Abre Tekken Tag Tournament 2: los luchadores están en la pantalla de '
                                          'selección.'))
        else:
            status.set(tr('It failed.', 'Ha fallado.'))
            messagebox.showerror(title, spanish(failed[-1]) if failed else tr(
                'The import failed: see the window\'s text.', 'La importación ha fallado: mira el texto de la ventana.'))
        failed.clear()

    def cancel():
        process = state['process']
        if process is None:
            return
        subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True,
                       creationflags=subprocess.CREATE_NO_WINDOW)
        # (what its session started: Tekken 7 and the helper)
        try:
            from ttt2char.tekken7 import autodump
            if state['run']:
                autodump.end_recorded(state['run'])
        except Exception:
            pass
        append(tr('Cancelled.', 'Cancelado.'))

    add_button = ttk.Button(buttons, text=tr('Add fighters', 'Añadir luchadores'), command=lambda: start(False))
    remove_button = ttk.Button(buttons, text=tr('Remove chosen', 'Quitar los elegidos'), command=lambda: start(True))
    cancel_button = ttk.Button(buttons, text=tr('Cancel', 'Cancelar'), command=cancel, state='disabled')
    for b in (add_button, cancel_button, remove_button):
        b.pack(side='right', padx=4)

    def closing():
        if state['process'] is not None:
            if not messagebox.askyesno(title, tr('An import is running. Stop it and close?',
                                                 'Hay una importación en marcha. ¿Pararla y cerrar?')):
                return
            cancel()
        root.destroy()
    root.protocol('WM_DELETE_WINDOW', closing)
    refresh_game()
    check_updates()
    root.after(100, poll)
    root.after(200, handle_events)
    root.mainloop()


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--run-import':
        run_import(sys.argv[2:])
    elif len(sys.argv) > 1 and sys.argv[1] == '--self-test':
        try:
            self_test()
        except BaseException:           # (printed, not PyInstaller's dialog: the CI waits on no window)
            import traceback
            traceback.print_exc()
            sys.stderr.flush()
            os._exit(1)
    else:
        window()
