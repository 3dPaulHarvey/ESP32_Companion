#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace companion_voice {
// Borrowed JSON tokens: no string copies or unbounded allocations/recursion.
// Protocol field names must be unescaped ASCII. Ignored string values may
// contain JSON escapes; audio values are checked separately before decoding.
class JsonDocument {
public:
    enum class Type { Object, Array, String, True, False, Null, Number };
    struct Token {
        std::string_view value{};
        std::uint16_t next = 0;
        Type type = Type::Null;
        bool escaped = false;
    };
    static constexpr std::uint16_t kMissing = 0xffff;
    static constexpr std::size_t kTokenLimit = 192;
    constexpr bool Parse(std::string_view input) {
        input_ = input; position_ = 0; count_ = 0;
        if (input.empty() || input.size() > 65536 || !Value(0)) return false;
        Space();
        return position_ == input_.size() && tokens_[0].type == Type::Object;
    }
    constexpr const Token& operator[](std::uint16_t index) const { return tokens_[index]; }
    constexpr std::uint16_t Find(std::uint16_t object, std::string_view name) const {
        if (object == kMissing || tokens_[object].type != Type::Object) return kMissing;
        for (auto i = static_cast<std::uint16_t>(object + 1); i < tokens_[object].next; i = tokens_[i + 1].next)
            if (tokens_[i].value == name) return i + 1;
        return kMissing;
    }
private:
    std::array<Token, kTokenLimit> tokens_{};
    std::string_view input_{};
    std::size_t position_ = 0;
    std::uint16_t count_ = 0;
    constexpr void Space() {
        while (position_ < input_.size() && (input_[position_] == ' ' || input_[position_] == '\t' ||
            input_[position_] == '\r' || input_[position_] == '\n')) ++position_;
    }
    constexpr bool Take(char c) { Space(); if (position_ == input_.size() || input_[position_] != c) return false; ++position_; return true; }
    constexpr bool Digit() const { return position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9'; }
    constexpr bool Hex(char c) const { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
    constexpr bool String(Token& token, bool key) {
        if (!Take('"')) return false;
        const auto start = position_;
        while (position_ < input_.size()) {
            const auto c = static_cast<unsigned char>(input_[position_++]);
            if (c == '"') { token.value = input_.substr(start, position_ - start - 1); return true; }
            if (c < 0x20 || (key && c > 0x7e)) return false;
            if (c != '\\') continue;
            if (key || position_ == input_.size()) return false;
            token.escaped = true;
            const char escaped = input_[position_++];
            if (escaped == 'u') {
                for (int i = 0; i < 4; ++i) if (position_ == input_.size() || !Hex(input_[position_++])) return false;
            } else if (escaped != '"' && escaped != '\\' && escaped != '/' && escaped != 'b' &&
                       escaped != 'f' && escaped != 'n' && escaped != 'r' && escaped != 't') return false;
        }
        return false;
    }
    constexpr bool Number() {
        if (position_ < input_.size() && input_[position_] == '-') ++position_;
        if (!Digit()) return false;
        if (input_[position_] == '0') ++position_;
        else while (Digit()) ++position_;
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            if (!Digit()) return false;
            while (Digit()) ++position_;
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) ++position_;
            if (!Digit()) return false;
            while (Digit()) ++position_;
        }
        return true;
    }
    constexpr bool Value(unsigned depth) {
        Space();
        if (depth > 8 || position_ == input_.size() || count_ == kTokenLimit) return false;
        const auto index = count_++;
        auto& token = tokens_[index];
        token = {};
        const char c = input_[position_];
        if (c == '{' || c == '[') {
            token.type = c == '{' ? Type::Object : Type::Array;
            const char end = c == '{' ? '}' : ']';
            ++position_; Space();
            if (position_ < input_.size() && input_[position_] == end) { ++position_; token.next = count_; return true; }
            while (true) {
                if (c == '{') {
                    if (count_ == kTokenLimit) return false;
                    const auto key = count_++;
                    tokens_[key] = {}; tokens_[key].type = Type::String;
                    if (!String(tokens_[key], true)) return false;
                    for (auto previous = static_cast<std::uint16_t>(index + 1); previous < key; previous = tokens_[previous + 1].next)
                        if (tokens_[previous].value == tokens_[key].value) return false;
                    tokens_[key].next = count_;
                    if (!Take(':')) return false;
                }
                if (!Value(depth + 1)) return false;
                Space();
                if (position_ < input_.size() && input_[position_] == end) { ++position_; token.next = count_; return true; }
                if (!Take(',')) return false;
            }
        }
        if (c == '"') { token.type = Type::String; if (!String(token, false)) return false; }
        else if (input_.substr(position_, 4) == "true") { token.type = Type::True; position_ += 4; }
        else if (input_.substr(position_, 5) == "false") { token.type = Type::False; position_ += 5; }
        else if (input_.substr(position_, 4) == "null") { token.type = Type::Null; position_ += 4; }
        else { token.type = Type::Number; if (!Number()) return false; }
        token.next = count_;
        return true;
    }
};
}  // namespace companion_voice
