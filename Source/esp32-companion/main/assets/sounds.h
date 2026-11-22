#pragma once

#include <string_view>

namespace companion_sounds {
extern const char popup_start[] asm("_binary_popup_ogg_start");
extern const char popup_end[] asm("_binary_popup_ogg_end");
inline const std::string_view kPopup{popup_start, static_cast<size_t>(popup_end - popup_start)};
}
