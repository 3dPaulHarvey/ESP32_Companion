#pragma once
#include "sdkconfig.h"
#include <cstdint>

namespace CompanionSessionProbe {
void Initialize();
bool HandleSerialByte(char byte, std::int64_t now_us);
void Poll(std::int64_t now_us);
void PrintStatus();
#if CONFIG_COMPANION_MICROPHONE_PREP
bool IsBusy();
void SetCalibrationGuard(bool active);
#endif
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
enum class VoiceStage { Idle, Connecting, Speaking, Finished };
bool RequestVoice();
VoiceStage GetVoiceStage();
void AcknowledgeVoice();
#endif
#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
void SetQuestionCaptureGuard(bool active);
bool RequestQuestion(std::uint32_t capture_id);
void CancelQuestion();
bool QuestionCancelled();
#if COMPANION_QUESTION_FIXTURE
bool RequestQuestionFixture();
#endif
#endif
}
