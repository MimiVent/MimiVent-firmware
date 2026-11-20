import pathlib, shutil, subprocess, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]

class ServiceAuthTest(unittest.TestCase):
 def test_candidate_activate_cancel_clear_failure_and_corruption(self):
  source=r'''
#include "WifiBindingCore.h"
#include <assert.h>
#include <stdio.h>
using namespace WifiBindings;
bool fail=false; int writes=0;
bool save(const Record& r,void*) { ++writes; assert(valid(r)); return !fail; }
#define SID "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define CID "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
#define TX1 "11111111-2222-3333-4444-555555555555"
#define TX2 "22222222-3333-4444-5555-666666666666"
int main(){
 const char* owner="AA-BB-CC-DD-EE-FF";
 // 激活候选只进入待核验阶段；旧服务必须保留到显式 confirm。
 Core atomic;
 auto& old=atomic.active.active;
 old.used=kEntryActive; old.stage=kStageActivated;
 strcpy(old.serviceId,SID); strcpy(old.credentialId,CID); old.authRevision=7;
 atomic.active.sequence=1; seal(atomic.active);
 assert(!atomic.open(owner,0));
 uint8_t replacementHash[32]; memset(replacementHash,9,32);
 assert(!atomic.begin(owner,TX1,"cccccccccccccccccccccccccccccccc",
   "dddddddddddddddddddddddddddddddd",replacementHash,8,1,save,nullptr));
 assert(!atomic.activate(owner,TX1,2,save,nullptr));
 assert(!strcmp(atomic.active.active.credentialId,CID));
 assert(!strcmp(atomic.active.candidate.credentialId,"dddddddddddddddddddddddddddddddd"));
 assert(atomic.active.candidate.stage==kStageActivated);
 assert(!atomic.confirm(TX1,save,nullptr));
 assert(!strcmp(atomic.active.active.credentialId,"dddddddddddddddddddddddddddddddd"));
 assert(atomic.active.candidate.used==kEntryEmpty);

 Core c;
 uint8_t h1[32]; memset(h1,1,32);
 // 未开窗不能 begin
 assert(!strcmp(c.begin(owner,TX1,SID,CID,h1,1,0,save,nullptr),"BIND_WINDOW_CLOSED"));
 assert(!c.open(owner,0));
 // 非法参数
 assert(!strcmp(c.begin(owner,"bad",SID,CID,h1,1,1,save,nullptr),"INVALID_REQUEST"));
 assert(!strcmp(c.begin(owner,TX1,"zz",CID,h1,1,1,save,nullptr),"INVALID_REQUEST"));
 // 生成候选
 assert(!c.begin(owner,TX1,SID,CID,h1,1,2,save,nullptr));
 assert(c.active.candidate.used==kEntryCandidate);
 assert(c.active.candidate.stage==kStagePending);
 assert(c.active.active.used==kEntryEmpty);
 assert(valid(c.active));
 // 事务不匹配
 assert(!strcmp(c.activate(owner,TX2,3,save,nullptr),"TRANSACTION_MISMATCH"));
 // 激活
 assert(!c.activate(owner,TX1,4,save,nullptr));
 assert(c.active.active.used==kEntryEmpty);
 assert(c.active.candidate.stage==kStageActivated);
 assert(c.active.candidate.authRevision==1);
 assert(!c.confirm(TX1,save,nullptr));
 assert(c.active.active.used==kEntryActive);
 assert(c.active.active.authRevision==1);
 assert(!strcmp(c.active.active.credentialId,CID));
 // 激活后窗口关闭，需重新开窗
 assert(!strcmp(c.begin(owner,TX2,SID,CID,h1,2,5,save,nullptr),"BIND_WINDOW_CLOSED"));
 // 持久化失败不生效
 fail=true;
 assert(!c.open(owner,10));
 uint8_t h2[32]; memset(h2,2,32);
 assert(!strcmp(c.begin(owner,TX2,SID,CID,h2,2,11,save,nullptr),"BINDINGS_STORE_FAILED"));
 assert(c.active.active.authRevision==1);
 fail=false;
 // 重新开窗后候选过期自动清除，不阻塞
 assert(!c.open(owner,20));
 assert(!c.begin(owner,TX2,SID,CID,h2,2,21,save,nullptr));
 c.tick(21+kTtlMs+1);
 assert(c.active.candidate.used==0);
 // 轮换：新事务替换有效授权
 assert(!c.open(owner,30));
 uint8_t h3[32]; memset(h3,3,32);
 assert(!c.begin(owner,TX2,SID,"cccccccccccccccccccccccccccccccc",h3,2,31,save,nullptr));
 assert(!c.activate(owner,TX2,32,save,nullptr));
 assert(c.active.active.authRevision==1);
 assert(c.active.candidate.authRevision==2);
 assert(c.active.candidate.credentialHash[0]==3);
 // 事务号不匹配的取消必须拒绝，匹配才清除
 assert(!strcmp(c.cancel(owner,TX1,save,nullptr),"TRANSACTION_MISMATCH"));
 assert(!c.cancel(owner,TX2,save,nullptr));
 assert(c.active.candidate.used==0);
 assert(c.active.active.authRevision==1);
 // 重启丢弃未激活候选，但保留已激活待核验候选和旧有效授权。
 Core pendingRecovery; assert(!pendingRecovery.open(owner,0));
 assert(!pendingRecovery.begin(owner,TX1,SID,CID,h1,1,1,save,nullptr));
 assert(pendingRecovery.recoverAfterRestart(save,nullptr));
 assert(pendingRecovery.active.candidate.used==0);
 Core activatedRecovery;
 auto& retained=activatedRecovery.active.active;
 retained.used=kEntryActive; retained.stage=kStageActivated;
 strcpy(retained.serviceId,SID); strcpy(retained.credentialId,CID); retained.authRevision=7;
 activatedRecovery.active.sequence=1; seal(activatedRecovery.active);
 assert(!activatedRecovery.open(owner,0));
 assert(!activatedRecovery.begin(owner,TX1,"cccccccccccccccccccccccccccccccc",
   "dddddddddddddddddddddddddddddddd",h2,8,1,save,nullptr));
 assert(!activatedRecovery.activate(owner,TX1,2,save,nullptr));
 assert(activatedRecovery.recoverAfterRestart(save,nullptr));
 assert(!strcmp(activatedRecovery.active.active.credentialId,CID));
 assert(activatedRecovery.active.candidate.stage==kStageActivated);
 activatedRecovery.tick(kTtlMs+10);
 assert(activatedRecovery.active.candidate.used==kEntryCandidate);
 // 清除全部
 assert(!c.reset(save,nullptr));
 assert(c.active.active.used==0 && c.active.candidate.used==0);
 // 损坏检测
 auto damaged=c.active; damaged.sequence=9; damaged.active.used=kEntryActive; damaged.active.stage=kStageActivated;
 memcpy(damaged.active.serviceId,SID,32); memcpy(damaged.active.credentialId,CID,32); seal(damaged);
 assert(damaged.magic==0x3142564d);
 assert(damaged.version==kRecordVersion);
 assert(damaged.bytes==sizeof(damaged));
 assert(damaged.sequence==9);
 assert(damaged.reserved==0);
 assert(damaged.checksum==crc(reinterpret_cast<const uint8_t*>(&damaged),offsetof(Record,checksum)));
 assert(validEntry(damaged.active));
 assert(validEntry(damaged.candidate));
 assert(damaged.active.used!=kEntryCandidate);
 assert(damaged.candidate.used!=kEntryActive);
 assert(memchr(damaged.deviceId,0,sizeof(damaged.deviceId))!=nullptr);
 assert(!damaged.deviceId[0]);
 assert(valid(damaged));
 // 篡改字段但不重算校验和：必须被拒绝（读回损坏检测）
 damaged.active.credentialHash[0]^=1; assert(!valid(damaged));
 // 重算校验和后重新有效，说明篡改本身可被 save() 后的 valid() 接受
 seal(damaged); assert(valid(damaged));
 damaged.active.used=7; seal(damaged); assert(!valid(damaged));
 // 序列耗尽
 Core wrap; wrap.active.sequence=UINT32_MAX;
 assert(!wrap.open(owner,0));
 assert(!strcmp(wrap.begin(owner,TX1,SID,CID,h1,1,1,save,nullptr),"SEQUENCE_EXHAUSTED"));
}
'''
  with tempfile.TemporaryDirectory() as d:
   p=pathlib.Path(d);(p/'main.cpp').write_text(source)
   subprocess.run([shutil.which('clang++') or 'c++','-std=c++11','-fsanitize=address,undefined','-I',str(ROOT),str(p/'main.cpp'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
