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

    def test_doctor_is_available_in_dry_run(self):
        result = run_dev("doctor")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("doctor checks skipped in --dry-run", result.stdout)


if __name__ == "__main__":
    unittest.main()
