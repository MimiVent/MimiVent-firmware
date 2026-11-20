from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]

class OtaTransportPolicyTest(unittest.TestCase):
    def test_signed_image_marker_split_boundaries_and_read_failure(self):
        source = r'''
#include "OtaTransportPolicy.h"
#include <string>
#include <cstring>
#include <cassert>
int main() {
 assert(otaIsKnownStableNasRollback("nas", "0f20984acd803e3e73e95faed5b7505dde85c32f204b4da9188aaeb14fba3a90"));
 assert(!otaIsKnownStableNasRollback("ble", "0f20984acd803e3e73e95faed5b7505dde85c32f204b4da9188aaeb14fba3a90"));
 assert(!otaIsKnownStableNasRollback("nas", "wrong"));
 const char* expected = "MIMIVENT_TRANSPORT_TARGET=nas";
 for (int offset = 0; offset < 300; ++offset) {
  std::string image(offset, 'x'); image += expected; image.push_back(0); image += "tail";
  auto read = [&](size_t start, void* out, size_t n) {memcpy(out, image.data()+start,n); return true;};
  assert(otaImageMatchesTransport(image.size(), expected, read));
 }
 for (auto suffix : {"ble", "remote", "nas-extra", ""}) {
  std::string image = std::string("MIMIVENT_TRANSPORT_TARGET=") + suffix;image.push_back(0);
  auto read = [&](size_t start, void* out, size_t n) {memcpy(out,image.data()+start,n);return true;};
  assert(!otaImageMatchesTransport(image.size(), expected, read));
 }
 auto failed = [](size_t, void*, size_t) {return false;};
 assert(!otaImageMatchesTransport(256,expected,failed));
}
'''
        with tempfile.TemporaryDirectory() as d:
            p = Path(d); (p/'test.cpp').write_text(source)
            subprocess.run([shutil.which('clang++') or 'g++', '-std=c++17', '-I', str(ROOT), str(p/'test.cpp'), '-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)
