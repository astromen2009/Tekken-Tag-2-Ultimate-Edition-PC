"""TTT2 Fighters: the Windows window over the character importer (tools/characters/import_character.py).

The player picks the installed Tekken Tag Tournament 2 and Tekken 7 (both found by themselves when they can be) and
the fighters; the importer then does the rest: Tekken 7 starts minimised for a minute or two while its movesets are
saved, and the packs go to %APPDATA%\\ttt2\\characters, where the game reads them at start-up.

The import runs as a second process of this same program (`--run-import ARGS...`: import_character's command line),
so that its signal handling has a main thread of its own and Cancel can end it; this window reads its --progress
lines. Frozen with PyInstaller (windows/fighters/build.ps1), with t7helper.exe beside it.

Written for this project (MIT); no third-party code.
"""
import os
import queue
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


# ---------------------------------------------------------------- the window

def window():
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
    from ttt2char.characters import CHARACTERS, definition

    root = tk.Tk()
    root.title(tr('TTT2 Fighters - Tekken 7 fighters for Tekken Tag Tournament 2',
                  'TTT2 Fighters - luchadores de Tekken 7 para Tekken Tag Tournament 2'))
    root.minsize(760, 560)
    frame = ttk.Frame(root, padding=12)
    frame.pack(fill='both', expand=True)
    frame.columnconfigure(1, weight=1)

    ttk.Label(frame, wraplength=720, justify='left', text=tr(
        'Adds Tekken 7\'s fighters that Tekken Tag Tournament 2 lacks (Negan, Akuma, Kazumi...) from your own copies '
        'of both games. Choose the folder Tekken 7 is in and press Add fighters. Steam must be installed and signed in '
        'to the account that bought Tekken 7 (it is started if it is closed); only the DLC fighters that account owns '
        'are added. Tekken 7 starts minimised for a minute or two: leave it alone, the importer closes it. The packs '
        'hold Namco\'s assets: keep them for yourself.',
        'Añade los luchadores de Tekken 7 que Tekken Tag Tournament 2 no tiene (Negan, Akuma, Kazumi...) a partir de '
        'tus propias copias de ambos juegos. Elige la carpeta donde está Tekken 7 y pulsa Añadir luchadores. Steam '
        'tiene que estar instalado y con la sesión iniciada en la cuenta con la que compraste Tekken 7 (se abre solo '
        'si está cerrado); solo se añaden los luchadores de DLC que esa cuenta tiene. Tekken 7 se abrirá minimizado un '
        'minuto o dos: no lo toques, el importador lo cierra. Los paquetes contienen material de Namco: son solo para '
        'ti.')).grid(row=0, column=0, columnspan=3, sticky='w')

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
            messagebox.showwarning('TTT2 Fighters', tr(
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

    status = tk.StringVar(value='')
    ttk.Label(frame, textvariable=status, font=('Segoe UI', 10, 'bold')).grid(row=4, column=0, columnspan=3,
                                                                             sticky='w', pady=(12, 0))
    log = tk.Text(frame, height=12, wrap='word', state='disabled')
    log.grid(row=5, column=0, columnspan=3, sticky='nsew', pady=(4, 0))
    frame.rowconfigure(5, weight=1)

    buttons = ttk.Frame(frame)
    buttons.grid(row=6, column=0, columnspan=3, sticky='e', pady=(10, 0))
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
            messagebox.showwarning('TTT2 Fighters', tr('Choose at least one fighter.', 'Elige al menos un luchador.'))
            return
        if not os.path.isdir(os.path.join(game.get(), 'game')):
            messagebox.showwarning('TTT2 Fighters', tr(
                'Choose the folder Tekken Tag Tournament 2 is installed in (the one with "game" in it).',
                'Elige la carpeta donde instalaste Tekken Tag Tournament 2 (la que tiene la carpeta "game").'))
            return
        if not remove:
            from ttt2char.tekken7 import autodump
            folder = autodump.tekken7_folder(tekken7.get())
            if not folder:
                messagebox.showwarning('TTT2 Fighters', tr('Choose the folder of Tekken 7 (with TEKKEN 7.exe).',
                                                           'Elige la carpeta de Tekken 7 (la que tiene TEKKEN 7.exe).'))
                return
            tekken7.set(folder)
            remember_tekken7(folder)
            if not steam_installed():
                messagebox.showwarning('TTT2 Fighters', tr(
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
            messagebox.showinfo('TTT2 Fighters', tr(
                'Done. Start Tekken Tag Tournament 2: the fighters are on the select screen.',
                'Hecho. Abre Tekken Tag Tournament 2: los luchadores están en la pantalla de selección.'))
        else:
            status.set(tr('It failed.', 'Ha fallado.'))
            messagebox.showerror('TTT2 Fighters', (spanish(failed[-1]) if failed else tr(
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
            if not messagebox.askyesno('TTT2 Fighters', tr('An import is running. Stop it and close?',
                                                           'Hay una importación en marcha. ¿Pararla y cerrar?')):
                return
            cancel()
        root.destroy()
    root.protocol('WM_DELETE_WINDOW', closing)
    root.after(100, poll)
    root.mainloop()


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--run-import':
        run_import(sys.argv[2:])
    else:
        window()
