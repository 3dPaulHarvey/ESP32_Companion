#pragma once
#include <cstddef>
#include <cstdint>

namespace companion_transport {
enum class WriteStatus { Complete, Timeout, Stopped, Failed, Invalid };
struct WriteResult {
    WriteStatus status = WriteStatus::Complete;
    unsigned written = 0, calls = 0, waits = 0, partials = 0;
    int last_return = 0;
    constexpr bool ok() const { return status == WriteStatus::Complete; }
};
// Continue the SAME TLS byte sequence. Never resend a WebSocket frame/header,
// advance on an unwritten byte, or extend the original absolute deadline.
template <class Io>
constexpr WriteResult WriteAll(const char* data, unsigned length, std::int64_t deadline, Io& io) {
    WriteResult result;
    if (!data && length) { result.status = WriteStatus::Invalid; return result; }
    while (result.written < length) {
        if (!io.Alive()) { result.status = WriteStatus::Stopped; return result; }
        const auto now = io.Now();
        if (now >= deadline) { result.status = WriteStatus::Timeout; return result; }
        const unsigned remaining = length - result.written;
        const auto milliseconds = (deadline - now + 999) / 1000;
        const int count = io.Write(data + result.written, remaining, static_cast<int>(milliseconds));
        ++result.calls; result.last_return = count;
        if (count > 0) {
            if (static_cast<unsigned>(count) > remaining) { result.status = WriteStatus::Invalid; return result; }
            if (static_cast<unsigned>(count) < remaining) ++result.partials;
            result.written += count;
        } else if (!io.Temporary(count)) { result.status = WriteStatus::Failed; return result; }
        if (io.Now() >= deadline) { result.status = WriteStatus::Timeout; return result; }
        if (count <= 0) { ++result.waits; io.Pause(); }
    }
    return result;
}
constexpr const char* WriteStatusName(WriteStatus status) {
    switch (status) {
        case WriteStatus::Complete: return "complete";
        case WriteStatus::Timeout: return "timeout";
        case WriteStatus::Stopped: return "stopped";
        case WriteStatus::Failed: return "fatal";
        case WriteStatus::Invalid: return "invalid";
    }
    return "invalid";
}
}
