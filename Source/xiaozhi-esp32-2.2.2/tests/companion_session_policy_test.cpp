#include "../main/companion/session_policy.h"

namespace {
using namespace companion_session;
constexpr std::string_view kCredential = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
constexpr char kResponse[] = R"({"token":"auth_tokens/public-test-fixture","model":"models/gemini-3.8-live","expires_at":"2026-01-01T00:05:00.000Z","new_session_expires_at":"2026-01-01T00:01:00.000Z","websocket_url":"wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContentConstrained"})";
constexpr std::int64_t kNow = 1767225600000LL;
constexpr bool Accept(std::string_view response, std::int64_t now = kNow, std::string_view credential = kCredential) {
    SessionMetadata result;
    return ValidateSession(response, now, credential, result);
}
constexpr bool ValidMetadata() {
    SessionMetadata result;
    return ValidateSession(kResponse, kNow, kCredential, result) && result.start_in_seconds == 60 &&
        result.expires_in_seconds == 300 && result.temporary_bytes == 31;
}
constexpr bool RejectMutation(std::string_view needle, char replacement) {
    std::array<char, sizeof(kResponse)> changed{};
    for (std::size_t i = 0; i < sizeof(kResponse); ++i) changed[i] = kResponse[i];
    const auto at = std::string_view(kResponse).find(needle);
    if (at == std::string_view::npos) return false;
    changed[at] = replacement;
    return !Accept(std::string_view(changed.data(), sizeof(kResponse) - 1));
}
constexpr bool RejectDuplicate() {
    std::array<char, sizeof(kResponse)> changed{};
    for (std::size_t i = 0; i < sizeof(kResponse); ++i) changed[i] = kResponse[i];
    const auto at = std::string_view(kResponse).find("model");
    constexpr char duplicate[] = "token";
    for (int i = 0; i < 5; ++i) changed[at + i] = duplicate[i];
    return !Accept(std::string_view(changed.data(), sizeof(kResponse) - 1));
}
constexpr bool RejectOversize() {
    std::array<char, kBodyLimit + 1> body{};
    return !Accept(std::string_view(body.data(), body.size()));
}
constexpr bool FrameValidAndCleared() {
    CredentialFrame frame;
    if (frame.Feed('g', 0) != ProvisionEvent::NotHandled || frame.Feed('~', 0) != ProvisionEvent::Receiving) return false;
    for (char c : kCredential) if (frame.Feed(c, 1000) != ProvisionEvent::Receiving) return false;
    if (frame.Feed('\n', 2000) != ProvisionEvent::Ready || std::string_view(frame.credential()) != kCredential) return false;
    frame.Reset();
    for (int i = 0; i < 65; ++i) if (frame.credential()[i]) return false;
    return true;
}
constexpr bool RejectFrame(std::size_t length, bool invalid_character) {
    CredentialFrame frame;
    frame.Feed('~', 0);
    for (std::size_t i = 0; i < length; ++i) frame.Feed(invalid_character && i == 1 ? 'g' : 'a', 1000);
    const auto result = frame.Feed('\n', 2000);
    for (int i = 0; i < 65; ++i) if (frame.credential()[i]) return false;
    return result == ProvisionEvent::Invalid && frame.Feed('g', 3000) == ProvisionEvent::NotHandled;
}
constexpr bool TimeoutDrainsInput() {
    CredentialFrame frame;
    frame.Feed('~', 0);
    frame.Feed('a', 1000);
    if (!frame.Expire(5000000) || frame.Expire(5000001)) return false;
    if (frame.credential()[0] != 0 || frame.Feed('g', 5000001) != ProvisionEvent::Receiving) return false;
    return frame.Feed('\n', 5000002) == ProvisionEvent::Invalid && frame.Feed('g', 5000003) == ProvisionEvent::NotHandled;
}

constexpr bool BorrowedViewHasOwner() {
    SessionMetadata result;
    std::string_view temporary;
    if (!ValidateSession(kResponse, kNow, kCredential, result, &temporary) || temporary != "auth_tokens/public-test-fixture") return false;
    if (temporary.data() != kResponse + std::string_view(kResponse).find("auth_tokens/")) return false;
    return !ValidateSession(kResponse, kNow + 60000, kCredential, result, &temporary) && temporary.empty();
}

static_assert(TimestampMs("2026-01-01T00:00:00.000Z") == kNow);
static_assert(TimestampMs("2028-02-29T12:34:56.123Z") > 0);
static_assert(TimestampMs("2026-02-29T00:00:00.000Z") == -1);
static_assert(TimestampMs("2100-02-29T00:00:00.000Z") == -1);
static_assert(TimestampMs("2026-13-01T00:00:00.000Z") == -1);
static_assert(TimestampMs("2026-01-01T24:00:00.000Z") == -1);
static_assert(TimestampMs("2026-01-01T00:00:00.000+00:00") == -1);
static_assert(ValidCredential(kCredential) && !ValidCredential("short"));
static_assert(!ValidCredential("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeF"));
static_assert(ValidMetadata());
static_assert(!Accept(kResponse, kNow + 60000));
static_assert(!Accept(kResponse, kNow - 5001));
static_assert(!Accept(kResponse, kNow, "auth_tokens/public-test-fixture"));
static_assert(!Accept(std::string_view(kResponse, sizeof(kResponse) - 2)));
static_assert(RejectMutation("model", 'x'));
static_assert(RejectMutation("gemini-3.8-live", 'x'));
static_assert(RejectMutation("generativelanguage", 'x'));
static_assert(RejectMutation("public-test", '\\'));
static_assert(RejectMutation("00:05:00", '1'));
static_assert(RejectDuplicate() && RejectOversize());
static_assert(FrameValidAndCleared());
static_assert(RejectFrame(63, false) && RejectFrame(65, false) && RejectFrame(64, true));
static_assert(TimeoutDrainsInput());
static_assert(BorrowedViewHasOwner());
}  // namespace
