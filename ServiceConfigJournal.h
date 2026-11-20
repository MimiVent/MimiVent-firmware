#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// 实验性完整事务镜像容器。载荷格式、存储后端和资源预算尚未确定，不能接入生产。
namespace ServiceConfigJournal {
constexpr size_t kMaxPayload=1024;
struct Record { uint32_t magic, sequence, length; uint8_t payload[kMaxPayload]; uint32_t crc; };
inline uint32_t checksum(const uint8_t* bytes, size_t length) {
 uint32_t crc=UINT32_MAX;
 for(size_t i=0;i<length;++i){crc^=bytes[i];for(int bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320U & (0U-(crc&1)));}
 return ~crc;
}
inline bool make(Record& out,uint32_t seq,const void* payload,size_t length) {
 if(!seq || !payload || length==0 || length>kMaxPayload) return false;
 memset(&out,0,sizeof(out));out.magic=0x314a564d;out.sequence=seq;out.length=length;
 memcpy(out.payload,payload,length);out.crc=checksum(reinterpret_cast<const uint8_t*>(&out),offsetof(Record,crc));return true;
}
inline bool valid(const Record& r) {
 return r.magic==0x314a564d && r.sequence && r.length && r.length<=kMaxPayload &&
 r.crc==checksum(reinterpret_cast<const uint8_t*>(&r),offsetof(Record,crc));
}
// 已存在但损坏的槽需要人工恢复，不能退回旧授权而复活已撤销凭据。
inline const Record* select(const Record* a,const Record* b) {
 if((a&&!valid(*a))||(b&&!valid(*b))) return nullptr;
 if(!a)return b;if(!b)return a;
 if(a->sequence==b->sequence && memcmp(a,b,sizeof(Record))!=0)return nullptr;
 return a->sequence>=b->sequence?a:b;
}
// 由后端实现读/写和提交；只有完整读回一致才报告成功。
enum class Status { ok, read_failed, corrupt, sequence_exhausted, invalid_payload, write_failed, verify_failed };
struct Backend {
 bool (*read)(void*, unsigned, Record&, bool&);
 bool (*write)(void*, unsigned, const Record&);
 void* context;
};
inline Status save(const Backend& io,const void* payload,size_t length) {
 if (!io.read || !io.write) return Status::read_failed;
 Record a{},b{};bool ae=false,be=false;
 if (!io.read(io.context,0,a,ae) || !io.read(io.context,1,b,be)) return Status::read_failed;
 const Record* current=select(ae?&a:nullptr,be?&b:nullptr);
 if ((ae||be) && !current) return Status::corrupt;
 if (current && current->sequence==UINT32_MAX) return Status::sequence_exhausted;
 const uint32_t seq=current?current->sequence+1:1;
 Record next{};if(!make(next,seq,payload,length))return Status::invalid_payload;
 const unsigned target=current==&a?1:0;
 if(!io.write(io.context,target,next))return Status::write_failed;
 Record check{};bool present=false;
 if(!io.read(io.context,target,check,present)||!present||!valid(check)||memcmp(&check,&next,sizeof(next)))return Status::verify_failed;
 return Status::ok;
}

}
