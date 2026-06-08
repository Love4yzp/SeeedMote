import os
import unittest

from settings import Settings


class SettingsTests(unittest.TestCase):
    def tearDown(self):
        os.environ.pop("SEEEDMOTE_APP_PORT", None)
        os.environ.pop("PORT", None)

    def test_port_accepts_seeedmote_app_port(self):
        os.environ["SEEEDMOTE_APP_PORT"] = "3101"
        settings = Settings()
        self.assertEqual(settings.port, 3101)

    def test_port_accepts_port_alias(self):
        os.environ["PORT"] = "3201"
        settings = Settings()
        self.assertEqual(settings.port, 3201)


if __name__ == "__main__":
    unittest.main()
