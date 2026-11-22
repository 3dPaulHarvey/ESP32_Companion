#include "../main/companion/followup_policy.h"
using namespace companion_followup;
namespace {
constexpr SpeechGate Gate(unsigned positives, bool alternating = false) {
    SpeechGate gate;
    for (unsigned i = 0; i < kFrames; ++i) gate.Feed(alternating ? (i % 2 == 0 && i / 2 < positives) : i < positives);
    return gate;
}
static_assert(kFrames * kFrameSamples == 64000);
static_assert(Gate(0).valid() && !Gate(0).speech());
static_assert(!Gate(9).speech());
static_assert(Gate(10).speech());
static_assert(!Gate(20, true).speech());
static_assert(!SpeechGate{}.speech());
static_assert(CanUploadFollowup(1, Gate(10)));
static_assert(!CanUploadFollowup(0, Gate(10)));
static_assert(!CanUploadFollowup(2, Gate(200)));
static_assert(!CanUploadFollowup(1, Gate(0)));
constexpr bool Bounded() { auto gate = Gate(200); return !gate.Feed(true) && gate.frames() == 200; }
static_assert(Bounded());
}
