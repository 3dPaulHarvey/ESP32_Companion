#pragma once

#include "bounded_json.h"
#include <cstddef>

namespace companion_voice {
inline constexpr std::size_t kMessageLimit = 65536;
inline constexpr std::size_t kAudioLimit = 24000 * 2 * 10;
inline constexpr char kGreeting[] = "{\"clientContent\":{\"turns\":[{\"role\":\"user\",\"parts\":[{\"text\":\"In one short sentence, say hello and tell me you are ready to chat.\"}]}],\"turnComplete\":true}}";

constexpr int Base64Digit(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    return c == '+' ? 62 : c == '/' ? 63 : -1;
}
constexpr bool DecodePcm(std::string_view encoded, std::uint8_t* target, std::size_t capacity, std::size_t& written) {
    written = 0;
    if (!target || encoded.empty() || encoded.size() % 4) return false;
    const auto padding = (encoded.back() == '=' ? 1 : 0) + (encoded[encoded.size() - 2] == '=' ? 1 : 0);
    const auto decoded = encoded.size() / 4 * 3 - padding;
    if (!decoded || decoded % 2 || decoded > capacity) return false;
    for (std::size_t i = 0; i < encoded.size(); i += 4) {
        const bool last = i + 4 == encoded.size();
        const int a = Base64Digit(encoded[i]), b = Base64Digit(encoded[i + 1]);
        const int c = encoded[i + 2] == '=' && last ? 0 : Base64Digit(encoded[i + 2]);
        const int d = encoded[i + 3] == '=' && last ? 0 : Base64Digit(encoded[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0 ||
            (encoded[i + 2] == '=' && encoded[i + 3] != '=') ||
            (last && ((padding == 2 && (b & 15)) || (padding == 1 && (c & 3))))) return false;
    }
    for (std::size_t i = 0; i < encoded.size(); i += 4) {
        const unsigned a = Base64Digit(encoded[i]), b = Base64Digit(encoded[i + 1]);
        const unsigned c = encoded[i + 2] == '=' ? 0 : Base64Digit(encoded[i + 2]);
        const unsigned d = encoded[i + 3] == '=' ? 0 : Base64Digit(encoded[i + 3]);
        target[written++] = (a << 2) | (b >> 4);
        if (written < decoded) target[written++] = (b << 4) | (c >> 2);
        if (written < decoded) target[written++] = (c << 6) | d;
    }
    return written == decoded;
}

struct AudioState {
    std::size_t bytes = 0;
    unsigned chunks = 0, peak = 0, clipped = 0;
    std::uint64_t squares = 0;
    bool turn_complete = false;
};

constexpr bool Consume(const JsonDocument& json, std::uint8_t* pcm, std::size_t capacity,
                       AudioState& state, const char*& reason) {
    using Type = JsonDocument::Type;
    constexpr auto missing = JsonDocument::kMissing;
    if (!pcm) { reason = "allocation_failed"; return false; }
    if (state.turn_complete) { reason = "content_after_turn"; return false; }
    for (std::uint16_t key = 1; key < json[0].next; key = json[key + 1].next) {
        if (json[key].value != "serverContent" && json[key].value != "usageMetadata" && json[key].value != "sessionResumptionUpdate") {
            reason = "unexpected_server_message"; return false;
        }
        if (json[key + 1].type != Type::Object) { reason = "invalid_server_message"; return false; }
    }
    const auto content = json.Find(0, "serverContent");
    // Gemini may send a resumption-state notice even in a one-turn session.
    // Its bounded object is ignored; no handle is retained or sent back.
    if (content == missing) return true;
    for (const auto name : {"interrupted", "turnComplete", "generationComplete"}) {
        const auto flag = json.Find(content, name);
        if (flag != missing && json[flag].type != Type::True && json[flag].type != Type::False) {
            reason = "invalid_turn_flag"; return false;
        }
    }
    const auto interrupted = json.Find(content, "interrupted");
    if (interrupted != missing && json[interrupted].type == Type::True) { reason = "turn_interrupted"; return false; }
    if (json.Find(content, "inputTranscription") != missing) { reason = "unexpected_input_transcription"; return false; }
    const auto model = json.Find(content, "modelTurn");
    if (model != missing) {
        if (json[model].type != Type::Object) { reason = "invalid_model_turn"; return false; }
        const auto role = json.Find(model, "role");
        if (role != missing && (json[role].type != Type::String || json[role].value != "model" || json[role].escaped)) {
            reason = "invalid_model_role"; return false;
        }
        const auto parts = json.Find(model, "parts");
        if (parts == missing || json[parts].type != Type::Array) { reason = "invalid_parts"; return false; }
        for (auto part = static_cast<std::uint16_t>(parts + 1); part < json[parts].next; part = json[part].next) {
            if (json[part].type != Type::Object || json.Find(part, "functionCall") != missing) { reason = "unexpected_part"; return false; }
            const auto audio = json.Find(part, "inlineData");
            if (audio == missing) continue;
            const auto mime = json.Find(audio, "mimeType"), data = json.Find(audio, "data");
            if (mime == missing || data == missing || json[mime].type != Type::String || json[mime].escaped ||
                json[mime].value != "audio/pcm;rate=24000" || json[data].type != Type::String || json[data].escaped) {
                reason = "invalid_audio_format"; return false;
            }
            if (state.bytes > capacity || state.bytes > kAudioLimit) { reason = "audio_limit"; return false; }
            std::size_t decoded = 0;
            if (!DecodePcm(json[data].value, pcm + state.bytes,
                    capacity - state.bytes < kAudioLimit - state.bytes ? capacity - state.bytes : kAudioLimit - state.bytes, decoded)) {
                reason = "invalid_or_excessive_pcm"; return false;
            }
            for (std::size_t i = state.bytes; i < state.bytes + decoded; i += 2) {
                const unsigned raw = pcm[i] | (static_cast<unsigned>(pcm[i + 1]) << 8);
                const int sample = raw < 32768 ? static_cast<int>(raw) : static_cast<int>(raw) - 65536;
                const auto magnitude = static_cast<unsigned>(sample < 0 ? -sample : sample);
                if (magnitude > state.peak) state.peak = magnitude;
                if (sample == -32768 || sample == 32767) ++state.clipped;
                state.squares += static_cast<std::uint64_t>(static_cast<std::int64_t>(sample) * sample);
            }
            state.bytes += decoded; ++state.chunks;
        }
    }
    const auto complete = json.Find(content, "turnComplete");
    if (complete != missing && json[complete].type == Type::True) {
        if (!state.bytes) { reason = "empty_audio_turn"; return false; }
        state.turn_complete = true;
    }
    return true;
}
}  // namespace companion_voice
