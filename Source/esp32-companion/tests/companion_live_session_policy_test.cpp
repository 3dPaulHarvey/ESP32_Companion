#include "../main/companion/live_session_policy.h"

namespace {
using namespace companion_live_session;
constexpr std::int64_t kStart = 1000000;

constexpr bool IdleStartsAfterReply() {
    Policy policy(kStart);
    if (policy.status(kStart) != Status::Ready || !policy.BeginTurn(kStart + 1)) return false;
    if (policy.BeginTurn(kStart + 2) || policy.CompleteTurn(kStart)) return false;
    if (policy.status(kStart + 40000000) != Status::TurnActive || !policy.CompleteTurn(kStart + 40000000)) return false;
    if (policy.CompleteTurn(kStart + 40000001) || policy.turns() != 1) return false;
    return policy.idle_deadline_us() == kStart + 70000000 &&
        policy.status(kStart + 69999999) == Status::Ready && policy.remaining_us(kStart + 69999999) == 1 &&
        policy.status(kStart + 70000000) == Status::IdleExpired && !policy.BeginTurn(kStart + 70000000);
}
constexpr bool WarmWakeUsesSameBound() {
    Policy policy(kStart);
    if (!policy.BeginTurn(kStart) || !policy.CompleteTurn(kStart + 10000000)) return false;
    if (!policy.BeginTurn(kStart + 39000000)) return false;
    return policy.idle_deadline_us() == 0 && policy.turn_active() && policy.turns() == 1 &&
        policy.remaining_us(kStart + 39000000) == 81000000;
}
constexpr bool LifetimeNeverExtends() {
    Policy policy(kStart);
    if (!policy.BeginTurn(kStart) || !policy.CompleteTurn(kStart + 110000000)) return false;
    if (policy.idle_deadline_us() != kStart + kSessionLimitUs) return false;
    if (!policy.BeginTurn(kStart + kSessionLimitUs - 1)) return false;
    return policy.status(kStart + kSessionLimitUs) == Status::SessionExpired &&
        !policy.CompleteTurn(kStart + kSessionLimitUs) && policy.remaining_us(kStart + kSessionLimitUs) == 0;
}
constexpr bool TurnLimitIsEnforced() {
    Policy policy(kStart);
    for (unsigned i = 0; i < kMaxTurns; ++i) {
        if (!policy.BeginTurn(kStart + i * 1000000) || !policy.CompleteTurn(kStart + i * 1000000 + 1)) return false;
    }
    return policy.turns() == kMaxTurns && policy.status(kStart + kMaxTurns * 1000000) == Status::TurnLimit &&
        !policy.BeginTurn(kStart + kMaxTurns * 1000000) && policy.remaining_us(kStart + kMaxTurns * 1000000) == 0;
}
constexpr bool InvalidClockCannotAdmit() {
    Policy invalid(-1), policy(kStart);
    if (invalid.BeginTurn(0) || policy.BeginTurn(kStart - 1) || policy.CompleteTurn(kStart)) return false;
    if (!policy.BeginTurn(kStart + 100) || policy.CompleteTurn(kStart + 99)) return false;
    return policy.status(kStart + 99) == Status::InvalidClock && !policy.BeginTurn(kStart + 99);
}
static_assert(kMaxTurns == 8 && kIdleWindowUs == 30000000 && kSessionLimitUs == 120000000);
static_assert(Policy(kStart).remaining_us(kStart) == kSessionLimitUs);
static_assert(Policy(kStart).status(kStart + kSessionLimitUs - 1) == Status::Ready);
static_assert(Policy(kStart).status(kStart + kSessionLimitUs) == Status::SessionExpired);
static_assert(IdleStartsAfterReply());
static_assert(WarmWakeUsesSameBound());
static_assert(LifetimeNeverExtends());
static_assert(TurnLimitIsEnforced());
static_assert(InvalidClockCannotAdmit());
}  // namespace
