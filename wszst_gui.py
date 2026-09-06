#!/usr/bin/env python3
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import subprocess
import threading
import os
import sys
import shutil
import sentry_sdk

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
    ("3D Models & Collision", "*.mdl0 *.bcres *.bfres *.bch *.kcl"),
    ("Layouts & Sequences", "*.brlyt *.brlan *.bflyt *.bflan *.ncer *.nanr *.rseq *.cseq *.sseq"),
]

GRID_COLUMNS = 3

_ALL_EXTS = " ".join(exts for _, exts in SUPPORTED_FAMILIES)

FILETYPES = (
    [("All supported", _ALL_EXTS)]
    + list(SUPPORTED_FAMILIES)
    + [("All files", "*.*")]
)


def find_wszst_binary():
    """Locate the wszst binary next to the script, in project/, or in PATH."""
    base_dir = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(base_dir, "wszst"),
        os.path.join(base_dir, "project", "wszst"),
        os.path.join(base_dir, "bin", "wszst"),
        "/usr/local/bin/wszst",
    ]
    for c in candidates:
        if os.path.isfile(c) and os.access(c, os.X_OK):
            return c
    found = shutil.which("wszst")
    if found:
        return found
    return "wszst"


def find_companion_tool(name, wszst_path):
    """Look for NAME bundled alongside the resolved wszst binary first (how
    the .app/PyInstaller build stages wit/mobipeg next to wszst -- see
    wszst-gui.spec / build.yml's build-gui-macos job), then fall back to
    PATH. wszst itself only ever searches PATH by bare name (find_program()
    in lib-passthru.c has no "next to my own binary" fallback), so passing
    an absolute path via --with-wit=/--with-mobipeg= is what actually makes
    a bundled copy usable instead of silently being ignored.
    """
    wszst_dir = os.path.dirname(os.path.abspath(wszst_path))
    candidate = os.path.join(wszst_dir, name)
    if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
        return candidate
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


class WszstGUI(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("wszst-gui — Wiimms SZS Tools Plus")
        self.geometry("760x680")
        self.minsize(660, 520)
        self.configure(padx=15, pady=15)

        self.wszst_path = find_wszst_binary()
        # wit (wiimms-iso-tools-plus) handles disc/DS/WAD pass-through and
        # mobipeg handles video/model transcoding; wszst shells out to both
        # by bare name via PATH only, so an explicit --with-* is the only
        # way a bundled copy actually gets used (see find_companion_tool).
        self.wit_path = find_companion_tool("wit", self.wszst_path)
        self.mobipeg_path = find_companion_tool("mobipeg", self.wszst_path)

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
        ttk.Entry(self.unpack_frame, textvariable=self.unpack_input_var).grid(
            row=0, column=1, sticky="ew", padx=5, pady=5
        )
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
        ttk.Entry(self.pack_frame, textvariable=self.pack_input_var).grid(
            row=0, column=1, sticky="ew", padx=5, pady=5
        )
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

        def run_thread():
            try:
                process = subprocess.Popen(
                    cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    bufsize=1,
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
        """--with-wit/--with-mobipeg for whatever companion tools were found
        bundled alongside wszst; harmless to pass even for operations that
        don't need them."""
        flags = []
        if self.wit_path:
            flags.append(f"--with-wit={self.wit_path}")
        if self.mobipeg_path:
            flags.append(f"--with-mobipeg={self.mobipeg_path}")
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


if __name__ == "__main__":
    app = WszstGUI()
    app.mainloop()
