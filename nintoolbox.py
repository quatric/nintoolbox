#!/usr/bin/env python3
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import subprocess
import threading
import os
import sys
import shutil
import shlex
import queue

try:
    import sentry_sdk
except ImportError:
    sentry_sdk = None

try:
    from tkinterdnd2 import TkinterDnD, DND_FILES
except ImportError:
    TkinterDnD = None
    DND_FILES = None

if sentry_sdk is not None:
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
    ("SZS / Archives", "*.szs *.carc *.arc *.brres *.sarc *.pac *.pcs *.gfa *.rarc *.pak *.zdat"),
    ("Textures & Images", "*.tpl *.bti *.tex0 *.bflim *.bclim *.ncgr *.nclr *.bntx *.txd"),
    ("3D Models & Collision", "*.mdl0 *.bcres *.bfres *.bch *.kcl *.csb *.ctb"),
    ("Layouts & Sequences", "*.brlyt *.brlan *.bflyt *.bflan *.ncer *.nanr *.rseq *.cseq *.sseq"),
    ("Flash / ActionScript", "*.swf"),
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
    candidates = [
        os.path.join(base_dir, "wszst"),
        os.path.join(base_dir, "project", "bin", "wszst"),
        os.path.join(base_dir, "project", "wszst"),
        os.path.join(base_dir, "bin", "wszst"),
    ]
    parent = os.path.dirname(base_dir)
    for sibling in ("Frameworks", "MacOS", "Resources"):
        candidates.append(os.path.join(parent, sibling, "wszst"))
        candidates.append(os.path.join(parent, sibling, "bin", "wszst"))
    if sys.platform == "win32":
        local_app = os.environ.get("LOCALAPPDATA", os.path.expanduser("~"))
        candidates.append(os.path.join(local_app, "nintoolbox", "bin", "wszst"))
    else:
        candidates.append(os.path.expanduser("~/.local/bin/wszst"))
        candidates.append("/usr/local/bin/wszst")

    for candidate in candidates:
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
    base = bundle_dir()
    candidates = [
        os.path.join(base, name),
        os.path.join(base, "bin", name),
        os.path.join(base, "extra_tools", name),
    ]
    if wszst_path and wszst_path != "wszst":
        wszst_dir = os.path.dirname(os.path.abspath(wszst_path))
        candidates.append(os.path.join(wszst_dir, name))
        candidates.append(os.path.join(wszst_dir, "extra_tools", name))
    parent = os.path.dirname(base)
    for sibling in ("Frameworks", "MacOS", "Resources"):
        candidates.append(os.path.join(parent, sibling, name))
        candidates.append(os.path.join(parent, sibling, "bin", name))

    for cand in candidates:
        found = _find_executable(cand)
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
    """Tkdnd supplies a Tcl list, including Tcl quoting and escaping."""
    try:
        paths = tk.Tcl().splitlist(data)
    except tk.TclError:
        return ""
    return paths[0] if paths else ""


def _clean_path(path):
    """Strip leading/trailing whitespace and matching outer quote/brace pairs."""
    path = path.strip()
    if len(path) >= 2 and (
        (path[0] == '"' and path[-1] == '"')
        or (path[0] == "'" and path[-1] == "'")
        or (path[0] == "{" and path[-1] == "}")
    ):
        return path[1:-1].strip()
    return path


def _parse_extra_args(data):
    """Group quoted arguments while preserving Windows path separators."""
    lexer = shlex.shlex(data, posix=True)
    lexer.whitespace_split = True
    lexer.commenters = ""
    if os.name == "nt":
        lexer.escape = ""
    return list(lexer)


