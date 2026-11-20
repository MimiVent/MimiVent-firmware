"""构建组合在预处理阶段拒绝冲突，不依赖 ESP32 SDK。"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FirmwareTransportTest(unittest.TestCase):
    def compile(self, flags, expected=None):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'transport.cpp'
            source.write_text('#include "FirmwareTransport.h"\n' + (expected or '') + '\nint main() {}\n')
            return subprocess.run([compiler, '-std=c++17', '-I', str(ROOT), *flags,
                                   '-fsyntax-only', str(source)], capture_output=True, text=True)

    def test_default_preserves_ble(self):
        r = self.compile([], 'static_assert(MIMIVENT_TRANSPORT_TARGET == 0 && !MIMIVENT_ENABLE_WIFI && !MIMIVENT_WIFI_READ_CONTROL);')
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_explicit_ble_and_nas_map_existing_guards(self):
        for target in [0, 1]:
            r = self.compile([f'-DMIMIVENT_TRANSPORT_TARGET={target}'],
                f'static_assert(MIMIVENT_ENABLE_WIFI == {target} && MIMIVENT_WIFI_READ_CONTROL == {target});')
            self.assertEqual(r.returncode, 0, r.stderr)

    def test_legacy_release_flags_still_select_nas(self):
        r = self.compile(['-DMIMIVENT_ENABLE_WIFI=1', '-DMIMIVENT_WIFI_READ_CONTROL=1'],
                         'static_assert(MIMIVENT_TRANSPORT_TARGET == 1);')
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_conflicting_guards_and_unknown_target_are_rejected(self):
        for flags in [
            ['-DMIMIVENT_TRANSPORT_TARGET=7'],
            ['-DMIMIVENT_TRANSPORT_TARGET=0', '-DMIMIVENT_ENABLE_WIFI=1'],
            ['-DMIMIVENT_TRANSPORT_TARGET=1', '-DMIMIVENT_WIFI_READ_CONTROL=0'],
            ['-DMIMIVENT_WIFI_READ_CONTROL=1'],
            ['-DMIMIVENT_TRANSPORT_TARGET=2'],
            ['-DMIMIVENT_TRANSPORT_TARGET=2', '-DMIMIVENT_REMOTE_SCAFFOLD=1', '-DMIMIVENT_WIFI_READ_CONTROL=1'],
        ]:
            r = self.compile(flags)
            self.assertNotEqual(r.returncode, 0, flags)

    def test_remote_scaffold_cannot_accidentally_enable_plaintext_mqtt(self):
        r = self.compile(['-DMIMIVENT_TRANSPORT_TARGET=2', '-DMIMIVENT_REMOTE_SCAFFOLD=1'],
                         'static_assert(MIMIVENT_ENABLE_WIFI && !MIMIVENT_WIFI_READ_CONTROL);')
        self.assertEqual(r.returncode, 0, r.stderr)
