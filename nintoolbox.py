#!/usr/bin/env python3
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import subprocess
import threading
import os
import sys
import shutil
import sentry_sdk

try:
    from tkinterdnd2 import TkinterDnD, DND_FILES
except ImportError:
    TkinterDnD = None
    DND_FILES = None

sentry_sdk.init(
    dsn="https://04887b3ebaf8072bdf4bf9287d7bebe0@o107347.ingest.us.sentry.io/4512040246509568",
    # Add data like request headers and IP for users,
    # see https://docs.sentry.io/platforms/python/data-management/data-collected/ for more info
    send_default_pii=True,
)

SUPPORTED_FAMILIES = [
    ("Wii / GameCube Games", "*.wbfs *.iso *.ciso *.wdf *.wia *.gcz *.gcm *.wad"),
    ("Nintendo DS", "*.nds *.srl *.dsi *.narc *.sdat"),
    ("Nintendo 3DS", "*.3ds *.cia *.cxi *.ncch *.darc *.bcsar"),
    ("Wii U", "*.wud *.wux *.rpx *.rpl *.bfsar"),
    ("Nintendo Switch", "*.nsp *.xci *.nca"),
    ("SZS / Archives", "*.szs *.carc *.arc *.brres *.sarc *.pac *.pcs *.gfa *.rarc"),
    ("Textures & Images", "*.tpl *.bti *.tex0 *.bflim *.bclim *.ncgr *.nclr *.bntx"),
    ("3D Models & Collision", "*.mdl0 *.bcres *.bfres *.bch *.kcl *.csb *.ctb"),
    ("Layouts & Sequences", "*.brlyt *.brlan *.bflyt *.bflan *.ncer *.nanr *.rseq *.cseq *.sseq"),
]

GRID_COLUMNS = 3

_ALL_EXTS = " ".join(exts for _, exts in SUPPORTED_FAMILIES)

FILETYPES = (
    [("All supported", _ALL_EXTS)]
    + list(SUPPORTED_FAMILIES)
    + [("All files", "*.*")]
)


def bundle_dir():
    """The single folder every companion tool is staged into: the PyInstaller
    onefile extraction dir (sys._MEIPASS) when frozen, otherwise the folder
    this script lives in. All tools -- wszst and every --with-<tool> --
    are bundled into this one place (see nintoolbox.spec); nothing is
    duplicated into a second location, so this is the only directory
    find_wszst_binary()/find_companion_tool() ever need to search."""
    return getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))


def _exe_variants(path):
    """PyInstaller stages Windows binaries with their '.exe' suffix intact,
    but bare-name existence checks (os.path.isfile) don't auto-resolve that
    the way shell PATH lookup does. Try both spellings."""
    if os.name == "nt" and not path.lower().endswith(".exe"):
        return [path + ".exe", path]
    return [path]


def _find_executable(path):
    for candidate in _exe_variants(path):
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    return None


def find_wszst_binary():
    """Locate the wszst binary bundled in bundle_dir(), or fall back to PATH.
    Deliberately never matches this GUI's own executable: on Windows, an
    absent/misplaced wszst used to fall through to shutil.which("nintoolbox"),
    which matches the running .exe itself (Python's shutil.which prepends the
    current directory on Windows) and re-launches the GUI instead of running
    the tool.
    """
    base_dir = bundle_dir()
    for candidate in (
        os.path.join(base_dir, "wszst"),
        os.path.join(base_dir, "project", "bin", "wszst"),
        os.path.join(base_dir, "project", "wszst"),
        os.path.join(base_dir, "bin", "wszst"),
        "/usr/local/bin/wszst",
    ):
        found = _find_executable(candidate)
        if found:
            return found
    found = shutil.which("wszst")
    if found:
        return found
    return "wszst"


def find_companion_tool(name, wszst_path):
    """Look for NAME in bundle_dir(), the one folder every companion tool is
    staged into alongside wszst (see nintoolbox.spec), then fall back to
    PATH. wszst itself searches PATH by bare name (find_program() in
    lib-passthru.c), so passing an explicit absolute path via --with-<tool>=
    guarantees the bundled copy is always preferred.
    """
    found = _find_executable(os.path.join(bundle_dir(), name))
    if found:
        return found
    # wszst_path may point outside bundle_dir() (PATH/system install); check
    # alongside it too before giving up to PATH search.
    wszst_dir = os.path.dirname(os.path.abspath(wszst_path))
    found = _find_executable(os.path.join(wszst_dir, name))
    if found:
        return found
    return shutil.which(name)


