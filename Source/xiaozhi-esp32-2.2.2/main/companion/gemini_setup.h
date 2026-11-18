#pragma once
#include <cstdint>
#include <string_view>

namespace CompanionGeminiSetup {
struct Result {
    bool ok = false;
    int upgrade_http = 0;
    bool setup_ack = false;
    bool close_sent = false;
    bool close_ack = false;
    unsigned ack_bytes = 0;
    unsigned close_code = 0;
    std::uint32_t elapsed_ms = 0;
    const char* reason = "unavailable";
};
void Initialize();
Result Verify(std::string_view temporary);
}
