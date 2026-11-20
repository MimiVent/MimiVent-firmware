#pragma once
#include <stdint.h>

// 事务转换规则。调用方必须先持久化目标状态并确认成功，才能提交内存状态。
// 此核心不负责凭据存储、网络切换或授权校验。
namespace ServiceTransaction {
enum class Phase : uint8_t { empty, candidate, activated, recovery_required, confirmed };
enum class Result : uint8_t { ok, busy, invalid, storage_failed, recovery_required, already_confirmed, verification_required };
struct State { uint64_t id = 0; Phase phase = Phase::empty; bool verified = false; };
inline Result begin(State& s, uint64_t id) {
 if (!id) return Result::invalid;
 if (s.phase == Phase::confirmed && s.id != id) s = State{};
 if (s.phase != Phase::empty) return s.id == id && s.phase == Phase::candidate ? Result::ok : Result::busy;
 s.id = id; s.phase = Phase::candidate; return Result::ok;
}
inline Result activate(State& s, uint64_t id, bool persisted) {
 if (s.id != id) return Result::invalid;
 if (s.phase == Phase::activated || s.phase == Phase::recovery_required) return Result::ok;
 if (s.phase != Phase::candidate) return Result::invalid;
 if (!persisted) return Result::storage_failed;
 s.phase = Phase::activated; return Result::ok;
}
inline Phase recoverAfterRestart(State& s) {
 if (s.phase == Phase::candidate) s = State{};
 else if (s.phase == Phase::activated) s.phase = Phase::recovery_required;
 s.verified = false;
 return s.phase;
}
inline Result cancel(State& s, uint64_t id) {
 if (s.id != id) return Result::invalid;
 if (s.phase == Phase::confirmed) return Result::already_confirmed;
 if (s.phase != Phase::candidate) return Result::recovery_required;
 s = State{}; return Result::ok;
}
// 仅在 Gateway 查询返回与本次事务匹配的授权信息后调用。核验标记不跨重启沿用。
inline Result markVerified(State& s, uint64_t id) {
 if (s.id != id || (s.phase != Phase::activated && s.phase != Phase::recovery_required)) return Result::invalid;
 s.verified = true; return Result::ok;
}
inline Result confirm(State& s, uint64_t id, bool persisted) {
 if (s.id != id) return Result::invalid;
 if (s.phase == Phase::confirmed) return Result::ok;
 if (s.phase != Phase::activated && s.phase != Phase::recovery_required) return Result::invalid;
 if (!s.verified) return Result::verification_required;
 if (!persisted) return Result::storage_failed;
 s.phase = Phase::confirmed; return Result::ok;
}
// 存储回调必须在成功返回前完成写入并读回确认；回调失败时不改变当前状态。
// verified 是会话内证据，不写入存储，设备重启后必须重新核验。
using Save = bool (*)(const State&, void*);
inline Result commitStored(State& current, const State& next, Save save, void* context) {
 if (!save) return Result::storage_failed;
 State persisted = next; persisted.verified = false;
 if (!save(persisted, context)) return Result::storage_failed;
 current = next; return Result::ok;
}
inline Result beginStored(State& s, uint64_t id, Save save, void* context) {
 State next=s; Result r=begin(next,id); if(r!=Result::ok) return r;
 if (next.id==s.id && next.phase==s.phase) return Result::ok;
 return commitStored(s,next,save,context);
}
inline Result activateStored(State& s, uint64_t id, Save save, void* context) {
 State next=s; Result r=activate(next,id,true); if(r!=Result::ok) return r;
 if (next.phase==s.phase) return Result::ok;
 return commitStored(s,next,save,context);
}
inline Result cancelStored(State& s, uint64_t id, Save save, void* context) {
 State next=s; Result r=cancel(next,id); if(r!=Result::ok) return r;
 if (next.id==s.id && next.phase==s.phase) return Result::ok;
 return commitStored(s,next,save,context);
}
inline Result confirmStored(State& s, uint64_t id, Save save, void* context) {
 State next=s; Result r=confirm(next,id,true); if(r!=Result::ok) return r;
 if (next.phase==s.phase) return Result::ok;
 return commitStored(s,next,save,context);
}

}
