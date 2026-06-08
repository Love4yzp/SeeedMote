import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEV = ROOT / "dev"


def run_dev(*args):
    return subprocess.run(
        [sys.executable, str(DEV), "--dry-run", *args],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


class DevCliTest(unittest.TestCase):
    def test_mote_debug_build_uses_ncs_toolchain_and_debug_conf(self):
        result = run_dev("mote", "build", "--debug", "--ncs", "v2.9.2")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("cwd: mote", result.stdout)
        self.assertIn("nrfutil toolchain-manager launch --ncs-version v2.9.2 -- west", result.stdout)
        self.assertIn("west build --no-sysbuild -p always", result.stdout)
        self.assertIn("-b xiao_ble/nrf52840/sense", result.stdout)
        self.assertIn("-d build_uf2", result.stdout)
        self.assertIn("-DEXTRA_CONF_FILE=debug.conf", result.stdout)

    def test_gateway_log_delegates_to_esphome_logs(self):
        result = run_dev("gateway", "log")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("esphome logs gateway/esphome.yaml", result.stdout)

    def test_app_mock_run_delegates_to_app_entrypoint(self):
        result = run_dev("app", "run", "--mock")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("make -C app dev MOCK=true", result.stdout)

    def test_app_makefile_dev_trap_cleans_long_running_children(self):
        makefile = (ROOT / "app" / "Makefile").read_text()

        self.assertIn("trap cleanup INT TERM HUP EXIT", makefile)
        self.assertIn("cd $(FRONTEND_DIR) &&", makefile)
        self.assertIn("./node_modules/.bin/vite", makefile)
        self.assertNotIn("npm --prefix $(FRONTEND_DIR) run dev &", makefile)

    def test_dev_app_run_forwards_stop_signals_to_process_group(self):
        dev = DEV.read_text()

        self.assertIn("signal.SIGHUP", dev)
        self.assertIn("run_command_for_app(command", dev)

    def test_doctor_is_available_in_dry_run(self):
        result = run_dev("doctor")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("doctor checks skipped in --dry-run", result.stdout)

    def test_dry_run_is_accepted_after_nested_command(self):
        result = subprocess.run(
            [sys.executable, str(DEV), "mote", "build", "--dry-run"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("cwd: mote", result.stdout)
        self.assertIn("west build --no-sysbuild -p always", result.stdout)

    def test_dry_run_is_accepted_after_top_level_command(self):
        result = subprocess.run(
            [sys.executable, str(DEV), "doctor", "--dry-run"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("doctor checks skipped in --dry-run", result.stdout)

    def test_help_command_prints_root_examples(self):
        result = subprocess.run(
            [sys.executable, str(DEV), "help"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("SeeedMote v2 development CLI", result.stdout)
        self.assertIn("./dev mote build", result.stdout)
        self.assertIn("./dev gateway run", result.stdout)

    def test_help_command_prints_nested_command_help(self):
        result = subprocess.run(
            [sys.executable, str(DEV), "help", "mote", "build"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("usage: ./dev mote build", result.stdout)
        self.assertIn("--debug", result.stdout)
        self.assertIn("--dry-run", result.stdout)

    def test_invalid_command_points_to_valid_commands(self):
        result = subprocess.run(
            [sys.executable, str(DEV), "unknown"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Unknown command: unknown", result.stderr)
        self.assertIn("Valid commands:", result.stderr)
        self.assertIn("mote", result.stderr)
        self.assertIn("gateway", result.stderr)

    def test_group_command_without_action_prints_group_examples(self):
        result = subprocess.run(
            [sys.executable, str(DEV), "app"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("usage: ./dev app", result.stdout)
        self.assertIn("{run}", result.stdout)
        self.assertIn("./dev app run --mock", result.stdout)
        self.assertNotIn("install", result.stdout)
        self.assertEqual(result.stderr, "")

    def test_app_install_is_not_exposed_as_public_cli_action(self):
        result = run_dev("app", "install")

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("invalid choice: 'install'", result.stderr)

    def test_mote_help_only_lists_canonical_actions(self):
        result = subprocess.run(
            [sys.executable, str(DEV), "mote"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("{build,flash,log,run,clean}", result.stdout)
        self.assertNotIn("monitor", result.stdout)

    def test_gateway_help_only_lists_canonical_actions(self):
        result = subprocess.run(
            [sys.executable, str(DEV), "gateway"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("{compile,run,log}", result.stdout)
        self.assertNotIn("build", result.stdout)
        self.assertNotIn("logs", result.stdout)

    def test_redundant_aliases_are_not_public_cli_actions(self):
        cases = (
            ("mote", "monitor"),
            ("gateway", "build"),
            ("gateway", "logs"),
        )

        for args in cases:
            with self.subTest(args=args):
                result = run_dev(*args)

                self.assertNotEqual(result.returncode, 0)
                self.assertIn(f"invalid choice: '{args[1]}'", result.stderr)


if __name__ == "__main__":
    unittest.main()
