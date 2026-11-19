#pragma once
#include "gemini_setup.h"
#include "followup_policy.h"
#include <array>
#include <string_view>

namespace CompanionConversation {
struct Turn {
    unsigned input_bytes = 0, input_chunks = 0;
    unsigned upload_ms = 0, max_send_ms = 0, slow_sends = 0;
    bool input_ended = false, turn_complete = false;
    unsigned audio_bytes = 0, audio_chunks = 0, played_samples = 0, duration_ms = 0;
    unsigned rms = 0, peak = 0, clipped = 0, playback_ms = 0;
};
struct Result {
    bool ok = false;
    const char* reason = "unavailable";
    CompanionGeminiSetup::Result connection{};
    std::array<Turn, companion_followup::kMaxTurns> turns{};
    unsigned completed_turns = 0, followup_captures = 0, quiet_discards = 0;
    unsigned write_calls = 0, write_waits = 0, short_writes = 0;
    const char* cleanup_reason = "none";
};
Result Run(std::string_view temporary, companion_question::Pcm& first, bool fixture = false);
}
