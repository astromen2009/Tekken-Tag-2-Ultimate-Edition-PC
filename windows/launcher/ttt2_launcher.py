"""TTT2 Ultimate Launcher: the Windows players' window for Tekken Tag Tournament 2 Ultimate Edition.

The game: Play; Update all, which takes the newest Windows installer from the releases of the repository it was
built from (launcher.ini, written by .github/workflows/windows-launcher.yml), installs it quietly, rebuilds the game
program from the install folder itself (ttt2-setup.exe INSTALL INSTALL: no .wua needed) when the kit that built it is
not the newest, and updates the launcher too; Install the game, which runs the installer's dialogs.

Tekken 7's fighters: the player picks Tekken 7's folder (any folder holding TEKKEN 7.exe, remembered) and the
fighters; the character importer (tools/characters/import_character.py) does the rest: Tekken 7 starts minimised for
a minute or two while its movesets are saved, and the packs go to %APPDATA%\\ttt2\\characters, where the game reads
them at start-up. The import runs as a second process of this same program (`--run-import ARGS...`:
import_character's command line), so that its signal handling has a main thread of its own and Cancel can end it;
this window reads its --progress lines. Frozen with PyInstaller, with t7helper.exe beside it.

Written for this project (MIT); no third-party code.
"""
import json
import os
import queue
import re
import subprocess
import sys
import threading

HERE = os.path.dirname(os.path.abspath(sys.executable if getattr(sys, 'frozen', False) else __file__))
FROZEN = getattr(sys, 'frozen', False)
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


# ---------------------------------------------------------------- the import process

def run_import(args):
    """--run-import: import_character's main() with `args`, its output on the pipe this window gave it."""
    if sys.stdout is None or sys.stderr is None:     # (a windowed PyInstaller program has none; the pipe is there)
        import msvcrt
        for number, name in ((-11, 'stdout'), (-12, 'stderr')):
            handle = ctypes.windll.kernel32.GetStdHandle(number)
            fd = msvcrt.open_osfhandle(handle, os.O_WRONLY | os.O_TEXT)
            setattr(sys, name, open(fd, 'w', encoding='utf-8', errors='replace', buffering=1))
    import import_character
    sys.argv = ['import_character'] + args
    import_character.main()


def helper_exe():
    for folder in (HERE, os.path.join(HERE, '_internal')):
        path = os.path.join(folder, 't7helper.exe')
        if os.path.isfile(path):
            return path
    return None


def installed_game():
    """The install folder `ttt2 --install` recorded, or ''."""
    try:
        with open(os.path.join(os.environ.get('APPDATA', ''), 'ttt2', 'installed'), encoding='utf-8') as f:
            folder = f.readline().strip()
        return folder if os.path.isdir(folder) else ''
    except OSError:
        return ''


REMEMBERED = os.path.join(os.environ.get('APPDATA', ''), 'ttt2', 'tekken7', 'folder.txt')


def found_tekken7():
    """The Tekken 7 folder chosen last time, else Steam's, else ''."""
    from ttt2char.tekken7 import autodump
    try:
        with open(REMEMBERED, encoding='utf-8') as f:
            folder = autodump.tekken7_folder(f.readline().strip())
        if folder:
            return folder
    except OSError:
        pass
    try:
        return autodump.tekken7_folder(autodump.tekken7_library()[2]) or ''
    except Exception:           # (no Steam library with it: the player chooses)
        return ''


def remember_tekken7(folder):
    try:
        os.makedirs(os.path.dirname(REMEMBERED), exist_ok=True)
        with open(REMEMBERED, 'w', encoding='utf-8') as f:
            f.write(folder + '\n')
    except OSError:
        pass


def steam_installed():
    from ttt2char.tekken7 import autodump
    try:
        return bool(autodump._windows_steam())
    except Exception:
        return True             # (not Windows: the importer tells)


# ---------------------------------------------------------------- the game and its updates

