#include "../main/local_companion_cycle.h"

#include <cassert>
#include <cstring>
#include <iostream>

namespace {
using State = LocalCompanionState;

void AssertIdleAccounting(const LocalCompanionCycle& cycle) {
    assert(cycle.state() == State::Idle);
    assert(cycle.started() == cycle.completed() + cycle.aborted());
    assert(cycle.deadline_us() == 0);
}

void AssertFresh(const LocalCompanionCycle& cycle) {
    AssertIdleAccounting(cycle);
    assert(cycle.started() == 0);
    assert(cycle.completed() == 0);
    assert(cycle.aborted() == 0);
}

void AssertBusyWakeRejected(LocalCompanionCycle& cycle, std::int64_t now_us) {
    const auto state = cycle.state();
    const auto deadline = cycle.deadline_us();
    const auto started = cycle.started();
    const auto completed = cycle.completed();
    const auto aborted = cycle.aborted();
    assert(!cycle.BeginWake(now_us));
    assert(cycle.state() == state && cycle.deadline_us() == deadline);
    assert(cycle.started() == started && cycle.completed() == completed && cycle.aborted() == aborted);
}

void EnterListening(LocalCompanionCycle& cycle, std::int64_t start) {
    assert(cycle.BeginWake(start));
    assert(!cycle.Tick(start + LocalCompanionCycle::kWakeDurationUs - 1));
    assert(cycle.Tick(start + LocalCompanionCycle::kWakeDurationUs));
    assert(cycle.state() == State::Listening);
}

void TestCaptureCompletionControlsSpeaking() {
    LocalCompanionCycle cycle;
    AssertFresh(cycle);
    assert(!cycle.Tick(0));
    assert(!cycle.CaptureFinished(0));
    assert(!cycle.Abort());
    assert(cycle.BeginWake(0));
    assert(cycle.state() == State::WakeDetected);
    assert(!cycle.CaptureFinished(50000));
    AssertBusyWakeRejected(cycle, 1);
    assert(!cycle.Tick(99999));
    assert(cycle.Tick(100000));
    assert(cycle.state() == State::Listening);
    assert(cycle.deadline_us() == 4100000);
    AssertBusyWakeRejected(cycle, 100001);
    assert(!cycle.CaptureFinished(99999));  // Backdated completion.
    assert(!cycle.Tick(500000));
    assert(!cycle.Tick(2100000));
    assert(cycle.state() == State::Listening && cycle.completed() == 0);
    assert(cycle.CaptureFinished(2100000));
    assert(cycle.state() == State::Speaking && cycle.deadline_us() == 2900000);
    assert(!cycle.CaptureFinished(2200000));  // Duplicate delivery.
    AssertBusyWakeRejected(cycle, 2200000);
    assert(!cycle.Tick(2899999));
    assert(cycle.Tick(2900000));
    AssertIdleAccounting(cycle);
    assert(cycle.completed() == 1 && cycle.aborted() == 0);
    assert(!cycle.CaptureFinished(3000000));
    assert(!cycle.Tick(999999999));
}

void TestWatchdogBoundaryAndRetry() {
    LocalCompanionCycle cycle;
    EnterListening(cycle, 0);
    assert(!cycle.Tick(4099999));
    assert(!cycle.CaptureFinished(4100000));  // Deadline belongs to timeout.
    assert(!cycle.CaptureFinished(4100001));
    assert(cycle.state() == State::Listening);
    assert(cycle.Tick(4100000));
    AssertIdleAccounting(cycle);
    assert(cycle.completed() == 0 && cycle.aborted() == 1);
    assert(!cycle.Tick(9999999));
    assert(!cycle.Abort());

    EnterListening(cycle, 10000000);
    assert(!cycle.CaptureFinished(10099999));  // Before this listening entry.
    assert(cycle.CaptureFinished(12100000));
    assert(cycle.Tick(12900000));
    AssertIdleAccounting(cycle);
    assert(cycle.started() == 2 && cycle.completed() == 1 && cycle.aborted() == 1);

    LocalCompanionCycle just_before_deadline;
    EnterListening(just_before_deadline, 0);
    assert(just_before_deadline.CaptureFinished(4099999));
    assert(!just_before_deadline.Tick(4899998));
    assert(just_before_deadline.Tick(4899999));
    assert(just_before_deadline.completed() == 1 && just_before_deadline.aborted() == 0);
}

void TestAbortAndLateResults() {
    LocalCompanionCycle cycle;
    EnterListening(cycle, 0);
    assert(cycle.Abort());
    AssertIdleAccounting(cycle);
    assert(cycle.aborted() == 1 && cycle.completed() == 0);
    assert(!cycle.CaptureFinished(2100000));
    assert(!cycle.Abort());

    assert(cycle.BeginWake(3000000));
    assert(cycle.Abort());
    assert(cycle.aborted() == 2);
    EnterListening(cycle, 4000000);
    assert(cycle.CaptureFinished(6100000));
    assert(cycle.Abort());
    AssertIdleAccounting(cycle);
    assert(cycle.started() == 3 && cycle.completed() == 0 && cycle.aborted() == 3);
}

void TestDelayedTicksPreserveEntryAndDwell() {
    LocalCompanionCycle cycle;
    assert(cycle.BeginWake(1000000));
    assert(cycle.Tick(2000000));
    assert(cycle.state() == State::Listening && cycle.deadline_us() == 6000000);
    assert(!cycle.Tick(2000000));
    assert(!cycle.Tick(5999999));
    assert(cycle.CaptureFinished(5999999));
    assert(cycle.deadline_us() == 6799999);
    assert(!cycle.Tick(5999999));
    assert(cycle.Tick(8000000));
    AssertIdleAccounting(cycle);
    assert(cycle.completed() == 1 && cycle.aborted() == 0);

    EnterListening(cycle, 10000000);
    assert(cycle.Tick(99999999));  // Late watchdog aborts; it never speaks.
    AssertIdleAccounting(cycle);
    assert(cycle.started() == 2 && cycle.completed() == 1 && cycle.aborted() == 1);
}

void TestTenCaptureCompletedCycles() {
    LocalCompanionCycle cycle;
    for (std::uint32_t i = 0; i < 10; ++i) {
        const std::int64_t start = static_cast<std::int64_t>(i) * 4000000;
        EnterListening(cycle, start);
        assert(!cycle.Tick(start + 2100000));
        assert(cycle.CaptureFinished(start + 2100000));
        assert(cycle.completed() == i);
        assert(cycle.Tick(start + 2900000));
        AssertIdleAccounting(cycle);
        assert(cycle.started() == i + 1 && cycle.completed() == i + 1 && cycle.aborted() == 0);
    }
}

void TestResetAndNames() {
    LocalCompanionCycle cycle;
    EnterListening(cycle, 0);
    assert(cycle.Abort());
    EnterListening(cycle, 5000000);
    assert(cycle.CaptureFinished(7100000));
    cycle.Reset();
    AssertFresh(cycle);
    assert(!cycle.Tick(7900000));
    assert(!cycle.CaptureFinished(7100000));
    const LocalCompanionCycle fresh;
    AssertFresh(fresh);
    assert(std::strcmp(LocalCompanionCycle::StateName(State::Idle), "IDLE") == 0);
    assert(std::strcmp(LocalCompanionCycle::StateName(State::WakeDetected), "WAKE_DETECTED") == 0);
    assert(std::strcmp(LocalCompanionCycle::StateName(State::Listening), "LISTENING") == 0);
    assert(std::strcmp(LocalCompanionCycle::StateName(State::Speaking), "SPEAKING") == 0);
    assert(std::strcmp(LocalCompanionCycle::StateName(static_cast<State>(99)), "INVALID") == 0);
}
}  // namespace

int main() {
    TestCaptureCompletionControlsSpeaking();
    TestWatchdogBoundaryAndRetry();
    TestAbortAndLateResults();
    TestDelayedTicksPreserveEntryAndDwell();
    TestTenCaptureCompletedCycles();
    TestResetAndNames();
    std::cout << "Local companion capture-cycle tests passed.\n";
}