class CollapsibleSection(ttk.Frame):
    """A disclosure triangle whose body is gridded/ungridded beneath it."""

    def __init__(self, parent, title, expanded=False):
        super().__init__(parent)
        self.columnconfigure(0, weight=1)
        self._title = title
        self._expanded = bool(expanded)
        self._button = ttk.Label(self, cursor="hand2", foreground="grey")
        self._button.grid(row=0, column=0, sticky="w")
        self._button.bind("<Button-1>", lambda _e: self.toggle())
        self.body = ttk.Frame(self)
        self.body.grid(row=1, column=0, sticky="ew", padx=(16, 0), pady=(3, 0))
        self._sync()

    def toggle(self):
        self._expanded = not self._expanded
        self._sync()

    def _sync(self):
        arrow = "\u25be" if self._expanded else "\u25b8"
        self._button.configure(text="%s  %s" % (arrow, self._title))
        if self._expanded:
            self.body.grid()
        else:
            self.body.grid_remove()


def _parse_dnd_path(data):
    """Extract the first path from a <<Drop>> event's data string. Tkdnd
    wraps any path containing a space in {curly braces} and space-separates
    multiple dropped paths; only the first dropped item is used here since
    every input field takes a single file/directory.
    """
    data = data.strip()
    if data.startswith("{"):
        end = data.find("}")
        return data[1:end] if end != -1 else data[1:]
    return data.split()[0] if data else ""


