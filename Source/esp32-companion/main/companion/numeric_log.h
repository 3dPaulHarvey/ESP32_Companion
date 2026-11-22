#pragma once
#include <array>
#include <cstdint>

namespace companion_log {
// The configured newlib nano printf does not support %lld. Format counters
// explicitly so monotonic microseconds survive beyond 32-bit uptime.
constexpr std::array<char, 21> Decimal(std::uint64_t value) {
    std::array<char, 21> result{};
    char reverse[20]{};
    unsigned count = 0;
    do { reverse[count++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
    for (unsigned i = 0; i < count; ++i) result[i] = reverse[count - i - 1];
    return result;
}
}  // namespace companion_log