APPDATA = os.environ.get('APPDATA', '')
LOCALAPPDATA = os.environ.get('LOCALAPPDATA', '')
KIT = os.path.join(LOCALAPPDATA, 'Programs', 'TTT2 Installer')     # (windows/installer.iss's folder)
GAME_PROGRAM = 'Tekken Tag Tournament 2.exe'
DEFAULT_REPOSITORY = 'astromen2009/Tekken-Tag-2-Ultimate-Edition-PC'


def read_ini(path):
    """{key: value} of `key = value` lines (kit.ini, setup.ini, launcher.ini); {} without the file."""
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


def launcher_info():
    for folder in (HERE, os.path.join(HERE, '_internal')):
        info = read_ini(os.path.join(folder, 'launcher.ini'))
        if info:
            return info
    return {}


def kit_version():
    return read_ini(os.path.join(KIT, 'kit.ini')).get('version', '')


def built_version():
    return read_ini(os.path.join(APPDATA, 'ttt2', 'setup.ini')).get('built', '')


def version_key(version):
    return tuple(int(n) for n in re.findall(r'\d+', version or ''))


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
            assets = [a for a in release.get('assets', []) if a['name'].startswith(asset_prefix) and a['name'].endswith('.exe')]
            version = tag[len(prefix):]
            if assets and (best is None or version_key(version) > version_key(best[0])):
                best = (version, assets[0]['browser_download_url'])
        if best:
            out[kind] = best
    return out


def download(url, path, progress):
    import urllib.request
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


def quiet_install(installer):
    """Runs an Inno Setup installer without its pages; -> whether it succeeded."""
    return subprocess.run([installer, '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-']).returncode == 0


# ---------------------------------------------------------------- the window

