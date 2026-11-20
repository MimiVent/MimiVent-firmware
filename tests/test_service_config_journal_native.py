"""服务配置双槽存储：断写、损坏和恢复。"""
from pathlib import Path
import shutil, subprocess, tempfile, unittest
ROOT=Path(__file__).resolve().parents[1]
class JournalTest(unittest.TestCase):
 def test_recovery(self):
  source=r'''#include "ServiceConfigJournal.h"
#include <cassert>
#include <cstring>
using namespace ServiceConfigJournal;
struct Memory { Record slot[2]{}; bool exists[2]{}; bool failWrite=false, failRead=false, corrupt=false; };
bool readSlot(void* ctx, unsigned i, Record& out, bool& exists){auto& m=*static_cast<Memory*>(ctx);if(m.failRead)return false;exists=m.exists[i];if(exists)out=m.slot[i];return true;}
bool writeSlot(void* ctx,unsigned i,const Record& in){auto& m=*static_cast<Memory*>(ctx);if(m.failWrite)return false;m.slot[i]=in;m.exists[i]=true;if(m.corrupt)m.slot[i].payload[0]^=1;return true;}
int main(){
 Record a{},b{}; const char first[]="old-config", second[]="new-config";
 assert(make(a,1,first,sizeof(first)));
 assert(valid(a)); assert(select(&a,nullptr)==&a);
 assert(make(b,2,second,sizeof(second)));
 assert(select(&a,&b)==&b);
 b.payload[0]^=1; assert(!valid(b)); assert(select(&a,&b)==nullptr);
 a.payload[0]^=1; assert(select(&a,&b)==nullptr);
 assert(!make(a,0,first,sizeof(first)));
 assert(!make(a,3,first,kMaxPayload+1));
 Memory m; Backend io{readSlot,writeSlot,&m};
 assert(save(io,first,sizeof(first))==Status::ok);
 assert(m.exists[0] && valid(m.slot[0]));
 m.failWrite=true; assert(save(io,second,sizeof(second))==Status::write_failed);
 assert(m.slot[0].sequence==1);m.failWrite=false;
 assert(save(io,second,sizeof(second))==Status::ok);
 assert(m.slot[1].sequence==2);
 m.failRead=true;assert(save(io,first,sizeof(first))==Status::read_failed);m.failRead=false;
 m.corrupt=true;assert(save(io,first,sizeof(first))==Status::verify_failed);
 assert(save(io,first,sizeof(first))==Status::corrupt);

}'''
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'test.cpp').write_text(source)
   subprocess.run([shutil.which('clang++') or 'c++','-std=c++11','-I',str(ROOT),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
