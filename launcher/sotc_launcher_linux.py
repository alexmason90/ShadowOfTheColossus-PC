#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native Linux launcher; also usable without a display via --play or --run."""
import os
from pathlib import Path
import queue
import signal
import subprocess
import sys
import threading

PORT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PORT))
import sotc


def main():
    if sys.argv[1:] == ['--help']:
        print('SotC Linux: --vulkan-test checks the GPU without a dump; --play starts with saved settings; --run [args] runs sotc.py directly.\n'
              'Configuration: ' + str(sotc.config_file()) + '\nData: ' + str(sotc.data_dir()))
        return 0
    if sys.argv[1:] == ['--vulkan-test']:
        return subprocess.run([str(sotc.find_probe()), '--vulkan-only']).returncode
    if sys.argv[1:] and sys.argv[1] in ('--play', '--run'):
        sys.argv = [str(PORT / 'sotc.py'), *sys.argv[2:]]
        return sotc.main()
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
    sotc.load_ini()
    root = tk.Tk()
    root.title('Shadow of the Colossus · Linux')
    root.geometry('760x620')
    root.minsize(600, 540)
    panel = ttk.Frame(root, padding=20)
    panel.pack(fill='both', expand=True)
    ttk.Label(panel, text='Shadow of the Colossus', font=('', 22)).pack(anchor='w')
    ttk.Label(panel, text='Native Linux · Vulkan · Bring your own game dump').pack(anchor='w', pady=(0, 18))
    fields = {}
    for name, label in [('SOTC_GAME_DIR', 'Game dump (folder containing eboot.bin)'),
                        ('SOTC_SYS_MODULES', 'Your console sound modules (optional)')]:
        ttk.Label(panel, text=label).pack(anchor='w')
        row = ttk.Frame(panel)
        row.pack(fill='x', pady=(4, 12))
        value = tk.StringVar(value=os.environ.get(name, ''))
        fields[name] = value
        ttk.Entry(row, textvariable=value).pack(side='left', fill='x', expand=True)
        def browse(var=value):
            folder = filedialog.askdirectory(parent=root)
            if folder:
                var.set(folder)
        ttk.Button(row, text='Browse…', command=browse).pack(side='left', padx=(8, 0))
    options = ttk.Frame(panel)
    options.pack(fill='x', pady=(0, 6))
    ttk.Label(options, text='Game frame rate').pack(side='left')
    current_fps = '30 FPS' if os.environ.get('SOTC_FPS_CAP', '30') == '30' else (
        '60 FPS' if os.environ.get('SOTC_FPS_LIMIT') == '60' else 'Unlimited')
    fps = tk.StringVar(value=current_fps)
    ttk.Combobox(options, textvariable=fps, values=('30 FPS', '60 FPS', 'Unlimited'),
                 state='readonly', width=14).pack(side='left', padx=10)
    optimized = tk.BooleanVar(value=os.environ.get('SOTC_LINUX_PERF', '1') == '1')
    ttk.Checkbutton(panel, text='Optimize GPU readbacks and memory reuse', variable=optimized).pack(anchor='w')
    framegen = tk.BooleanVar(value=os.environ.get('SOTC_FRAMEGEN') in ('fsr', 'fsr3'))
    ttk.Checkbutton(panel, text='FSR 3 frame generation (experimental)', variable=framegen).pack(anchor='w')
    ttk.Label(panel, text='Adds intermediate frames; it does not increase game simulation speed.',
              wraplength=700).pack(anchor='w')
    ttk.Label(panel, text='Saves and logs: ' + str(sotc.data_dir()), wraplength=700).pack(anchor='w', pady=12)
    log = tk.Text(panel, height=9, wrap='word', state='disabled')
    buttons = ttk.Frame(panel)
    buttons.pack(side='bottom', fill='x', pady=(12, 0))
    log.pack(fill='both', expand=True)
    messages = queue.Queue()
    process = None
    closing = False

    def save():
        # Preserve advanced settings and control mappings edited by the user.
        config = sotc.config_file()
        lines = config.read_text().splitlines() if config.exists() else []
        values = {key: var.get().strip() for key, var in fields.items()}
        modes = [m for m in os.environ.get('SOTC_MODES', 'af16').split(',')
                 if m and m not in ('fps60', 'fps60game')]
        if fps.get() != '30 FPS':
            modes.append('fps60game')
        values.update(SOTC_MODES=','.join(modes),
                      SOTC_FPS_CAP='30' if fps.get() == '30 FPS' else 'off',
                      SOTC_FPS_LIMIT='60' if fps.get() == '60 FPS' else '0',
                      SOTC_FRAMEGEN='fsr' if framegen.get() else 'off',
                      SOTC_LINUX_PERF='1' if optimized.get() else '0')
        lines = [line for line in lines if line.split('=', 1)[0].strip() not in values]
        config.parent.mkdir(parents=True, exist_ok=True)
        config.write_text('\n'.join(lines + [f'{key}={value}' for key, value in values.items()]) + '\n')
        return values

    def play():
        nonlocal process
        game = Path(fields['SOTC_GAME_DIR'].get()).expanduser()
        if not (game / 'eboot.bin').is_file():
            messagebox.showerror('Game dump needed', 'Choose a decrypted dump containing eboot.bin.', parent=root)
            return
        fields['SOTC_GAME_DIR'].set(str(game.resolve()))
        try:
            values = save()
            env = dict(os.environ, **values, PYTHONUNBUFFERED='1')
            process = subprocess.Popen([sys.executable, str(PORT / 'sotc.py')], env=env,
                                       stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, start_new_session=True)
        except OSError as error:
            messagebox.showerror('Could not start', str(error), parent=root)
            return
        play_button.configure(state='disabled')
        stop_button.configure(state='normal')
        def read_output(child=process):
            # Keep the game log even when there is no terminal (desktop/Steam launch).
            data = sotc.data_dir()
            data.mkdir(parents=True, exist_ok=True)
            with sotc.open_session_log(data) as session:
                for line in child.stdout:
                    session.write(line)
                    messages.put(line.decode('utf-8', errors='replace'))
            code = child.wait()
            sotc.keep_crash_log(data, code)
            messages.put(code)
        threading.Thread(target=read_output, daemon=True).start()

    def stop():
        if process and process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            root.after(3000, kill)

    def kill():
        if process and process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass

    def poll():
        nonlocal process
        try:
            while True:
                item = messages.get_nowait()
                if isinstance(item, int):
                    process = None
                    play_button.configure(state='normal')
                    stop_button.configure(state='disabled')
                    if closing:
                        root.destroy()
                        return
                    item = f'\nGame exited with code {item}.\n'
                log.configure(state='normal')
                log.insert('end', item)
                if int(log.index('end-1c').split('.')[0]) > 1200:
                    log.delete('1.0', '200.0')
                log.see('end')
                log.configure(state='disabled')
        except queue.Empty:
            pass
        root.after(100, poll)

    def close():
        nonlocal closing
        if process and process.poll() is None:
            closing = True
            stop()
        else:
            root.destroy()

    play_button = ttk.Button(buttons, text='Play', command=play)
    play_button.pack(side='right')
    stop_button = ttk.Button(buttons, text='Stop', command=stop, state='disabled')
    stop_button.pack(side='right', padx=8)
    root.protocol('WM_DELETE_WINDOW', close)
    root.after(100, poll)
    root.mainloop()
    return 0


if __name__ == '__main__':
    sys.exit(main())
