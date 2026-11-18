#pragma once
#include "sdkconfig.h"
#include <cstdint>
#include <string_view>

namespace CompanionGeminiVoice { class Reply; }
namespace companion_question { class Pcm; }

namespace CompanionGeminiSetup {
struct Result {
    bool ok = false;
    int upgrade_http = 0;
    bool setup_ack = false;
    bool prompt_sent = false;
    unsigned input_bytes = 0, input_chunks = 0;
    bool input_ended = false;
    bool close_sent = false;
    bool close_ack = false;
    unsigned ack_bytes = 0;
    unsigned close_code = 0;
    std::uint32_t elapsed_ms = 0;
    const char* reason = "unavailable";
};
void Initialize();
Result Verify(std::string_view temporary);
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
Result ReceiveGreeting(std::string_view temporary, CompanionGeminiVoice::Reply& reply);
#endif
#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
Result ReceiveQuestion(std::string_view temporary, CompanionGeminiVoice::Reply& reply, companion_question::Pcm& input);
#endif
}
