#include "../main/companion/standalone_policy.h"
using companion_standalone::Policy;
using Decision = Policy::Decision;
namespace {
constexpr bool PauseAndRestore() {
    Policy p; p.Pause(0); p.NetworkCommand(false);
    return p.paused() && p.wifi_wanted() && p.Decide(1, true, true, true) == Decision::Paused &&
        p.Expire(10000000) && !p.paused() && p.Decide(10000000, true, true, true) == Decision::Cloud;
}
constexpr bool CannotRevive() { Policy p; p.Pause(0); return !p.Heartbeat(10000000) && p.Expire(10000000); }
constexpr bool HeartbeatRenewal() { Policy p; p.Pause(0); return p.Heartbeat(9000000) && !p.Expire(10000000) && p.Expire(19000000); }
constexpr bool ExplicitOffline() { Policy p; p.NetworkCommand(false); p.Pause(0); p.NetworkCommand(true); p.Resume(); return !p.wifi_wanted(); }
constexpr bool OfflineOverridesLink() { Policy p; p.NetworkCommand(false); return p.Decide(0, true, true, true) == Decision::Offline; }
constexpr bool Cooldown() {
    Policy p; p.Finished(0, false);
    return p.cooldown_ms(1) == 30000 && p.Decide(29999999, true, true, true) == Decision::Cooldown &&
        p.Decide(30000000, true, true, true) == Decision::Cloud;
}
constexpr bool SuccessClearsCooldown() { Policy p; p.Finished(0, false); p.Finished(1, true); return !p.cooldown_ms(1); }
static_assert(Policy{}.wifi_wanted());
static_assert(Policy{}.Decide(0, true, true, true) == Decision::Cloud);
static_assert(Policy{}.Decide(0, false, true, true) == Decision::Offline);
static_assert(Policy{}.Decide(0, true, false, true) == Decision::Unconfigured);
static_assert(Policy{}.Decide(0, true, true, false) == Decision::RetryWindow);
static_assert(PauseAndRestore());
static_assert(CannotRevive());
static_assert(HeartbeatRenewal());
static_assert(ExplicitOffline());
static_assert(OfflineOverridesLink());
static_assert(Cooldown());
static_assert(SuccessClearsCooldown());
}
