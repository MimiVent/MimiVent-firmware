"""P2 事务状态边界：重启、幂等、存储失败和最终确认。"""
from pathlib import Path
import shutil, subprocess, tempfile, unittest
ROOT = Path(__file__).resolve().parents[1]
class ServiceTransactionTest(unittest.TestCase):
 def test_transaction_recovery(self):
  src = r'''#include "ServiceTransaction.h"
#include <cassert>
using namespace ServiceTransaction;
struct FakeStore { State saved{}; bool fail=false; unsigned writes=0; };
bool saveState(const State& s, void* ctx) { auto& store=*static_cast<FakeStore*>(ctx); ++store.writes; if(store.fail)return false; store.saved=s; return true; }
int main(){
 State s{}; assert(begin(s,1)==Result::ok); assert(begin(s,1)==Result::ok);
 assert(begin(s,2)==Result::busy); assert(activate(s,1,false)==Result::storage_failed);
 assert(s.phase==Phase::candidate); assert(activate(s,1,true)==Result::ok);
 assert(activate(s,1,true)==Result::ok);
 assert(recoverAfterRestart(s)==Phase::recovery_required);
 assert(activate(s,1,true)==Result::ok);
 assert(cancel(s,1)==Result::recovery_required); assert(confirm(s,1,true)==Result::verification_required);
 assert(markVerified(s,2)==Result::invalid); assert(markVerified(s,1)==Result::ok); assert(confirm(s,1,false)==Result::storage_failed);
 assert(s.phase==Phase::recovery_required); assert(confirm(s,1,true)==Result::ok);
 assert(begin(s,2)==Result::ok); assert(cancel(s,2)==Result::ok);
 assert(begin(s,1)==Result::ok); assert(activate(s,1,true)==Result::ok); assert(markVerified(s,1)==Result::ok); assert(confirm(s,1,true)==Result::ok);
 assert(confirm(s,1,true)==Result::ok); assert(cancel(s,1)==Result::already_confirmed);
 State cancelTest{}; FakeStore cancelStore;
 assert(beginStored(cancelTest,10,saveState,&cancelStore)==Result::ok);
 cancelStore.fail=true;
 assert(cancelStored(cancelTest,10,saveState,&cancelStore)==Result::storage_failed);
 assert(cancelTest.phase==Phase::candidate && cancelStore.saved.phase==Phase::candidate);
 cancelStore.fail=false; assert(cancelStored(cancelTest,10,saveState,&cancelStore)==Result::ok);
 assert(cancelTest.phase==Phase::empty && cancelStore.saved.phase==Phase::empty);
 State d{}; FakeStore store;
 assert(beginStored(d,11,saveState,&store)==Result::ok);
 store.fail=true; assert(activateStored(d,11,saveState,&store)==Result::storage_failed);
 assert(d.phase==Phase::candidate && store.saved.phase==Phase::candidate);
 store.fail=false; assert(activateStored(d,11,saveState,&store)==Result::ok);
 assert(store.saved.phase==Phase::activated);
 State restored=store.saved; assert(recoverAfterRestart(restored)==Phase::recovery_required);
 assert(confirmStored(restored,11,saveState,&store)==Result::verification_required);
 assert(markVerified(restored,11)==Result::ok); store.fail=true;
 assert(confirmStored(restored,11,saveState,&store)==Result::storage_failed);
 assert(restored.phase==Phase::recovery_required); store.fail=false;
 assert(confirmStored(restored,11,saveState,&store)==Result::ok);
 assert(store.saved.phase==Phase::confirmed && !store.saved.verified);
 State t{}; assert(begin(t,2)==Result::ok); assert(recoverAfterRestart(t)==Phase::empty);
 assert(begin(t,3)==Result::ok); assert(activate(t,3,true)==Result::ok); assert(markVerified(t,3)==Result::ok);
 assert(recoverAfterRestart(t)==Phase::recovery_required); assert(confirm(t,3,true)==Result::verification_required);
}'''
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'a.cpp').write_text(src)
   subprocess.run([shutil.which('clang++') or 'c++','-std=c++11','-I',str(ROOT),str(p/'a.cpp'),'-o',str(p/'a')],check=True)
   subprocess.run([str(p/'a')],check=True)
