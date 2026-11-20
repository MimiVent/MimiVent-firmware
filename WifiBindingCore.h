#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>

// P2 单服务授权模型。
// 设备只保存一个有效服务授权和一个短期候选事务；手机权限由 Gateway 管理，
// 设备不再保存每手机 token（取消原 kMaxBindings=3 的多客户端授权表）。
// 旧三槽记录保留在 slot-a/slot-b，不读取也不覆盖，保证回退旧固件时数据仍在。
namespace WifiBindings {
constexpr uint32_t kTtlMs = 300000;          // 候选事务有效期 5 分钟
constexpr uint8_t kRecordVersion = 3;
constexpr size_t kIdLength = 33;             // 32 hex + NUL
constexpr size_t kTransactionLength = 37;    // uuid + NUL
constexpr uint8_t kEntryEmpty = 0;
constexpr uint8_t kEntryActive = 1;
constexpr uint8_t kEntryCandidate = 2;
constexpr uint8_t kStagePending = 0;         // 候选已生成，尚未激活
constexpr uint8_t kStageActivated = 1;       // 已激活，等待 Gateway 只读核验确认

struct ServiceEntry {
  uint8_t used;                     // kEntryEmpty / kEntryActive / kEntryCandidate
  uint8_t stage;                    // 候选阶段；有效授权固定 kStageActivated
  char serviceId[kIdLength];        // Gateway 生成并持久化的服务身份
  char credentialId[kIdLength];     // 当前服务凭据编号
  uint8_t credentialHash[32];       // 服务 token 的 SHA-256
  uint32_t authRevision;            // 配置一致性修订号
  char transactionId[kTransactionLength];  // 仅候选使用
};

struct Record {
  uint32_t magic;
  uint16_t version, bytes;
  uint32_t sequence;
  char deviceId[kIdLength];         // 硬件派生，首次写入后固定
  ServiceEntry active;
  ServiceEntry candidate;
  uint16_t reserved;
  uint32_t checksum;
};
// 必须不超过旧双槽预算（旧 Record 384 B），避免改动 NVS 分区与预算常量。
static_assert(sizeof(Record) <= 384, "service record must fit previous NVS slot budget");
static_assert(offsetof(Record, checksum) == sizeof(Record) - sizeof(uint32_t), "checksum must be last");

inline bool equalHash(const uint8_t* a, const uint8_t* b) {
  uint8_t difference = 0; for (size_t i = 0; i < 32; ++i) difference |= a[i] ^ b[i]; return difference == 0;
}
inline uint32_t crc(const uint8_t* p, size_t length) {
  uint32_t v = UINT32_MAX;
  for (size_t i = 0; i < length; ++i) { v ^= p[i]; for (int b = 0; b < 8; ++b) v = (v >> 1) ^ (0xedb88320U & (0U - (v & 1))); }
  return ~v;
}
inline bool hexId(const char* s) {
  if (!s || strlen(s) != 32) return false;
  for (size_t i = 0; i < 32; ++i) if (!isxdigit((unsigned char)s[i])) return false;
  return true;
}
inline bool uuidText(const char* s) {
  if (!s || strlen(s) != 36) return false;
  for (size_t i = 0; i < 36; ++i) { bool dash = i == 8 || i == 13 || i == 18 || i == 23; if (dash ? s[i] != '-' : !isxdigit((unsigned char)s[i])) return false; }
  return true;
}
inline bool validEntry(const ServiceEntry& e) {
  if (!e.used) return true;
  if (e.used != kEntryActive && e.used != kEntryCandidate) return false;
  if (e.stage != kStagePending && e.stage != kStageActivated) return false;
  if (!hexId(e.serviceId) || !hexId(e.credentialId)) return false;
  if (!memchr(e.serviceId, 0, sizeof(e.serviceId)) || !memchr(e.credentialId, 0, sizeof(e.credentialId))) return false;
  if (e.used == kEntryCandidate) {
    if (!uuidText(e.transactionId) || !memchr(e.transactionId, 0, sizeof(e.transactionId))) return false;
  }
  return true;
}
inline bool valid(const Record& r) {
  if (r.magic != 0x3142564d || r.version != kRecordVersion || r.bytes != sizeof(r) || !r.sequence ||
      r.reserved || r.checksum != crc(reinterpret_cast<const uint8_t*>(&r), offsetof(Record, checksum))) return false;
  if (!memchr(r.deviceId, 0, sizeof(r.deviceId))) return false;
  if (r.deviceId[0] && !hexId(r.deviceId)) return false;
  if (!validEntry(r.active) || !validEntry(r.candidate)) return false;
  if (r.active.used == kEntryCandidate || r.candidate.used == kEntryActive) return false;
  return true;
}
inline void seal(Record& r) {
  r.magic = 0x3142564d; r.version = kRecordVersion; r.bytes = sizeof(r); r.reserved = 0;
  r.checksum = crc(reinterpret_cast<const uint8_t*>(&r), offsetof(Record, checksum));
}

class Core {
 public:
  Record active{};
  char windowOwner[18]{};
  uint32_t windowAt = 0;
  bool window = false;
  using Save = bool (*)(const Record&, void*);

