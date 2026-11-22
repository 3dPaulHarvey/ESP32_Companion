#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace companion_question {
inline constexpr std::size_t kSamples = 64000, kChunkSamples = 640;
inline constexpr unsigned kChunkMs = 40;
inline constexpr char kPrefix[] = "{\"realtimeInput\":{\"audio\":{\"mimeType\":\"audio/pcm;rate=16000\",\"data\":\"";
inline constexpr char kSuffix[] = "\"}}}";
inline constexpr char kEnd[] = "{\"realtimeInput\":{\"audioStreamEnd\":true}}";
inline constexpr std::size_t kFrameCapacity = sizeof(kPrefix) + ((kChunkSamples * 2 + 2) / 3) * 4 + sizeof(kSuffix);
inline constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Encode explicit little-endian int16 bytes, independent of host endianness.
constexpr std::size_t EncodeFrame(const std::int16_t* samples, std::size_t count, char* out, std::size_t capacity) {
    if (!samples || !out || !count || count > kChunkSamples) return 0;
    const std::size_t bytes = count * 2, encoded = ((bytes + 2) / 3) * 4;
    const std::size_t size = sizeof(kPrefix) - 1 + encoded + sizeof(kSuffix) - 1;
    if (capacity <= size) return 0;
    std::size_t used = 0;
    for (char c : std::string_view(kPrefix)) out[used++] = c;
    auto byte = [&](std::size_t i) { return static_cast<unsigned>((static_cast<std::uint16_t>(samples[i / 2]) >> ((i % 2) * 8)) & 255); };
    for (std::size_t i = 0; i < bytes; i += 3) {
        const unsigned a = byte(i), b = i + 1 < bytes ? byte(i + 1) : 0, c = i + 2 < bytes ? byte(i + 2) : 0;
        out[used++] = kAlphabet[a >> 2];
        out[used++] = kAlphabet[((a & 3) << 4) | (b >> 4)];
        out[used++] = i + 1 < bytes ? kAlphabet[((b & 15) << 2) | (c >> 6)] : '=';
        out[used++] = i + 2 < bytes ? kAlphabet[c & 63] : '=';
    }
    for (char c : std::string_view(kSuffix)) out[used++] = c;
    out[used] = 0;
    return used;
}
}  // namespace companion_question
