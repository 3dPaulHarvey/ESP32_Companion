#include "../main/companion/wake_question_policy.h"
using companion_wake::Arm;
namespace {
constexpr bool OneUse() {
    Arm arm;
    return !arm.waiting() && !arm.Consume(0) && arm.ArmOnce(0) && arm.Consume(1) &&
        !arm.waiting() && !arm.Consume(2) && arm.ArmOnce(3);
}
constexpr bool CannotExtendWindow() {
    Arm arm; arm.ArmOnce(0);
    for (int i = 1; i <= 11; ++i) if (!arm.Heartbeat(i * 5000000LL)) return false;
    return arm.Expire(59999999) == Arm::Expiry::None &&
        arm.Expire(60000000) == Arm::Expiry::Window && !arm.Consume(60000000);
}
constexpr bool CannotReviveLease() {
    Arm arm; arm.ArmOnce(0);
    return !arm.Heartbeat(10000000) && !arm.waiting() && !arm.Consume(10000001);
}
constexpr bool DuplicateArmDoesNotResetTimeout() {
    Arm arm; arm.ArmOnce(0);
    return !arm.ArmOnce(9000000) && arm.Expire(10000000) == Arm::Expiry::Lease;
}
constexpr bool CancelStopsUploadPermission() {
    Arm arm; arm.ArmOnce(0); arm.Disarm();
    return !arm.Heartbeat(1) && !arm.Consume(2) && arm.Expire(3) == Arm::Expiry::None;
}
constexpr bool HeartbeatKeepsWindowAlive() {
    Arm arm; arm.ArmOnce(0);
    return arm.Heartbeat(9000000) && arm.Expire(10000000) == Arm::Expiry::None && arm.Consume(18000000);
}
static_assert(OneUse());
static_assert(CannotExtendWindow());
static_assert(CannotReviveLease());
static_assert(DuplicateArmDoesNotResetTimeout());
static_assert(CancelStopsUploadPermission());
static_assert(HeartbeatKeepsWindowAlive());
}
