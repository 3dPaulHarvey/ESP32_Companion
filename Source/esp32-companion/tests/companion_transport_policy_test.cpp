#include "../main/companion/transport_write_policy.h"
#include <array>
using namespace companion_transport;
namespace {
struct Io {
    std::array<int, 8> returns{};
    std::array<char, 16> sink{};
    unsigned next = 0, accepted = 0, stop_after = 99;
    std::int64_t clock = 0;
    constexpr bool Alive() const { return next < stop_after; }
    constexpr std::int64_t Now() { return clock += 1000; }
    constexpr bool Temporary(int value) const { return value == 0 || value == -2 || value == -3; }
    constexpr void Pause() { clock += 1000; }
    constexpr int Write(const char* data, unsigned length, int) {
        const auto value = returns[next++];
        if (value > 0 && static_cast<unsigned>(value) <= length)
            for (int i = 0; i < value; ++i) sink[accepted++] = data[i];
        return value;
    }
};
constexpr char input[] = "abcdef";
constexpr bool Full() { Io io{{6}}; const auto r = WriteAll(input, 6, 100000, io); return r.ok() && r.calls == 1 && r.written == 6; }
constexpr bool Partial() { Io io{{2, 1, 3}}; const auto r = WriteAll(input, 6, 100000, io); return r.ok() && r.partials == 2 && io.sink[0] == 'a' && io.sink[2] == 'c' && io.sink[5] == 'f' && io.accepted == 6; }
constexpr bool TemporaryRetry(int code) { Io io{{code, 6}}; const auto r = WriteAll(input, 6, 100000, io); return r.ok() && r.calls == 2 && r.waits == 1 && io.accepted == 6; }
constexpr bool Mixed() { Io io{{0, 2, -2, -3, 1, 3}}; const auto r = WriteAll(input, 6, 100000, io); return r.ok() && r.calls == 6 && r.waits == 3 && r.partials == 2 && io.accepted == 6 && io.sink[3] == 'd' && io.sink[5] == 'f'; }
constexpr bool Fatal() { Io io{{-9, 6}}; const auto r = WriteAll(input, 6, 100000, io); return r.status == WriteStatus::Failed && r.calls == 1 && !r.written; }
constexpr bool FatalAfterPartial() { Io io{{2, -9, 4}}; const auto r = WriteAll(input, 6, 100000, io); return r.status == WriteStatus::Failed && r.calls == 2 && r.written == 2 && io.accepted == 2; }
constexpr bool Deadline() { Io io{}; const auto r = WriteAll(input, 6, 3500, io); return r.status == WriteStatus::Timeout && r.calls == 1 && r.waits == 1; }
constexpr bool Stop(bool partial) { Io io{{2, 4}}; io.stop_after = partial ? 1 : 0; const auto r = WriteAll(input, 6, 100000, io); return r.status == WriteStatus::Stopped && r.calls == (partial ? 1U : 0U) && r.written == (partial ? 2U : 0U); }
constexpr bool Oversized() { Io io{{7}}; const auto r = WriteAll(input, 6, 100000, io); return r.status == WriteStatus::Invalid && r.calls == 1 && !r.written; }
constexpr bool Null() { Io io{}; const auto r = WriteAll(nullptr, 6, 100000, io); return r.status == WriteStatus::Invalid && !r.calls; }
constexpr bool Empty() { Io io{}; const auto r = WriteAll(nullptr, 0, 100000, io); return r.ok() && !r.calls; }
constexpr bool Expired() { Io io{{6}}; const auto r = WriteAll(input, 6, 1000, io); return r.status == WriteStatus::Timeout && !r.calls; }
constexpr bool LateCompletion() { Io io{{6}}; const auto r = WriteAll(input, 6, 2000, io); return r.status == WriteStatus::Timeout && r.calls == 1 && r.written == 6; }
static_assert(Full());
static_assert(Partial());
static_assert(TemporaryRetry(0));
static_assert(TemporaryRetry(-2));
static_assert(TemporaryRetry(-3));
static_assert(Mixed());
static_assert(Fatal());
static_assert(FatalAfterPartial());
static_assert(Deadline());
static_assert(Stop(false));
static_assert(Stop(true));
static_assert(Oversized());
static_assert(Null());
static_assert(Empty());
static_assert(Expired());
static_assert(LateCompletion());
}
