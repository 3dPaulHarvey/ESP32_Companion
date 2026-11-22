#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace companion_session {
inline constexpr std::string_view kModel = "models/gemini-3.8-live";
inline constexpr std::string_view kWebSocket = "wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContentConstrained";
inline constexpr std::size_t kBodyLimit = 16384;

// A successful network time sync remains useful across nearby conversations.
// Bound its age by monotonic time and reject wall-clock adjustments before
// reusing it; certificate and temporary-token date validation still run.
class RecentTimeSync {
public:
    static constexpr std::int64_t kMinimumUnixUs = 1735689600000000LL;
    static constexpr std::int64_t kMaximumAgeUs = 600000000;
    static constexpr std::int64_t kMaximumDriftUs = 2000000;

    constexpr bool Complete(bool synchronized, std::int64_t monotonic_us, std::int64_t unix_us) {
        valid_ = synchronized && monotonic_us >= 0 && unix_us >= kMinimumUnixUs;
        if (valid_) { monotonic_us_ = monotonic_us; unix_us_ = unix_us; }
        return valid_;
    }
    constexpr std::int64_t AgeUs(std::int64_t monotonic_us) const {
        return valid_ && monotonic_us >= monotonic_us_ ? monotonic_us - monotonic_us_ : -1;
    }
    constexpr bool CanReuse(std::int64_t monotonic_us, std::int64_t unix_us) const {
        const auto age = AgeUs(monotonic_us);
        if (age < 0 || age >= kMaximumAgeUs || unix_us < kMinimumUnixUs) return false;
        const auto wall_elapsed = unix_us - unix_us_;
        return wall_elapsed >= age - kMaximumDriftUs && wall_elapsed <= age + kMaximumDriftUs;
    }
private:
    bool valid_ = false;
    std::int64_t monotonic_us_ = 0, unix_us_ = 0;
};