def window():
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
    from ttt2char.characters import CHARACTERS, definition

    root = tk.Tk()
    root.title('TTT2 Ultimate Launcher - Tekken Tag Tournament 2 Ultimate Edition')
    root.minsize(760, 640)
    info = launcher_info()
    repository = info.get('repository') or DEFAULT_REPOSITORY
    events = queue.Queue()              # (what, value) from the update thread, handled in the window's

    # ------------------------------------------------ the game
    top = ttk.LabelFrame(root, text='Tekken Tag Tournament 2', padding=10)
    top.pack(fill='x', padx=12, pady=(12, 0))
    game_status = tk.StringVar()
    update_status = tk.StringVar(value=tr('Looking for updates...', 'Buscando actualizaciones...'))
    ttk.Label(top, textvariable=game_status, font=('Segoe UI', 10, 'bold'), wraplength=740, justify='left').pack(anchor='w')
    ttk.Label(top, textvariable=update_status, wraplength=740, justify='left').pack(anchor='w', pady=(2, 8))
    game_buttons = ttk.Frame(top)
    game_buttons.pack(anchor='w')
    latest = {}

    def refresh_game():
        folder = installed_game()
        built, kit = built_version(), kit_version()
        if folder and os.path.isfile(os.path.join(folder, GAME_PROGRAM)):
            game_status.set(tr('Installed in %s', 'Instalado en %s') % folder +
                            (tr(' (built with installer %s)', ' (compilado con el instalador %s)') % built if built else ''))
            play_button.configure(state='normal')
        else:
            game_status.set(tr('Not installed yet: press Install the game.', 'Aún no está instalado: pulsa Instalar el juego.'))
            play_button.configure(state='disabled')
        newest = latest.get('windows', ('', ''))[0]
        launcher_newest = latest.get('launcher', ('', ''))[0]
        notes = []
        if newest and version_key(newest) > version_key(kit):
            notes.append(tr('A new version of the game installer is out (%s).', 'Hay una versión nueva del instalador del juego (%s).') % newest)
        elif folder and kit and built != kit:
            notes.append(tr('The game program can be rebuilt with installer %s.', 'El programa del juego se puede recompilar con el instalador %s.') % kit)
        if launcher_newest and version_key(launcher_newest) > version_key(info.get('version', '')):
            notes.append(tr('A new version of this launcher is out (%s).', 'Hay una versión nueva de este lanzador (%s).') % launcher_newest)
        if latest:
            update_status.set(' '.join(notes) + (tr(' Press Update all.', ' Pulsa Actualizar todo.') if notes else '')
                              if notes else tr('Everything is up to date.', 'Todo está actualizado.'))
        if folder and not os.path.isdir(os.path.join(game.get(), 'game')):
            game.set(folder)

    def check_updates():
        def work():
            try:
                events.put(('latest', latest_releases(repository)))
            except Exception as error:
                events.put(('status', tr('Could not look for updates (%s).', 'No se pudo buscar actualizaciones (%s).') % error))
        threading.Thread(target=work, daemon=True).start()

    def play():
        folder = installed_game()
        if folder:
            os.startfile(os.path.join(folder, GAME_PROGRAM), cwd=folder)

    def ensure_kit(wanted):
        """The newest game installer's kit installed (downloaded when it is newer than the one there)."""
        newest = wanted.get('windows')
        if newest and (version_key(newest[0]) > version_key(kit_version()) or not os.path.isfile(os.path.join(KIT, 'ttt2-setup.exe'))):
            events.put(('status', tr('Downloading the game installer %s...', 'Descargando el instalador del juego %s...') % newest[0]))
            path = os.path.join(os.environ.get('TEMP', HERE), os.path.basename(newest[1]))
            download(newest[1], path, lambda done, total: events.put(('status', tr(
                'Downloading the game installer %s: %d%%', 'Descargando el instalador del juego %s: %d%%')
                % (newest[0], 100 * done // max(total, 1)))))
            events.put(('status', tr('Installing the game installer %s...', 'Instalando el instalador del juego %s...') % newest[0]))
            if not quiet_install(path):
                raise RuntimeError(tr('the game installer did not install', 'el instalador del juego no se instaló'))
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
        def work():
            wanted = latest_releases(repository)
            events.put(('latest', wanted))
            ensure_kit(wanted)
            folder = installed_game()
            if folder and built_version() != kit_version():
                events.put(('status', tr('Rebuilding the game program (its window shows the progress)...',
                                         'Recompilando el programa del juego (su ventana muestra el progreso)...')))
                # (from the install folder itself: the .wua is not needed again)
                code = subprocess.run([os.path.join(KIT, 'ttt2-setup.exe'), folder, folder]).returncode
                if code != 0:
                    raise RuntimeError(tr('rebuilding the game program failed', 'falló la recompilación del programa del juego'))
            mine = wanted.get('launcher')
            if mine and FROZEN and version_key(mine[0]) > version_key(info.get('version', '')):
                events.put(('status', tr('Downloading the launcher %s...', 'Descargando el lanzador %s...') % mine[0]))
                path = os.path.join(os.environ.get('TEMP', HERE), os.path.basename(mine[1]))
                download(mine[1], path, lambda done, total: None)
                events.put(('relaunch', path))
        in_background(work)

    def install_game():
        def work():
            ensure_kit(latest_releases(repository))
            events.put(('status', tr('Installing the game: follow the installer\'s windows.',
                                     'Instalando el juego: sigue las ventanas del instalador.')))
            subprocess.run([os.path.join(KIT, 'ttt2-setup.exe')])
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
                elif what == 'done':
                    busy_game(False)
                    refresh_game()
                elif what == 'failed':
                    busy_game(False)
                    refresh_game()
                    messagebox.showerror('TTT2 Ultimate Launcher', tr('Updating failed: %s', 'Falló la actualización: %s') % value)
                elif what == 'relaunch':
                    # (the installer replaces this program: it runs once this one has ended, then starts it again)
                    exe = sys.executable if FROZEN else ''
                    subprocess.Popen('cmd /c timeout /t 2 /nobreak >nul & "%s" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- & start "" "%s"'
                                     % (value, exe), creationflags=subprocess.CREATE_NO_WINDOW)
                    root.destroy()
                    return
        except queue.Empty:
            pass
        root.after(200, handle_events)

    # ------------------------------------------------ Tekken 7's fighters
    frame = ttk.LabelFrame(root, text=tr('Tekken 7 fighters', 'Luchadores de Tekken 7'), padding=10)
    frame.pack(fill='both', expand=True, padx=12, pady=12)
    frame.columnconfigure(1, weight=1)

    ttk.Label(frame, wraplength=720, justify='left', text=tr(
        'From your own Tekken 7: choose its folder and press Add fighters. Steam must be installed and signed in to '
        'the account that bought it (only the DLC fighters it owns are added). Tekken 7 opens minimised for a minute '
        'or two and closes again: leave it alone. The packs are Namco\'s assets: keep them for yourself.',
        'A partir de tu propio Tekken 7: elige su carpeta y pulsa Añadir luchadores. Steam tiene que estar instalado '
        'y con la sesión iniciada en la cuenta con la que lo compraste (solo se añaden los luchadores de DLC que '
        'tenga). Tekken 7 se abre minimizado un minuto o dos y se cierra solo: no lo toques. Los paquetes son '
        'material de Namco: solo para ti.')).grid(row=0, column=0, columnspan=3, sticky='w')

    game = tk.StringVar(value=installed_game())
    tekken7 = tk.StringVar(value=found_tekken7())

    def folder_row(row, label, variable, title, resolve=None):
        ttk.Label(frame, text=label).grid(row=row, column=0, sticky='w', pady=(10, 0))
        ttk.Entry(frame, textvariable=variable).grid(row=row, column=1, sticky='ew', padx=6, pady=(10, 0))

        def browse():
            chosen = filedialog.askdirectory(title=title, initialdir=variable.get() or None, mustexist=True)
            if chosen:
                chosen = os.path.normpath(chosen)
                variable.set(resolve(chosen) if resolve else chosen)
        ttk.Button(frame, text=tr('Browse...', 'Examinar...'), command=browse).grid(row=row, column=2, pady=(10, 0))

    folder_row(1, tr('Tekken Tag Tournament 2 (its install folder):', 'Tekken Tag Tournament 2 (carpeta instalada):'),
               game, tr('The folder Tekken Tag Tournament 2 is installed in', 'La carpeta donde está instalado Tekken Tag Tournament 2'))
    def resolve_tekken7(folder):
        from ttt2char.tekken7 import autodump
        found = autodump.tekken7_folder(folder)
        if not found:
            messagebox.showwarning('TTT2 Ultimate Launcher', tr(
                'There is no TEKKEN 7.exe in %s. Choose the folder you installed Tekken 7 in.',
                'No hay ningún TEKKEN 7.exe en %s. Elige la carpeta donde instalaste Tekken 7.') % folder)
            return folder
        return found

    folder_row(2, tr('Tekken 7 (its install folder):', 'Tekken 7 (su carpeta de instalación):'), tekken7,
               tr('The folder of Tekken 7 (with TEKKEN 7.exe)', 'La carpeta de Tekken 7 (con TEKKEN 7.exe)'),
               resolve_tekken7)

    box = ttk.LabelFrame(frame, text=tr('Fighters', 'Luchadores'), padding=8)
    box.grid(row=3, column=0, columnspan=3, sticky='ew', pady=(12, 0))
    chosen = {}
    for n, name in enumerate(CHARACTERS):
        chosen[name] = tk.BooleanVar(value=True)
        ttk.Checkbutton(box, text=definition(name).NAME, variable=chosen[name]).grid(row=n // 4, column=n % 4,
                                                                                      sticky='w', padx=6)

    def set_all(value):
        for v in chosen.values():
            v.set(value)
    links = ttk.Frame(box)
    links.grid(row=len(CHARACTERS) // 4 + 1, column=0, columnspan=4, sticky='w', pady=(6, 0))
    ttk.Button(links, text=tr('All', 'Todos'), command=lambda: set_all(True)).pack(side='left')
    ttk.Button(links, text=tr('None', 'Ninguno'), command=lambda: set_all(False)).pack(side='left', padx=6)

    buttons = ttk.Frame(frame)
    buttons.grid(row=4, column=0, columnspan=3, sticky='e', pady=(10, 0))
    status = tk.StringVar(value='')
    ttk.Label(frame, textvariable=status, font=('Segoe UI', 10, 'bold')).grid(row=5, column=0, columnspan=3,
                                                                             sticky='w', pady=(8, 0))
    log = tk.Text(frame, height=6, wrap='word', state='disabled')
    log.grid(row=6, column=0, columnspan=3, sticky='nsew', pady=(4, 0))
    frame.rowconfigure(6, weight=1)
    state = {'process': None}
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
        names = [n for n, v in chosen.items() if v.get()]
        if not names:
            messagebox.showwarning('TTT2 Ultimate Launcher', tr('Choose at least one fighter.', 'Elige al menos un luchador.'))
            return
        if not os.path.isdir(os.path.join(game.get(), 'game')):
            messagebox.showwarning('TTT2 Ultimate Launcher', tr(
                'Choose the folder Tekken Tag Tournament 2 is installed in (the one with "game" in it).',
                'Elige la carpeta donde instalaste Tekken Tag Tournament 2 (la que tiene la carpeta "game").'))
            return
        if not remove:
            from ttt2char.tekken7 import autodump
            folder = autodump.tekken7_folder(tekken7.get())
            if not folder:
                messagebox.showwarning('TTT2 Ultimate Launcher', tr('Choose the folder of Tekken 7 (with TEKKEN 7.exe).',
                                                           'Elige la carpeta de Tekken 7 (la que tiene TEKKEN 7.exe).'))
                return
            tekken7.set(folder)
            remember_tekken7(folder)
            if not steam_installed():
                messagebox.showwarning('TTT2 Ultimate Launcher', tr(
                    'Tekken 7 needs Steam: install it (store.steampowered.com) and sign in with the account that bought '
                    'Tekken 7. Nothing needs downloading again: the folder you chose is used as it is.',
                    'Tekken 7 necesita Steam: instálalo (store.steampowered.com) e inicia sesión con la cuenta con la que '
                    'compraste Tekken 7. No hace falta volver a descargar nada: se usa la carpeta que elegiste tal cual.'))
                return
        args = names + ['--game', game.get(), '--progress'] + (['--remove'] if remove else ['--tekken7', tekken7.get()])
        command = ([sys.executable, '--run-import'] if FROZEN else [sys.executable, os.path.abspath(__file__),
                                                                    '--run-import']) + args
        env = dict(os.environ, PYTHONIOENCODING='utf-8', PYTHONUNBUFFERED='1', TTT2_TEKKEN7=tekken7.get())
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

    added, failed = [], []

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
                elif text.startswith('@added '):
                    added.append(text[7:])
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
            messagebox.showinfo('TTT2 Ultimate Launcher', tr(
                'Done. Start Tekken Tag Tournament 2: the fighters are on the select screen.',
                'Hecho. Abre Tekken Tag Tournament 2: los luchadores están en la pantalla de selección.'))
        else:
            status.set(tr('It failed.', 'Ha fallado.'))
            messagebox.showerror('TTT2 Ultimate Launcher', (spanish(failed[-1]) if failed else tr(
                'The import failed: see the window\'s text.', 'La importación ha fallado: mira el texto de la ventana.')))
        added.clear()
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
            autodump.end_recorded(os.path.join(autodump.CACHE, 'run'))
        except Exception:
            pass
        append(tr('Cancelled.', 'Cancelado.'))

    add_button = ttk.Button(buttons, text=tr('Add fighters', 'Añadir luchadores'), command=lambda: start(False))
    remove_button = ttk.Button(buttons, text=tr('Remove chosen', 'Quitar los elegidos'), command=lambda: start(True))
    cancel_button = ttk.Button(buttons, text=tr('Cancel', 'Cancelar'), command=cancel, state='disabled')
    for b in (remove_button, cancel_button, add_button):
        b.pack(side='left', padx=4)

    def closing():
        if state['process'] is not None:
            if not messagebox.askyesno('TTT2 Ultimate Launcher', tr('An import is running. Stop it and close?',
                                                           'Hay una importación en marcha. ¿Pararla y cerrar?')):
                return
            cancel()
        root.destroy()
    root.protocol('WM_DELETE_WINDOW', closing)
    root.after(100, poll)
    refresh_game()
    check_updates()
    root.after(200, handle_events)
    root.mainloop()


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--run-import':
        run_import(sys.argv[2:])
    else:
        window()
