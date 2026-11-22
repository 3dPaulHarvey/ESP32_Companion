#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace companion_audio {

enum class PlaybackAction { Wait, Ready, Complete, Invalid };
struct PlaybackSlice {
    PlaybackAction action = PlaybackAction::Invalid;
    std::size_t offset = 0, samples = 0;
};

// Account only for the prefix committed by the producer. EOF freezes its
// length. Committing a queued slice advances the consumer exactly once.
class PlaybackPrefix {
public:
    constexpr explicit PlaybackPrefix(std::size_t capacity, std::size_t prebuffer = 2880, std::size_t quantum = 480)
        : capacity_(capacity), prebuffer_(std::min(prebuffer, capacity)), quantum_(quantum) {}
    constexpr PlaybackSlice Next(std::size_t available, bool finished) {
        if (invalid_ || !capacity_ || !quantum_ || available > capacity_ || available < seen_ || available < consumed_ ||
            (ended_ && (!finished || available != seen_))) {
            invalid_ = true; offered_ = 0; return {};
        }
        seen_ = available;
        ended_ = ended_ || finished;
        offered_ = 0;
        if (!started_ && !finished && available < prebuffer_) return {PlaybackAction::Wait};
        if (consumed_ == available)
            return {finished ? (consumed_ ? PlaybackAction::Complete : PlaybackAction::Invalid) : PlaybackAction::Wait};
        if (!finished && available - consumed_ < quantum_) return {PlaybackAction::Wait};
        offered_ = std::min(quantum_, available - consumed_);
        return {PlaybackAction::Ready, consumed_, offered_};
    }
    constexpr bool Commit(std::size_t samples) {
        if (!samples || samples != offered_) return false;
        consumed_ += samples;
        offered_ = 0;
        started_ = true;
        return true;
    }
    constexpr std::size_t consumed() const { return consumed_; }
private:
    std::size_t capacity_, prebuffer_, quantum_, consumed_ = 0, seen_ = 0, offered_ = 0;
    bool started_ = false, ended_ = false, invalid_ = false;
};

// Before local speech acceptance, retain only the latest bounded pre-roll and
// expose zero samples. After acceptance, append without changing any previously
// exposed sample. All storage belongs to the caller, so this is host-testable.
class QuestionPublisher {
public:
    constexpr QuestionPublisher(std::int16_t* pcm, std::size_t capacity, std::int16_t* pre_roll, std::size_t pre_roll_capacity)
        : pcm_(pcm), capacity_(capacity), pre_roll_(pre_roll), pre_roll_capacity_(pre_roll_capacity) {}
    constexpr bool valid() const {
        return pcm_ && pre_roll_ && capacity_ && pre_roll_capacity_ && pre_roll_capacity_ <= capacity_;
    }
    constexpr bool Feed(const std::int16_t* input, std::size_t count, bool speech_accepted) {
        if (!valid() || !input || !count || count > capacity_ || sealed_ || (started_ && !speech_accepted)) return false;
        if (started_) {
            if (count > capacity_ - published_) return false;
            std::copy_n(input, count, pcm_ + published_);
            published_ += count;
            return true;
        }
        for (std::size_t i = 0; i < count; ++i) pre_roll_[(pre_roll_next_ + i) % pre_roll_capacity_] = input[i];
        pre_roll_next_ = (pre_roll_next_ + count) % pre_roll_capacity_;
        pre_roll_used_ = std::min(pre_roll_capacity_, pre_roll_used_ + count);
        if (speech_accepted) {
            const auto first = (pre_roll_next_ + pre_roll_capacity_ - pre_roll_used_) % pre_roll_capacity_;
            const auto first_count = std::min(pre_roll_used_, pre_roll_capacity_ - first);
            std::copy_n(pre_roll_ + first, first_count, pcm_);
            std::copy_n(pre_roll_, pre_roll_used_ - first_count, pcm_ + first_count);
            published_ = pre_roll_used_;
            started_ = true;
        }
        return true;
    }
    constexpr void Seal() { sealed_ = true; }
    constexpr std::size_t published() const { return published_; }
    constexpr bool speech_started() const { return started_; }
private:
    std::int16_t* pcm_;
    std::size_t capacity_;
    std::int16_t* pre_roll_;
    std::size_t pre_roll_capacity_, pre_roll_used_ = 0, pre_roll_next_ = 0, published_ = 0;
    bool started_ = false, sealed_ = false;
};

}  // namespace companion_audio
