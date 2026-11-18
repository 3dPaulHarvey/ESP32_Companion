#pragma once

#include "session_policy.h"

namespace companion_gemini {
inline constexpr std::size_t kMessageLimit = 16384;
inline constexpr std::string_view kPathPrefix = "/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContentConstrained?access_token=";
inline constexpr std::size_t kPathCapacity = kPathPrefix.size() + 3 * 8192 + 1;

constexpr bool Unreserved(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
}

constexpr bool EncodePath(std::string_view temporary, char* target, std::size_t capacity, std::size_t& used) {
    used = 0;
    if (!target || temporary.size() < 16 || temporary.size() > 8192 || capacity <= kPathPrefix.size()) return false;
    for (char c : temporary) if (c < 0x21 || c > 0x7e) return false;
    std::size_t required = kPathPrefix.size();
    for (unsigned char c : temporary) required += Unreserved(c) ? 1 : 3;
    if (required >= capacity) return false;
    for (char c : kPathPrefix) target[used++] = c;
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char c : temporary) {
        if (Unreserved(c)) target[used++] = static_cast<char>(c);
        else { target[used++] = '%'; target[used++] = hex[c >> 4]; target[used++] = hex[c & 15]; }
    }
    target[used] = 0;
    return true;
}

constexpr bool IsSetupComplete(std::string_view message) {
    companion_session::FlatJson json(message);
    std::string_view field;
    return json.Take('{') && json.String(field) && field == "setupComplete" && json.Take(':') &&
        json.Take('{') && json.Take('}') && json.Take('}') && json.End();
}

enum class PieceResult { Pending, Message, Control, Invalid };

// Tracks WebSocket frame boundaries separately from transport read chunks.
// Control frames may appear between fragmented data frames without losing data.
class FrameTracker {
public:
    constexpr bool Begin(unsigned opcode, bool fin, std::size_t length) {
        if (active_ || message_ready_) return false;
        const bool control = opcode == 8 || opcode == 9 || opcode == 10;
        if (control) {
            if (!fin || length > 125 || (opcode == 8 && length == 1)) return false;
            control_size_ = 0;
        } else {
            if (opcode == 0) { if (!fragmented_) return false; }
            else if (opcode == 1 || opcode == 2) { if (fragmented_) return false; }
            else return false;
            if (length > kMessageLimit - message_size_) return false;
        }
        active_ = true; control_ = control; opcode_ = opcode; fin_ = fin; left_ = length;
        return true;
    }
    constexpr PieceResult Append(std::size_t count) {
        if (!active_ || count > left_) return PieceResult::Invalid;
        left_ -= count;
        if (control_) control_size_ += count;
        else message_size_ += count;
        if (left_) return PieceResult::Pending;
        active_ = false;
        if (control_) return PieceResult::Control;
        fragmented_ = !fin_;
        if (!fin_) return PieceResult::Pending;
        message_ready_ = true;
        return PieceResult::Message;
    }
    constexpr bool active() const { return active_; }
    constexpr bool control() const { return control_; }
    constexpr unsigned opcode() const { return opcode_; }
    constexpr std::size_t message_size() const { return message_size_; }
    constexpr std::size_t control_size() const { return control_size_; }
    constexpr void ResetMessage() { message_size_ = 0; message_ready_ = false; fragmented_ = false; }
private:
    std::size_t left_ = 0, message_size_ = 0, control_size_ = 0;
    unsigned opcode_ = 0;
    bool active_ = false, control_ = false, fin_ = false, fragmented_ = false, message_ready_ = false;
};
}  // namespace companion_gemini
