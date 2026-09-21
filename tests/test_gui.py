"""Headless GUI regressions: python3 -m unittest discover -s tests -p test_gui.py."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location(
    "nintoolbox_gui", Path(__file__).resolve().parents[1] / "nintoolbox.py"
)
gui = importlib.util.module_from_spec(spec)
# Importing tests must not initialize external reporting.
with patch.dict(sys.modules, {"sentry_sdk": None}):
    spec.loader.exec_module(gui)


class Variable:
    def __init__(self, value=""):
        self.value = value

    def get(self):
        return self.value

    def set(self, value):
        self.value = value


class GuiTests(unittest.TestCase):
    def test_reporting_dependency_is_optional(self):
        self.assertIsNone(gui.sentry_sdk)

    def test_drop_paths_use_tcl_list_rules(self):
        for path in ("/tmp/a b", "/tmp/a {b} c", "/tmp/a}b", r"C:\My Games\game.iso"):
            encoded = gui.tk._join((path, "/tmp/second"))
            self.assertEqual(gui._parse_dnd_path(encoded), path)
        self.assertEqual(gui._parse_dnd_path(r"/tmp/a\ b /tmp/c"), "/tmp/a b")
        self.assertEqual(gui._parse_dnd_path(""), "")
        self.assertEqual(gui._parse_dnd_path("{unclosed"), "")

    def test_extra_args_group_quotes_and_keep_hashes(self):
        self.assertEqual(gui._parse_extra_args('--dest "my game.d" --name="a # b"'),
                         ["--dest", "my game.d", "--name=a # b"])
        with self.assertRaises(ValueError):
            gui._parse_extra_args('"unclosed')

    def test_windows_extra_args_keep_backslashes(self):
        with patch.object(gui.os, "name", "nt"):
            self.assertEqual(gui._parse_extra_args(r'--dest "C:\My Games\out"'),
                             ["--dest", r"C:\My Games\out"])

    def test_switching_pack_directory_clears_stale_target(self):
        with tempfile.TemporaryDirectory() as root:
            archive = Path(root) / "game.arc.d"
            plain = Path(root) / "other"
            archive.mkdir()
            plain.mkdir()
            form = SimpleNamespace(pack_input_var=Variable(str(archive)), pack_target_var=Variable())
            gui.NintoolboxGUI.on_pack_input_changed(form)
            self.assertEqual(form.pack_target_var.get(), str(archive)[:-2])
            form.pack_input_var.set(str(plain))
            gui.NintoolboxGUI.on_pack_input_changed(form)
            self.assertEqual(form.pack_target_var.get(), "")

    def make_form(self):
        form = SimpleNamespace(wszst_path="wszst", with_companion_tool_flags=lambda: [], execute_cmd=Mock())
        for prefix in ("pack", "unpack"):
            for suffix, value in (("input", "-game.arc"), ("extra", '--name "my game"'),
                                  ("overwrite", False), ("auto", False), ("outdir", ""), ("target", "")):
                setattr(form, f"{prefix}_{suffix}_var", Variable(value))
            setattr(form, f"{prefix}_run_btn", Mock())
        return form

    def test_commands_preserve_quoted_args_and_option_like_filenames(self):
        for method in (gui.NintoolboxGUI.run_pack, gui.NintoolboxGUI.run_unpack):
            form = self.make_form()
            method(form)
            cmd = form.execute_cmd.call_args.args[0]
            self.assertEqual(cmd[-4:], ["--name", "my game", "--", "-game.arc"])

    def test_invalid_quotes_do_not_launch_a_process(self):
        for prefix, method in (("pack", gui.NintoolboxGUI.run_pack), ("unpack", gui.NintoolboxGUI.run_unpack)):
            form = self.make_form()
            getattr(form, f"{prefix}_extra_var").set('"unclosed')
            with patch.object(gui.messagebox, "showerror") as error:
                method(form)
                error.assert_called_once()
            form.execute_cmd.assert_not_called()

    def run_child(self, cmd):
        callbacks, logs = [], []
        main_thread = threading.get_ident()

        def after(delay, callback):
            self.assertEqual(threading.get_ident(), main_thread)
            callbacks.append(callback)

        def append(text):
            self.assertEqual(threading.get_ident(), main_thread)
            logs.append(text)

        form = SimpleNamespace(_command_running=False, unpack_run_btn=Mock(), pack_run_btn=Mock(),
                               console=Mock(), wszst_path=sys.executable, after=after, append_console=append)
        gui.NintoolboxGUI.execute_cmd(form, cmd, form.unpack_run_btn)
        self.assertTrue(form._command_running)
        form.unpack_run_btn.config.assert_called_with(state="disabled")
        form.pack_run_btn.config.assert_called_with(state="disabled")
        # A second tab cannot clear the console or start another process.
        gui.NintoolboxGUI.execute_cmd(form, ["must-not-run"], form.pack_run_btn)
        self.assertEqual(form.console.delete.call_count, 1)
        deadline = time.monotonic() + 10
        while callbacks and time.monotonic() < deadline:
            callbacks.pop(0)()
            time.sleep(0.01)
        self.assertFalse(form._command_running)
        form.unpack_run_btn.config.assert_called_with(state="normal")
        form.pack_run_btn.config.assert_called_with(state="normal")
        return "".join(logs)

    def test_child_output_with_invalid_bytes_and_failure_status(self):
        output = self.run_child([sys.executable, "-c", "import os; os.write(1, b'hello\\xff\\n'); raise SystemExit(7)"])
        self.assertIn("hello", output)
        self.assertIn("exit code 7", output)
        self.assertNotIn("Execution error:", output)

    def test_missing_executable_restores_buttons(self):
        with tempfile.TemporaryDirectory() as root:
            output = self.run_child([str(Path(root) / "missing")])
        self.assertIn("Execution error:", output)

    def test_bundle_directory_is_available_to_companion_tools(self):
        output = self.run_child([sys.executable, "-c", "import os; print(os.environ['PATH'].split(os.pathsep)[0])"])
        self.assertIn(gui.bundle_dir() + "\n", output)


if __name__ == "__main__":
    unittest.main()