constexpr bool ValidCredential(std::string_view value) {
    if (value.size() != 64) return false;
    for (char c : value) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

// Accept the exact UTC millisecond format emitted by the Worker. Do not use
// local timezone state or normalize invalid dates into a different month.
constexpr std::int64_t TimestampMs(std::string_view s) {
    if (s.size() != 24 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' ||
        s[16] != ':' || s[19] != '.' || s[23] != 'Z') return -1;
    int fields[7]{};
    constexpr int starts[] = {0, 5, 8, 11, 14, 17, 20};
    constexpr int lengths[] = {4, 2, 2, 2, 2, 2, 3};
    for (int i = 0; i < 7; ++i) for (int j = 0; j < lengths[i]; ++j) {
        const char c = s[starts[i] + j];
        if (c < '0' || c > '9') return -1;
        fields[i] = fields[i] * 10 + c - '0';
    }
    int year = fields[0];
    const int month = fields[1], day = fields[2];
    if (year < 2025 || year > 2100 || month < 1 || month > 12 || day < 1 ||
        fields[3] > 23 || fields[4] > 59 || fields[5] > 59) return -1;
    constexpr int month_days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (day > month_days[month - 1] + (month == 2 && leap ? 1 : 0)) return -1;
    year -= month <= 2;
    const int era = year / 400;
    const int yoe = year - era * 400;
    const int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const std::int64_t days = era * 146097LL + doe - 719468;
    return (((days * 24 + fields[3]) * 60 + fields[4]) * 60 + fields[5]) * 1000 + fields[6];
}

struct SessionMetadata {
    std::size_t temporary_bytes = 0;
    int start_in_seconds = 0;
    int expires_in_seconds = 0;
};

struct SessionDiagnostics {
    const char* reason = "unavailable";
    std::int64_t start_delta_ms = 0, expiry_delta_ms = 0;
    std::size_t temporary_bytes = 0;
};

// The Worker owns a flat five-string response. Reject unknown/duplicate fields,
// nesting, escaped/control/non-ASCII strings and trailing data. Values are
// borrowed only during validation; no temporary credential is copied or kept.
class FlatJson {
public:
    constexpr explicit FlatJson(std::string_view text) : text_(text) {}
    constexpr bool Take(char c) {
        Space();
        if (at_ >= text_.size() || text_[at_] != c) return false;
        ++at_;
        return true;
    }
    constexpr bool String(std::string_view& value) {
        if (!Take('"')) return false;
        const auto start = at_;
        while (at_ < text_.size() && text_[at_] != '"') {
            const auto c = static_cast<unsigned char>(text_[at_++]);
            if (c < 0x20 || c > 0x7e || c == '\\') return false;
        }
        if (at_ == text_.size()) return false;
        value = text_.substr(start, at_++ - start);
        return true;
    }
    constexpr bool End() { Space(); return at_ == text_.size(); }
private:
    constexpr void Space() {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\n' || text_[at_] == '\r' || text_[at_] == '\t')) ++at_;
    }
    std::string_view text_;
    std::size_t at_ = 0;
};

constexpr bool ValidateSession(std::string_view body, std::int64_t now_ms,
                               std::string_view device_credential, SessionMetadata& result,
                               std::string_view* borrowed_temporary = nullptr,
                               SessionDiagnostics* diagnostics = nullptr) {
    result = {};
    if (diagnostics) *diagnostics = {};
    auto reject = [&](const char* reason) { if (diagnostics) diagnostics->reason = reason; return false; };
    if (borrowed_temporary) *borrowed_temporary = {};
    if (body.empty() || body.size() > kBodyLimit) return reject("body_size");
    if (now_ms < 1735689600000LL) return reject("clock_uninitialized");
    FlatJson json(body);
    if (!json.Take('{')) return reject("invalid_json");
    unsigned seen = 0;
    std::string_view temporary, expires, starts;
    for (int i = 0; i < 5; ++i) {
        if (i && !json.Take(',')) return reject("invalid_json");
        std::string_view key, value;
        if (!json.String(key) || !json.Take(':') || !json.String(value)) return reject("invalid_json");
        unsigned bit = 0;
        if (key == "token") { bit = 1; temporary = value; }
        else if (key == "model") { bit = 2; if (value != kModel) return reject("wrong_model"); }
        else if (key == "expires_at") { bit = 4; expires = value; }
        else if (key == "new_session_expires_at") { bit = 8; starts = value; }
        else if (key == "websocket_url") { bit = 16; if (value != kWebSocket) return reject("wrong_endpoint"); }
        else return reject("unknown_field");
        if (seen & bit) return reject("duplicate_field");
        seen |= bit;
    }
    if (seen != 31 || !json.Take('}') || !json.End() || temporary.size() < 16 ||
        temporary.size() > 8192 || temporary == device_credential) return reject("invalid_contract");
    for (char c : temporary) if (c < 0x21 || c > 0x7e) return reject("invalid_temporary");
    const auto expire_ms = TimestampMs(expires), start_ms = TimestampMs(starts);
    if (expire_ms < 0 || start_ms < 0) return reject("invalid_timestamp");
    if (diagnostics) {
        diagnostics->temporary_bytes = temporary.size();
        diagnostics->start_delta_ms = start_ms - now_ms;
        diagnostics->expiry_delta_ms = expire_ms - now_ms;
    }
    if (expire_ms - start_ms != 240000) return reject("invalid_lifetime");
    if (start_ms <= now_ms) return reject("start_expired");
    if (start_ms - now_ms > 65000 || expire_ms - now_ms > 305000) return reject("clock_behind");
    result.temporary_bytes = temporary.size();
    result.start_in_seconds = static_cast<int>((start_ms - now_ms) / 1000);
    result.expires_in_seconds = static_cast<int>((expire_ms - now_ms) / 1000);
    // This view is valid only while the caller owns the response buffer.
    if (borrowed_temporary) *borrowed_temporary = temporary;
    if (diagnostics) diagnostics->reason = "verified";
    return true;
}

enum class ProvisionEvent { NotHandled, Receiving, Ready, Invalid };

class CredentialFrame {
public:
    constexpr ProvisionEvent Feed(char c, std::int64_t now_us) {
        if (!active_) {
            if (c != '~') return ProvisionEvent::NotHandled;
            Reset(); active_ = true; deadline_ = now_us + 5000000;
            return ProvisionEvent::Receiving;
        }
        Expire(now_us);
        if (c == '\n' || c == '\r') {
            active_ = false;
            if (!invalid_ && size_ == 64) return ProvisionEvent::Ready;
            Reset(); return ProvisionEvent::Invalid;
        }
        if (!invalid_ && size_ < 64 && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) data_[size_++] = c;
        else { invalid_ = true; Clear(); }
        return ProvisionEvent::Receiving;
    }
    constexpr bool Expire(std::int64_t now_us) {
        if (active_ && !invalid_ && now_us >= deadline_) { invalid_ = true; Clear(); return true; }
        return false;
    }
    constexpr const char* credential() const { return data_.data(); }
    constexpr void Reset() { Clear(); active_ = false; invalid_ = false; deadline_ = 0; }
private:
    constexpr void Clear() { for (auto& c : data_) c = 0; size_ = 0; }
    std::array<char, 65> data_{};
    std::size_t size_ = 0;
    bool active_ = false;
    bool invalid_ = false;
    std::int64_t deadline_ = 0;
};
}  // namespace companion_session