class NintoolboxGUI(TkinterDnD.Tk if TkinterDnD else tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("nintoolbox — Nintendo Toolbox")
        self.geometry("760x680")
        self.minsize(660, 520)
        self.configure(padx=15, pady=15)

        self.wszst_path = find_wszst_binary()
        # Companion tools: wit (disc images), mobipeg (video/audio transcoding),
        # sharpii (Wii WADs), nsz (Switch NSZ/XCZ), vgmtrans/wbrsar (audio archives),
        # ctrtool (3DS containers), ndstool (DS ROMs), hactool/hacbrewpack (Switch),
        # 7zz/7z (archives), makerom (3DS repacking), and wud2app (Wii U WUDs).
        self.wit_path = find_companion_tool("wit", self.wszst_path)
        self.mobipeg_path = find_companion_tool("mobipeg", self.wszst_path)
        self.sharpii_path = find_companion_tool("sharpii", self.wszst_path)
        self.nsz_path = find_companion_tool("nsz", self.wszst_path)
        self.vgmtrans_path = find_companion_tool("vgmtrans", self.wszst_path)
        self.ctrtool_path = find_companion_tool("ctrtool", self.wszst_path)
        self.ndstool_path = find_companion_tool("ndstool", self.wszst_path)
        self.hactool_path = find_companion_tool("hactool", self.wszst_path)
        self.hacbrewpack_path = find_companion_tool("hacbrewpack", self.wszst_path)
        self.sevenz_path = find_companion_tool("7zz", self.wszst_path) or find_companion_tool("7z", self.wszst_path)
        self.makerom_path = find_companion_tool("makerom", self.wszst_path)
        self.wud2app_path = find_companion_tool("wud2app", self.wszst_path)

        try:
            base_path = getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))
            if sys.platform != "darwin":
                icon_path = os.path.join(base_path, "logo.png")
                if os.path.exists(icon_path):
                    img = tk.PhotoImage(file=icon_path)
                    self.tk.call("wm", "iconphoto", self._w, img)
        except Exception:
            pass

        style = ttk.Style(self)
        if "aqua" in style.theme_names():
            style.theme_use("aqua")
        elif "clam" in style.theme_names():
            style.theme_use("clam")

        self.notebook = ttk.Notebook(self)
        self.notebook.pack(fill=tk.BOTH, expand=True)

        # --- UNPACK TAB (XX) ---
        self.unpack_frame = ttk.Frame(self.notebook, padding=10)
        self.notebook.add(self.unpack_frame, text="Unpack Game / Archive (XX)")
        self.setup_unpack_tab()

        # --- PACK TAB (CREATE) ---
        self.pack_frame = ttk.Frame(self.notebook, padding=10)
        self.notebook.add(self.pack_frame, text="Pack / Rebuild Game (CREATE)")
        self.setup_pack_tab()

        # --- INSTALL CLI TOOLS ---
        install_bar = ttk.Frame(self)
        install_bar.pack(fill=tk.X, pady=(8, 0))
        ttk.Button(
            install_bar,
            text="Install CLI Tools to PATH…",
            command=self.install_cli_tools,
        ).pack(side=tk.LEFT)

        # --- CONSOLE ---
        ttk.Label(self, text="Console Output:").pack(anchor="w", pady=(10, 0))
        console_frame = ttk.Frame(self)
        console_frame.pack(fill=tk.BOTH, expand=True)

        self.console = tk.Text(
            console_frame,
            height=10,
            state="disabled",
            bg="#1e1e1e",
            fg="#cccccc",
            font=("Menlo", 12),
        )
        self.console.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        scrollbar = ttk.Scrollbar(console_frame, command=self.console.yview)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        self.console.config(yscrollcommand=scrollbar.set)

    # -------------------------------------------------------------------------
    # UNPACK TAB
    # -------------------------------------------------------------------------
    def setup_unpack_tab(self):
        self.unpack_frame.columnconfigure(1, weight=1)

        # Row 0: Input File
        ttk.Label(self.unpack_frame, text="Input Game / Container:").grid(
            row=0, column=0, sticky="e", padx=5, pady=5
        )
        self.unpack_input_var = tk.StringVar()
        unpack_input_entry = ttk.Entry(self.unpack_frame, textvariable=self.unpack_input_var)
        unpack_input_entry.grid(row=0, column=1, sticky="ew", padx=5, pady=5)
        self.enable_drop(unpack_input_entry, self.unpack_input_var)
        ttk.Button(
            self.unpack_frame,
            text="Browse...",
            command=lambda: self.browse_file(self.unpack_input_var, FILETYPES),
        ).grid(row=0, column=2, padx=5, pady=5)

        # Row 1: Supported formats disclosure
        formats = CollapsibleSection(self.unpack_frame, "Supported containers & formats")
        formats.grid(row=1, column=0, columnspan=3, sticky="ew", padx=5, pady=(0, 8))
        for i, (name, exts) in enumerate(SUPPORTED_FAMILIES):
            cell = ttk.Frame(formats.body)
            cell.grid(
                row=i // GRID_COLUMNS,
                column=i % GRID_COLUMNS,
                sticky="nw",
                padx=(0, 20),
                pady=(0, 6),
            )
            ttk.Label(cell, text=name).pack(anchor="w")
            ttk.Label(
                cell, foreground="grey", text="  ".join(e.lstrip("*") for e in exts.split())
            ).pack(anchor="w")
        for col in range(GRID_COLUMNS):
            formats.body.columnconfigure(col, weight=1, uniform="fmt")

        # Row 2: Destination directory
        ttk.Label(self.unpack_frame, text="Output Directory:").grid(
            row=2, column=0, sticky="e", padx=5, pady=5
        )
        self.unpack_outdir_var = tk.StringVar(value="")
        ttk.Entry(self.unpack_frame, textvariable=self.unpack_outdir_var).grid(
            row=2, column=1, sticky="ew", padx=5, pady=5
        )
        ttk.Button(
            self.unpack_frame,
            text="Browse...",
            command=lambda: self.browse_dir(self.unpack_outdir_var),
        ).grid(row=2, column=2, padx=5, pady=5)

        # Row 3: Options
        self.unpack_auto_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            self.unpack_frame,
            text="Auto-extract nested sub-archives and decode textures/models/audio",
            variable=self.unpack_auto_var,
        ).grid(row=3, column=1, sticky="w", padx=5, pady=2)

        self.unpack_overwrite_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            self.unpack_frame,
            text="Overwrite existing files in destination directory",
            variable=self.unpack_overwrite_var,
        ).grid(row=4, column=1, sticky="w", padx=5, pady=2)

        # Row 5: Extra Arguments
        ttk.Label(self.unpack_frame, text="Extra wszst arguments:").grid(
            row=5, column=0, sticky="e", padx=5, pady=5
        )
        self.unpack_extra_var = tk.StringVar(value="")
        ttk.Entry(self.unpack_frame, textvariable=self.unpack_extra_var).grid(
            row=5, column=1, sticky="ew", padx=5, pady=5
        )
        ttk.Label(
            self.unpack_frame,
            foreground="grey",
            text="passed verbatim to wszst XX (e.g. -v, --long)",
        ).grid(row=6, column=1, columnspan=2, sticky="w", padx=5)

        # Run Button
        self.unpack_run_btn = ttk.Button(
            self.unpack_frame, text="▶ Unpack Game / Archive (XX)", command=self.run_unpack
        )
        self.unpack_run_btn.grid(row=7, column=1, pady=15)

        self.unpack_input_var.trace_add(
            "write", lambda *a: self.on_unpack_input_changed()
        )

    def on_unpack_input_changed(self):
        val = self.unpack_input_var.get().strip()
        if val.startswith("{") and val.endswith("}"):
            val = val[1:-1]
            self.unpack_input_var.set(val)
        if val and (os.path.isfile(val) or os.path.isdir(val)):
            # Default output directory: "<input>.d" or alongside input
            if val.endswith(".d"):
                dest = val
            else:
                dest = val + ".d"
            self.unpack_outdir_var.set(dest)

    # -------------------------------------------------------------------------
    # PACK TAB
    # -------------------------------------------------------------------------
    def setup_pack_tab(self):
        self.pack_frame.columnconfigure(1, weight=1)

        # Row 0: Extracted directory
        ttk.Label(self.pack_frame, text="Extracted Directory (.d):").grid(
            row=0, column=0, sticky="e", padx=5, pady=5
        )
        self.pack_input_var = tk.StringVar()
        pack_input_entry = ttk.Entry(self.pack_frame, textvariable=self.pack_input_var)
        pack_input_entry.grid(row=0, column=1, sticky="ew", padx=5, pady=5)
        self.enable_drop(pack_input_entry, self.pack_input_var)
        ttk.Button(
            self.pack_frame,
            text="Browse...",
            command=lambda: self.browse_dir(self.pack_input_var),
        ).grid(row=0, column=2, padx=5, pady=5)

        # Row 1: Target output file
        ttk.Label(self.pack_frame, text="Output Target File:").grid(
            row=1, column=0, sticky="e", padx=5, pady=5
        )
        self.pack_target_var = tk.StringVar()
        ttk.Entry(self.pack_frame, textvariable=self.pack_target_var).grid(
            row=1, column=1, sticky="ew", padx=5, pady=5
        )
        ttk.Button(
            self.pack_frame,
            text="Save As...",
            command=self.browse_save_pack,
        ).grid(row=1, column=2, padx=5, pady=5)

        # Row 2: Information note
        info_frame = ttk.LabelFrame(self.pack_frame, text="Selective Repack Info", padding=8)
        info_frame.grid(row=2, column=0, columnspan=3, sticky="ew", padx=5, pady=10)
        ttk.Label(
            info_frame,
            foreground="#555555",
            wraplength=600,
            text=(
                "• wszst CREATE scans bottom-up and uses content-hash caching to rebuild ONLY "
                "the specific containers/archives that contain user-modified files.\n"
                "• Untouched archives across the game are preserved 100% byte-for-byte.\n"
                "• If target output is blank, wszst automatically determines the target container format."
            ),
        ).pack(anchor="w")

        # Row 3: Options
        self.pack_overwrite_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            self.pack_frame,
            text="Allow overwriting destination container file",
            variable=self.pack_overwrite_var,
        ).grid(row=3, column=1, sticky="w", padx=5, pady=2)

        # Row 4: Extra Arguments
        ttk.Label(self.pack_frame, text="Extra wszst arguments:").grid(
            row=4, column=0, sticky="e", padx=5, pady=5
        )
        self.pack_extra_var = tk.StringVar(value="")
        ttk.Entry(self.pack_frame, textvariable=self.pack_extra_var).grid(
            row=4, column=1, sticky="ew", padx=5, pady=5
        )
        ttk.Label(
            self.pack_frame,
            foreground="grey",
            text="passed verbatim to wszst CREATE (e.g. -v, --test)",
        ).grid(row=5, column=1, columnspan=2, sticky="w", padx=5)

        # Run Button
        self.pack_run_btn = ttk.Button(
            self.pack_frame,
            text="▶ Pack / Rebuild Game (CREATE)",
            command=self.run_pack,
        )
        self.pack_run_btn.grid(row=6, column=1, pady=15)

        self.pack_input_var.trace_add(
            "write", lambda *a: self.on_pack_input_changed()
        )

    def on_pack_input_changed(self):
        val = self.pack_input_var.get().strip()
        if val.startswith("{") and val.endswith("}"):
            val = val[1:-1]
            self.pack_input_var.set(val)
        if val and os.path.isdir(val):
            # If folder ends with .d, default target is without .d
            if val.endswith(".d"):
                target = val[:-2]
                self.pack_target_var.set(target)

    # -------------------------------------------------------------------------
    # COMMON ACTIONS & RUNNERS
    # -------------------------------------------------------------------------
    def browse_file(self, var, filetypes=None):
        filename = filedialog.askopenfilename(
            filetypes=filetypes or [("All files", "*.*")]
        )
        if filename:
            var.set(filename)

    def browse_dir(self, var):
        directory = filedialog.askdirectory()
        if directory:
            var.set(directory)

    def enable_drop(self, widget, var):
        """Let WIDGET accept a dragged-in file/folder and write its path into
        VAR. No-op if tkinterdnd2 isn't available (e.g. it failed to bundle).
        """
        if DND_FILES is None:
            return
        widget.drop_target_register(DND_FILES)
        widget.dnd_bind("<<Drop>>", lambda e: var.set(_parse_dnd_path(e.data)))

    def browse_save_pack(self):
        current = self.pack_target_var.get().strip()
        initial = os.path.basename(current) if current else "game.wbfs"
        initialdir = os.path.dirname(current) if current else ""
        filename = filedialog.asksaveasfilename(
            title="Save repacked game/container as",
            initialfile=initial,
            initialdir=initialdir or None,
            filetypes=FILETYPES,
        )
        if filename:
            self.pack_target_var.set(filename)

    def install_cli_tools(self):
        """Copy every bundled CLI tool (and share/ data) onto the user's
        PATH, and add the destination to PATH if it isn't already there.
        Mirrors installer/install.sh and installer/install.ps1, but as a
        one-click in-app action instead of a script the user runs by hand.
        """
        tools_dir = bundle_dir()
        gui_names = {"nintoolbox", "nintoolbox.exe"}

        if sys.platform == "win32":
            dest = os.path.join(os.environ.get("LOCALAPPDATA", os.path.expanduser("~")), "nintoolbox", "bin")
            is_exe = lambda p: p.lower().endswith(".exe")
        else:
            dest = os.path.expanduser("~/.local/bin")
            is_exe = lambda p: os.access(p, os.X_OK)

        try:
            os.makedirs(dest, exist_ok=True)
            installed = []
            for name in sorted(os.listdir(tools_dir)):
                if name in gui_names or name == "share":
                    continue
                src = os.path.join(tools_dir, name)
                if not os.path.isfile(src) or not is_exe(src):
                    continue
                shutil.copy2(src, os.path.join(dest, name))
                installed.append(name)

            share_src = os.path.join(tools_dir, "share")
            if os.path.isdir(share_src):
                share_dest = os.path.join(os.path.dirname(dest), "share")
                os.makedirs(share_dest, exist_ok=True)
                for name in os.listdir(share_src):
                    shutil.copy2(os.path.join(share_src, name), os.path.join(share_dest, name))

            if not installed:
                messagebox.showwarning(
                    "Install CLI Tools",
                    "No bundled CLI tools were found next to this app -- nothing installed.",
                )
                return

            path_note = self._add_to_path(dest)
            self.append_console(
                "Installed to %s:\n  %s\n%s\n" % (dest, "\n  ".join(installed), path_note)
            )
            messagebox.showinfo(
                "Install CLI Tools",
                "Installed %d tool(s) to:\n%s\n\n%s" % (len(installed), dest, path_note),
            )
        except Exception as exc:
            messagebox.showerror("Install CLI Tools", "Installation failed:\n%s" % exc)

    def _add_to_path(self, dest):
        """Add dest to the user's PATH. Returns a one-line status message."""
        if sys.platform == "win32":
            try:
                import winreg

                with winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Environment", 0, winreg.KEY_READ | winreg.KEY_WRITE) as key:
                    try:
                        current, _ = winreg.QueryValueEx(key, "Path")
                    except FileNotFoundError:
                        current = ""
                    parts = [p for p in current.split(";") if p]
                    if dest in parts:
                        return "%s is already on your PATH." % dest
                    winreg.SetValueEx(key, "Path", 0, winreg.REG_EXPAND_SZ, ";".join(parts + [dest]))
                return "Added %s to your user PATH. Open a new terminal for it to take effect." % dest
            except Exception as exc:
                return "Could not update PATH automatically (%s). Add %s to PATH manually." % (exc, dest)
        else:
            if dest in os.environ.get("PATH", "").split(os.pathsep):
                return "%s is already on your PATH." % dest
            return (
                "Add this to your shell profile (~/.zshrc, ~/.bashrc, etc.) to use it "
                "from a terminal:\n  export PATH=\"%s:$PATH\"" % dest
            )

    def append_console(self, text):
        self.console.config(state="normal")
        self.console.insert(tk.END, text)
        self.console.see(tk.END)
        self.console.config(state="disabled")

    def execute_cmd(self, cmd, btn):
        btn.config(state="disabled")
        self.console.config(state="normal")
        self.console.delete(1.0, tk.END)
        self.console.config(state="disabled")
        self.append_console(f"$ {' '.join(cmd)}\n\n")

        wszst_dir = os.path.dirname(os.path.abspath(self.wszst_path))
        env = dict(os.environ)
        env["PATH"] = f"{wszst_dir}{os.pathsep}{env.get('PATH', '')}"

        def run_thread():
            try:
                process = subprocess.Popen(
                    cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    bufsize=1,
                    env=env,
                )
                for line in process.stdout:
                    self.after(0, self.append_console, line)

                process.wait()
                self.after(
                    0,
                    self.append_console,
                    f"\nProcess finished with exit code {process.returncode}\n",
                )
            except Exception as e:
                self.after(0, self.append_console, f"\nExecution error: {e}\n")
            finally:
                self.after(0, lambda: btn.config(state="normal"))

        threading.Thread(target=run_thread, daemon=True).start()

    def with_companion_tool_flags(self):
        """--with-wit/--with-mobipeg/--with-sharpii/--with-nsz/--with-vgmtrans/--with-ctrtool/
        --with-ndstool/--with-hactool/--with-hacbrewpack/--with-7z for whatever
        companion tools were found bundled alongside wszst; harmless to pass even
        for operations that don't need them."""
        flags = []
        if self.wit_path:
            flags.append(f"--with-wit={self.wit_path}")
        if self.mobipeg_path:
            flags.append(f"--with-mobipeg={self.mobipeg_path}")
        if self.sharpii_path:
            flags.append(f"--with-sharpii={self.sharpii_path}")
        if self.nsz_path:
            flags.append(f"--with-nsz={self.nsz_path}")
        if self.vgmtrans_path:
            flags.append(f"--with-vgmtrans={self.vgmtrans_path}")
        if self.ctrtool_path:
            flags.append(f"--with-ctrtool={self.ctrtool_path}")
        if self.ndstool_path:
            flags.append(f"--with-ndstool={self.ndstool_path}")
        if self.hactool_path:
            flags.append(f"--with-hactool={self.hactool_path}")
        if self.hacbrewpack_path:
            flags.append(f"--with-hacbrewpack={self.hacbrewpack_path}")
        if self.sevenz_path:
            flags.append(f"--with-7z={self.sevenz_path}")
        return flags

    def run_unpack(self):
        inp = self.unpack_input_var.get().strip()
        outdir = self.unpack_outdir_var.get().strip()

        if not inp:
            messagebox.showwarning("Warning", "Please select an input game or archive file.")
            return

        cmd = [self.wszst_path, "XX"] + self.with_companion_tool_flags()

        if self.unpack_overwrite_var.get():
            cmd.append("-o")
        if self.unpack_auto_var.get():
            cmd.append("-a")
        if outdir:
            cmd.extend(["-d", outdir])

        extra = self.unpack_extra_var.get().strip()
        if extra:
            cmd.extend(extra.split())

        cmd.append(inp)
        self.execute_cmd(cmd, self.unpack_run_btn)

    def run_pack(self):
        inp = self.pack_input_var.get().strip()
        target = self.pack_target_var.get().strip()

        if not inp:
            messagebox.showwarning(
                "Warning", "Please select an extracted (.d) directory to pack."
            )
            return

        cmd = [self.wszst_path, "CREATE"] + self.with_companion_tool_flags()

        if self.pack_overwrite_var.get():
            cmd.append("-o")
        if target:
            cmd.extend(["-d", target])

        extra = self.pack_extra_var.get().strip()
        if extra:
            cmd.extend(extra.split())

        cmd.append(inp)
        self.execute_cmd(cmd, self.pack_run_btn)


# Backward-compatible alias
WszstGUI = NintoolboxGUI

if __name__ == "__main__":
    app = NintoolboxGUI()
    app.mainloop()