class NintoolboxGUI(TkinterDnD.Tk if TkinterDnD else tk.Tk):
    def report_callback_exception(self, exc, val, tb):
        if sentry_sdk is not None:
            try:
                sentry_sdk.capture_exception((exc, val, tb))
            except Exception:
                pass
        try:
            super().report_callback_exception(exc, val, tb)
        except (TypeError, AttributeError):
            import traceback
            traceback.print_exception(exc, val, tb)


    def __init__(self):
        super().__init__()
        self.title("nintoolbox — Nintendo Toolbox")
        self.geometry("760x680")
        self.minsize(660, 520)
        self.configure(padx=15, pady=15)

        self._command_running = False
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
        self.ffdec_path = find_companion_tool("ffdec", self.wszst_path)

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
        val = _clean_path(self.unpack_input_var.get())
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
        val = _clean_path(self.pack_input_var.get())
        if val and os.path.isdir(val):
            # If folder ends with .d, default target is without .d
            if val.endswith(".d"):
                target = val[:-2]
                self.pack_target_var.set(target)
            else:
                self.pack_target_var.set("")

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
        """Copy every bundled CLI tool (and share/ data, keys) onto the user's
        PATH, and add the destination to PATH if it isn't already there.
        Mirrors installer/install.sh and installer/install.ps1, but as a
        one-click in-app action instead of a script the user runs by hand.
        """
        tools_dir = bundle_dir()
        gui_names = {
            "nintoolbox", "nintoolbox.exe",
            "python", "python3", "python.exe", "python3.exe", "pythonw.exe",
        }
        skip_exts = {
            ".dylib", ".so", ".a", ".o", ".framework", ".plist", ".py", ".pyc", ".pyd"
        }

        if sys.platform == "win32":
            dest = os.path.join(os.environ.get("LOCALAPPDATA", os.path.expanduser("~")), "nintoolbox", "bin")
            def is_tool(p):
                name = os.path.basename(p).lower()
                return name.endswith((".exe", ".dll")) and name not in gui_names
        else:
            dest = os.path.expanduser("~/.local/bin")
            def is_tool(p):
                name = os.path.basename(p)
                if name.lower() in gui_names or any(name.endswith(ext) for ext in skip_exts) or ".so." in name:
                    return False
                return os.path.isfile(p) and os.access(p, os.X_OK)

        try:
            os.makedirs(dest, exist_ok=True)
            installed = []
            search_dirs = [tools_dir]
            for sub in ("bin", "project/bin", "extra_tools"):
                d = os.path.join(tools_dir, sub)
                if os.path.isdir(d):
                    search_dirs.append(d)
            parent = os.path.dirname(tools_dir)
            for sibling in ("Frameworks", "MacOS", "Resources"):
                d = os.path.join(parent, sibling)
                if os.path.isdir(d):
                    search_dirs.append(d)

            seen_tools = set()
            for sdir in search_dirs:
                for name in sorted(os.listdir(sdir)):
                    if name in seen_tools:
                        continue
                    src = os.path.join(sdir, name)
                    if not is_tool(src):
                        continue
                    shutil.copy2(src, os.path.join(dest, name))
                    seen_tools.add(name)
                    installed.append(name)

            # Copy bundled key files
            for key_file in ("seeddb.bin", "prod.keys", "title.keys", "keys.txt"):
                for sdir in search_dirs:
                    kpath = os.path.join(sdir, key_file)
                    if os.path.isfile(kpath):
                        shutil.copy2(kpath, os.path.join(dest, key_file))
                        installed.append(key_file)
                        break

            # Copy wiiu_keys directory
            wiiu_dest = os.path.join(dest, "wiiu_keys")
            for sdir in search_dirs:
                wpath = os.path.join(sdir, "wiiu_keys")
                if os.path.isdir(wpath):
                    os.makedirs(wiiu_dest, exist_ok=True)
                    for item in os.listdir(wpath):
                        src_item = os.path.join(wpath, item)
                        if os.path.isfile(src_item):
                            shutil.copy2(src_item, os.path.join(wiiu_dest, item))
                    installed.append("wiiu_keys/")
                    break

            # Copy share/ directory
            share_dest = os.path.join(os.path.dirname(dest), "share", "nintoolbox")
            for sdir in search_dirs:
                spath = os.path.join(sdir, "share")
                if os.path.isdir(spath):
                    os.makedirs(share_dest, exist_ok=True)
                    for item in os.listdir(spath):
                        src_item = os.path.join(spath, item)
                        if os.path.isfile(src_item):
                            shutil.copy2(src_item, os.path.join(share_dest, item))
                    if sys.platform == "win32":
                        win_share = os.path.join(dest, "share")
                        os.makedirs(win_share, exist_ok=True)
                        for item in os.listdir(spath):
                            src_item = os.path.join(spath, item)
                            if os.path.isfile(src_item):
                                shutil.copy2(src_item, os.path.join(win_share, item))
                    installed.append("share/")
                    break

            for sdir in search_dirs:
                ffdec_jar = os.path.join(sdir, "ffdec.jar")
                if os.path.isfile(ffdec_jar):
                    shutil.copy2(ffdec_jar, os.path.join(dest, "ffdec.jar"))
                    installed.append("ffdec.jar")
                    break

            for sdir in search_dirs:
                lib_src = os.path.join(sdir, "lib")
                if os.path.isdir(lib_src):
                    lib_dest = os.path.join(dest, "lib")
                    os.makedirs(lib_dest, exist_ok=True)
                    for name in os.listdir(lib_src):
                        shutil.copy2(os.path.join(lib_src, name), os.path.join(lib_dest, name))
                    installed.append("lib/")
                    break

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
        if self._command_running:
            return
        self._command_running = True
        for button in (self.unpack_run_btn, self.pack_run_btn):
            button.config(state="disabled")
        self.console.config(state="normal")
        self.console.delete(1.0, tk.END)
        self.console.config(state="disabled")
        self.append_console(f"$ {shlex.join(cmd)}\n\n")

        wszst_dir = (
            os.path.dirname(os.path.abspath(self.wszst_path))
            if self.wszst_path and self.wszst_path != "wszst"
            else ""
        )
        env = dict(os.environ)
        path_entries = [bundle_dir()]
        if wszst_dir:
            path_entries.append(wszst_dir)
        bin_sub = os.path.join(bundle_dir(), "bin")
        if os.path.isdir(bin_sub):
            path_entries.append(bin_sub)
        parent_dir = os.path.dirname(bundle_dir())
        for sibling in ("MacOS", "Frameworks", "Resources"):
            sib_dir = os.path.join(parent_dir, sibling)
            if os.path.isdir(sib_dir):
                path_entries.append(sib_dir)
        env["PATH"] = os.pathsep.join(path_entries + [env.get("PATH", "")])
        output = queue.Queue()

        popen_kwargs = {}
        if sys.platform == "win32":
            popen_kwargs["creationflags"] = subprocess.CREATE_NO_WINDOW

        def run_thread():
            try:
                with subprocess.Popen(
                    cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    errors="replace",
                    bufsize=1,
                    env=env,
                    **popen_kwargs,
                ) as process:
                    for line in process.stdout:
                        output.put(line)
                    process.wait()
                    output.put(f"\nProcess finished with exit code {process.returncode}\n")
            except Exception as e:
                output.put(f"\nExecution error: {e}\n")
            finally:
                output.put(None)

        def poll_output():
            # All Tk calls stay on the main thread, including after(). Limit
            # each batch so a verbose child cannot starve the event loop.
            for _ in range(200):
                try:
                    line = output.get_nowait()
                except queue.Empty:
                    break
                if line is None:
                    self._command_running = False
                    for button in (self.unpack_run_btn, self.pack_run_btn):
                        button.config(state="normal")
                    return
                self.append_console(line)
            self.after(50, poll_output)

        threading.Thread(target=run_thread, daemon=True).start()
        self.after(50, poll_output)

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
        if self.ffdec_path:
            flags.append(f"--with-ffdec={self.ffdec_path}")
        return flags

    def run_unpack(self):
        inp = _clean_path(self.unpack_input_var.get())
        outdir = _clean_path(self.unpack_outdir_var.get())

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
        try:
            cmd.extend(_parse_extra_args(extra))
        except ValueError as exc:
            messagebox.showerror("Invalid arguments", str(exc))
            return

        cmd.extend(["--", inp])
        self.execute_cmd(cmd, self.unpack_run_btn)

    def run_pack(self):
        inp = _clean_path(self.pack_input_var.get())
        target = _clean_path(self.pack_target_var.get())

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
        try:
            cmd.extend(_parse_extra_args(extra))
        except ValueError as exc:
            messagebox.showerror("Invalid arguments", str(exc))
            return

        cmd.extend(["--", inp])
        self.execute_cmd(cmd, self.pack_run_btn)


# Backward-compatible alias
WszstGUI = NintoolboxGUI

if __name__ == "__main__":
    app = NintoolboxGUI()
    app.mainloop()
