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
bool IsConfigured();
bool AdmissionOpen(std::int64_t now_us);
void SetCalibrationGuard(bool active);
#endif
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
enum class VoiceStage { Idle, Connecting, Speaking, FollowupRequested, FollowupListening, Finished };
bool RequestVoice();
VoiceStage GetVoiceStage();
void AcknowledgeVoice();
#endif
#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
void SetQuestionCaptureGuard(bool active);
bool RequestQuestion(std::uint32_t capture_id);
void CancelQuestion();
bool QuestionCancelled();
#if CONFIG_COMPANION_FOLLOWUP
enum class ConversationOwner { Desktop, Standalone };
bool RequestConversation(std::uint32_t capture_id, ConversationOwner owner = ConversationOwner::Desktop);
bool LastConversationSucceeded();
bool ConversationActive();
unsigned ConversationTurn();
void SetConversationStage(VoiceStage stage, unsigned turn);
void RequestFollowupCapture();
bool BeginFollowupCapture();
bool SubmitFollowupCapture(std::uint32_t capture_id);
std::uint32_t TakeFollowupCaptureId();
void HeartbeatConversation(std::int64_t now_us);
#if COMPANION_QUESTION_FIXTURE
bool RequestConversationFixture(ConversationOwner owner = ConversationOwner::Desktop);
#endif
#endif
#if COMPANION_QUESTION_FIXTURE
bool RequestQuestionFixture();
#endif
#endif
}
