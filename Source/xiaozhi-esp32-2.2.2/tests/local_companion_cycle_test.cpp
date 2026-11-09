#include "../main/local_companion_cycle.h"

#include <cassert>
#include <cstring>
#include <iostream>

namespace {
using State = LocalCompanionState;

void AssertFresh(const LocalCompanionCycle& cycle) {
    assert(cycle.state() == State::Idle);
    assert(cycle.started() == 0);
    assert(cycle.completed() == 0);
    assert(cycle.deadline_us() == 0);
}

void AssertBusyWakeRejected(LocalCompanionCycle& cycle, std::int64_t now_us) {
    const auto old_state = cycle.state();
    const auto old_deadline = cycle.deadline_us();
    const auto old_started = cycle.started();
    const auto old_completed = cycle.completed();
    assert(!cycle.BeginWake(now_us));
    assert(cycle.state() == old_state);
    assert(cycle.deadline_us() == old_deadline);
    assert(cycle.started() == old_started);
    assert(cycle.completed() == old_completed);
}

void TestExactBoundariesAndBusyWakes() {
    LocalCompanionCycle cycle;
    AssertFresh(cycle);
    assert(!cycle.Tick(0));
    assert(cycle.BeginWake(0));
    assert(cycle.state() == State::WakeDetected);
    assert(cycle.started() == 1);
    assert(cycle.completed() == 0);
    AssertBusyWakeRejected(cycle, 1);
    assert(!cycle.Tick(99999));
    assert(cycle.Tick(100000));
    assert(cycle.state() == State::Listening);
    AssertBusyWakeRejected(cycle, 100001);
    assert(!cycle.Tick(499999));
    assert(cycle.Tick(500000));
    assert(cycle.state() == State::Speaking);
    assert(cycle.completed() == 0);
    AssertBusyWakeRejected(cycle, 500001);
    assert(!cycle.Tick(1299999));
    assert(cycle.Tick(1300000));
    assert(cycle.state() == State::Idle);
    assert(cycle.started() == 1);
    assert(cycle.completed() == 1);
    assert(!cycle.Tick(999999999));
    assert(cycle.completed() == 1);
}

void TestDelayedTicksPreserveEveryState() {
    LocalCompanionCycle cycle;
    assert(cycle.BeginWake(1000000));
    assert(cycle.Tick(2000000));
    assert(cycle.state() == State::Listening);
    assert(cycle.deadline_us() == 2400000);
    assert(!cycle.Tick(2000000));
    assert(!cycle.Tick(2399999));
    assert(cycle.Tick(3000000));
    assert(cycle.state() == State::Speaking);
    assert(cycle.deadline_us() == 3800000);
    assert(!cycle.Tick(3000000));
    assert(!cycle.Tick(3799999));
    assert(cycle.Tick(3800000));
    assert(cycle.state() == State::Idle);
    assert(cycle.completed() == 1);
}

void TestTenCompleteCycles() {
    LocalCompanionCycle cycle;
    for (std::uint32_t i = 0; i < 10; ++i) {
        const std::int64_t start = static_cast<std::int64_t>(i) * 2000000;
        assert(cycle.BeginWake(start));
        assert(cycle.Tick(start + 100000));
        assert(cycle.state() == State::Listening);
        assert(cycle.Tick(start + 500000));
        assert(cycle.state() == State::Speaking);
        assert(cycle.completed() == i);
        assert(cycle.Tick(start + 1300000));
        assert(cycle.state() == State::Idle);
        assert(cycle.started() == i + 1);
        assert(cycle.completed() == i + 1);
    }
}

void TestResetAndFreshConstructor() {
    LocalCompanionCycle cycle;
    assert(cycle.BeginWake(20));
    assert(cycle.Tick(100020));
    assert(cycle.Tick(500020));
    cycle.Reset();
    AssertFresh(cycle);
    assert(!cycle.Tick(1300020));
    assert(cycle.BeginWake(0));
    assert(cycle.started() == 1);
    assert(cycle.completed() == 0);
    const LocalCompanionCycle fresh;
    AssertFresh(fresh);
}

void TestNames() {
    assert(std::strcmp(LocalCompanionCycle::StateName(State::Idle), "IDLE") == 0);
    assert(std::strcmp(LocalCompanionCycle::StateName(State::WakeDetected), "WAKE_DETECTED") == 0);
    assert(std::strcmp(LocalCompanionCycle::StateName(State::Listening), "LISTENING") == 0);
    assert(std::strcmp(LocalCompanionCycle::StateName(State::Speaking), "SPEAKING") == 0);
    assert(std::strcmp(LocalCompanionCycle::StateName(static_cast<State>(99)), "INVALID") == 0);
}
}  // namespace

int main() {
    TestExactBoundariesAndBusyWakes();
    TestDelayedTicksPreserveEveryState();
    TestTenCompleteCycles();
    TestResetAndFreshConstructor();
    TestNames();
    std::cout << "Local companion cycle tests passed.\n";
}
