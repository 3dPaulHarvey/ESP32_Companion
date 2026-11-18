#pragma once
#include "sdkconfig.h"
#include <cstdint>

namespace CompanionSessionProbe {
void Initialize();
bool HandleSerialByte(char byte, std::int64_t now_us);
void Poll(std::int64_t now_us);
void PrintStatus();
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
enum class VoiceStage { Idle, Connecting, Speaking, Finished };
bool RequestVoice();
VoiceStage GetVoiceStage();
void AcknowledgeVoice();
#endif
}