  void tick(uint32_t now) {
    if (window && uint32_t(now - windowAt) >= kTtlMs) window = false;
    if (active.candidate.used && active.candidate.stage == kStagePending &&
        uint32_t(now - windowAt) >= kTtlMs) {
      // 候选过期即清除，避免旧候选在下一次配置时被误用。
      active.candidate = {};
    }
  }
  const char* open(const char* owner, uint32_t now) {
    tick(now);
    if (window && strcmp(windowOwner, owner)) return "BIND_BUSY";
    strncpy(windowOwner, owner, sizeof(windowOwner) - 1); windowAt = now; window = true; return nullptr;
  }
  // 生成候选：设备产生 credentialId 与 token（由调用方生成 hash 后传入 id）。
  const char* begin(const char* owner, const char* transactionId, const char* serviceId,
                    const char* credentialId, const uint8_t* credentialHash,
                    uint32_t authRevision, uint32_t now, Save save, void* context) {
    tick(now);
    if (!window || strcmp(windowOwner, owner)) return "BIND_WINDOW_CLOSED";
    if (!uuidText(transactionId) || !hexId(serviceId) || !hexId(credentialId)) return "INVALID_REQUEST";
    // 只有"待激活"的候选会阻塞新事务；已激活候选保留供 Gateway 核验，不阻塞重新配置。
    if (active.candidate.used && active.candidate.stage == kStagePending &&
        strcmp(active.candidate.transactionId, transactionId)) return "TRANSACTION_BUSY";
    if (active.sequence == UINT32_MAX) return "SEQUENCE_EXHAUSTED";
    Record next = active;
    ServiceEntry& c = next.candidate;
    c = {};
    c.used = kEntryCandidate; c.stage = kStagePending;
    strncpy(c.serviceId, serviceId, sizeof(c.serviceId) - 1);
    strncpy(c.credentialId, credentialId, sizeof(c.credentialId) - 1);
    memcpy(c.credentialHash, credentialHash, 32);
    c.authRevision = authRevision;
    strncpy(c.transactionId, transactionId, sizeof(c.transactionId) - 1);
    ++next.sequence; seal(next);
    if (!save(next, context)) return "BINDINGS_STORE_FAILED";
    active = next; return nullptr;
  }
  // 激活候选：保留旧有效授权，仅将候选标记为待核验；确认前可安全回退。
  const char* activate(const char* owner, const char* transactionId, uint32_t now, Save save, void* context) {
    tick(now);
    if (!active.candidate.used) return "BINDING_INVALID";
    if (strcmp(active.candidate.transactionId, transactionId)) return "TRANSACTION_MISMATCH";
    if (active.candidate.stage == kStageActivated) return nullptr;
    if (active.sequence == UINT32_MAX) return "SEQUENCE_EXHAUSTED";
    Record next = active;
    // 进入待核验阶段时保留旧 active；只有 confirm 才原子替换。
    next.candidate.stage = kStageActivated;
    ++next.sequence; seal(next);
    if (!save(next, context)) return "BINDINGS_STORE_FAILED";
    active = next; window = false; return nullptr;
  }
  const char* cancel(const char* owner, const char* transactionId, Save save, void* context) {
    if (!active.candidate.used) return nullptr;
    if (transactionId && transactionId[0] && strcmp(active.candidate.transactionId, transactionId)) return "TRANSACTION_MISMATCH";
    if (active.sequence == UINT32_MAX) return "SEQUENCE_EXHAUSTED";
    Record next = active; next.candidate = {};
    ++next.sequence; seal(next);
    if (!save(next, context)) return "BINDINGS_STORE_FAILED";
    active = next; window = false; return nullptr;
  }
  // 清除全部服务授权（含候选）。用于换绑失败、重置设备。
  const char* reset(Save save, void* context) {
    if (active.sequence == UINT32_MAX) return "SEQUENCE_EXHAUSTED";
    Record next = active;
    next.active = {}; next.candidate = {};
    ++next.sequence; seal(next);
    if (!save(next, context)) return "BINDINGS_STORE_FAILED";
    active = next; window = false; return nullptr;
  }
  // Gateway 核验成功后确认候选，清除候选记录（避免重复激活）。
  const char* confirm(const char* transactionId, Save save, void* context) {
    if (!active.candidate.used) return nullptr;
    if (strcmp(active.candidate.transactionId, transactionId)) return "TRANSACTION_MISMATCH";
    if (active.candidate.stage != kStageActivated) return "VERIFICATION_REQUIRED";
    if (active.sequence == UINT32_MAX) return "SEQUENCE_EXHAUSTED";
    Record next = active;
    next.active = next.candidate;
    next.active.used = kEntryActive;
    next.active.stage = kStageActivated;
    next.active.transactionId[0] = 0;
    next.candidate = {};
    ++next.sequence; seal(next);
    if (!save(next, context)) return "BINDINGS_STORE_FAILED";
    active = next; return nullptr;
  }
  // 重启后丢弃未激活候选；已激活候选保留，供 Gateway 核验后继续或回退。
  bool recoverAfterRestart(Save save, void* context) {
    window = false;
    windowAt = 0;
    windowOwner[0] = 0;
    if (!active.candidate.used || active.candidate.stage == kStageActivated) return true;
    if (active.sequence == UINT32_MAX) return false;
    Record next = active;
    next.candidate = {};
    ++next.sequence;
    seal(next);
    if (!save || !save(next, context)) return false;
    active = next;
    return true;
  }
};
}
