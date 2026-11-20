"""执行真实块完成逻辑，保证固件偏移与压缩包偏移属于同一检查点。"""
from pathlib import Path
import subprocess, tempfile, shutil, unittest
ROOT=Path(__file__).resolve().parents[1]
class OtaCheckpointAlignmentTest(unittest.TestCase):
 def test_image_and_package_offsets_are_saved_atomically_at_checkpoint_boundary(self):
  source=(ROOT/'OtaManager.cpp').read_text()
  body=source[source.index('bool finishOtaPackageBlock()'):source.index('bool consumeOtaPackageBytes(')]
  prefix=r'''
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <cassert>
using std::min;
constexpr uint32_t kOtaPackageBlockBytes=4096,kCheckpointBytes=16384;
uint8_t otaImageBlock[4096]{},otaPackageBlock[4096]{};
uint32_t otaPackageBlockCodec=0,otaPackageBlockStoredLength=0,otaPackageBlockRawLength=0,otaPackageBlockCrc=0;
uint32_t otaPackageBlockIndex=0,otaPackageDescriptorBytes=0,otaPackageBlockBytes=0;
uint32_t expectedSize=4096*12,receivedSize=0,packageCheckpointOffset=0,packageReceivedSize=512,checkpointOffset=0;
uint32_t persistedImage=0,persistedPackage=0,saves=0;
std::string packageFailure;
bool failSave=false;
uint32_t crc32(const uint8_t*,size_t){return 0;}
bool otaInflateBlock(const uint8_t*,size_t,uint8_t*,size_t,size_t*){return false;}
bool writeDecodedOtaBlock(const uint8_t*,size_t n){receivedSize+=n;return true;}
bool saveCheckpoint(uint32_t n){if(failSave)return false;persistedImage=n;persistedPackage=packageCheckpointOffset;checkpointOffset=n;++saves;return true;}
'''
  suffix=r'''
void block() {otaPackageBlockCodec=0;otaPackageBlockStoredLength=otaPackageBlockRawLength=4096;packageReceivedSize+=4112;}
int main(){
 for(int i=0;i<9;++i){
  block();assert(finishOtaPackageBlock());
  if(persistedImage)assert(persistedPackage==512+(persistedImage/4096)*4112);
  assert(saves==uint32_t((i+1)/4));
 }
 // 第三个 checkpoint 持久化失败时必须终止，不把成功状态暴露给 App。
 block();assert(finishOtaPackageBlock());block();assert(finishOtaPackageBlock());
 expectedSize+=4096;failSave=true;block();assert(!finishOtaPackageBlock());
 assert(packageFailure=="CHECKPOINT_WRITE");assert(persistedImage==32768);
}
'''
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'test.cpp').write_text(prefix+body+suffix)
   subprocess.run([shutil.which('clang++') or 'g++','-std=c++17',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
