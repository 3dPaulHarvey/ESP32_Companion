#pragma once
#include "question_audio.h"
#include <memory>

// Developer-only synthetic source. Not linked into normal question firmware.
std::unique_ptr<companion_question::Pcm> MakeQuestionFixture();
#if CONFIG_COMPANION_FOLLOWUP
std::unique_ptr<companion_question::Pcm> MakeFollowupFixture(bool silent);
#endif
