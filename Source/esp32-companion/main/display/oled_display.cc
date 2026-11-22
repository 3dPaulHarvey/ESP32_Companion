#include "oled_display.h"
#include "boards/seeedstudio-s3-wifi/board_config.h"
#include <algorithm>
#include <cstdlib>
#include <esp_err.h>
#include <esp_lvgl_port.h>

static_assert(DISPLAY_WIDTH == 128 && DISPLAY_HEIGHT == 64);

OledDisplay::OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel)
    : panel_io_(panel_io), panel_(panel) {
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.task_stack = 6144;
    port_cfg.task_affinity = 1;
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = DISPLAY_WIDTH * DISPLAY_HEIGHT,
        .double_buffer = false,
        .trans_size = 0,
        .hres = DISPLAY_WIDTH,
        .vres = DISPLAY_HEIGHT,
        .monochrome = true,
        .rotation = { .swap_xy = false, .mirror_x = DISPLAY_MIRROR_X, .mirror_y = DISPLAY_MIRROR_Y },
        .flags = { .buff_dma = 1, .buff_spiram = 0, .sw_rotate = 0,
                   .full_refresh = 0, .direct_mode = 0 },
    };
    display_ = lvgl_port_add_disp(&display_cfg);
    ESP_ERROR_CHECK(display_ ? ESP_OK : ESP_FAIL);
    SetupUI();
    DisplayLockGuard lock(this);
    face_timer_ = lv_timer_create([](lv_timer_t* timer) {
        static_cast<OledDisplay*>(lv_timer_get_user_data(timer))->UpdateFace();
    }, 60, this);
}

OledDisplay::~OledDisplay() {
    {
        DisplayLockGuard lock(this);
        if (face_timer_) lv_timer_delete(face_timer_);
        if (container_) lv_obj_delete(container_);
        if (status_bar_) lv_obj_delete(status_bar_);
    }
    if (display_) lvgl_port_remove_disp(display_);
    lvgl_port_deinit();
    if (panel_) esp_lcd_panel_del(panel_);
    if (panel_io_) esp_lcd_panel_io_del(panel_io_);
}

bool OledDisplay::Lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }
void OledDisplay::Unlock() { lvgl_port_unlock(); }

void OledDisplay::SetStatus(const char* status) {
    DisplayLockGuard lock(this);
    lv_label_set_text(status_label_, status);
}

void OledDisplay::SetFaceState(FaceState state) {
    DisplayLockGuard lock(this);
    if (face_state_ != state) blink_phase_ = 0;
    face_state_ = state;
}

void OledDisplay::IdleBehavior(int base_eye_height) {
    // Random subtle movement
    if (rand() % 40 == 0) {
        idle_move_offset_x_ = (rand() % 5) - 2;
        idle_move_offset_y_ = (rand() % 3) - 1;
    }

    int eye_h = base_eye_height;
    int eye_w = eye_h * 0.65;  // slightly oval

    lv_obj_set_size(left_eye_, eye_w, eye_h);
    lv_obj_set_size(right_eye_, eye_w, eye_h);

    lv_obj_align(left_eye_, LV_ALIGN_CENTER, -12 + idle_move_offset_x_, -5 + idle_move_offset_y_);
    lv_obj_align(right_eye_, LV_ALIGN_CENTER, 12 + idle_move_offset_x_, -5 + idle_move_offset_y_);

    // Mouth relaxed
    lv_obj_set_size(mouth_, 14, 3);
    lv_obj_align(mouth_, LV_ALIGN_CENTER, idle_move_offset_x_, 10 + idle_move_offset_y_);
}

void OledDisplay::ListeningBehavior(int base_eye_height) {
    int right_h = base_eye_height;
    int right_w = std::max(1, static_cast<int>(right_h * 0.65));  // slightly oval

    int left_h = std::max(1, base_eye_height - 2);
    int left_w = std::max(1, static_cast<int>(left_h * 0.65));  // slightly oval

    lv_obj_set_size(left_eye_, left_w, left_h);
    lv_obj_set_size(right_eye_, right_w, right_h);

    // Shift whole face slightly left
    lv_obj_align(left_eye_, LV_ALIGN_CENTER, -16, -5);
    lv_obj_align(right_eye_, LV_ALIGN_CENTER, 4, -5);

    // Mouth smaller
    lv_obj_set_size(mouth_, 10, 2);
    lv_obj_align(mouth_, LV_ALIGN_CENTER, -4, 10);
}

void OledDisplay::SpeakingBehavior(int eye_height) {
    uint32_t now = lv_tick_get();

    // --- EYES ---
    lv_obj_set_size(left_eye_, eye_height* 0.65, eye_height);
    lv_obj_set_size(right_eye_, eye_height* 0.65, eye_height);

    lv_obj_align(left_eye_, LV_ALIGN_CENTER, -30, -6);
    lv_obj_align(right_eye_, LV_ALIGN_CENTER, 30, -6);

    // --- MOUTH TIMING ---
    if (now - speak_last_update_ > 90 + (rand() % 60)) {
        speak_last_update_ = now;

        // simulate syllables
        int r = rand() % 100;

        if (r < 20)
            speak_mouth_target_ = 2;  // closed
        else if (r < 50)
            speak_mouth_target_ = 6;  // small
        else if (r < 80)
            speak_mouth_target_ = 10;  // medium
        else
            speak_mouth_target_ = 16;  // wide
    }

    // --- SMOOTH TRANSITION ---
    if (speak_mouth_current_ < speak_mouth_target_)
        speak_mouth_current_ += 2;
    else if (speak_mouth_current_ > speak_mouth_target_)
        speak_mouth_current_ -= 2;

    // --- APPLY MOUTH ---
    lv_obj_set_size(mouth_, 15, speak_mouth_current_);
    lv_obj_set_style_radius(mouth_, 8, 0);
    lv_obj_align(mouth_, LV_ALIGN_CENTER, 0, 18);
}

void OledDisplay::UpdateFace() {
    DisplayLockGuard lock(this);

    // --- Random blink trigger ---
    // A steady listening pose visibly spans the entire microphone capture.
    // Keep the existing random blink in the other expressions.
    if (face_state_ == FaceState::Listening) {
        blink_phase_ = 0;
    } else if (blink_phase_ == 0) {
        if (rand() % 120 == 0) {  // random blink
            blink_phase_ = 1;
        }
    }

    // --- Blink state machine ---
    int eye_height = 20;

    switch (blink_phase_) {
        case 1:  // closing
            eye_height = 4;
            blink_phase_ = 2;
            break;
        case 2:  // closed
            eye_height = 1;
            blink_phase_ = 3;
            break;
        case 3:  // opening
            eye_height = 6;
            blink_phase_ = 0;
            break;
        default:
            eye_height = 20;
            break;
    }

    // --- State-specific behavior ---
    switch (face_state_) {
        case FaceState::Idle:
            IdleBehavior(eye_height);
            break;

        case FaceState::Listening:
            ListeningBehavior(eye_height);
            break;

        case FaceState::Speaking:
            SpeakingBehavior(eye_height);
            break;
    }
}

void OledDisplay::SetupUI() {
    DisplayLockGuard lock(this);
    auto screen = lv_display_get_screen_active(display_);
    lv_obj_set_style_text_font(screen, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(screen, lv_color_black(), 0);

    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, 128, 64);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);

    // Keep the original face geometry: a 16-pixel status row above a 48-pixel face.
    auto top_bar = lv_obj_create(container_);
    lv_obj_set_size(top_bar, 128, 16);
    lv_obj_set_style_radius(top_bar, 0, 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 0, 0);
    lv_obj_set_scrollbar_mode(top_bar, LV_SCROLLBAR_MODE_OFF);

    status_bar_ = lv_obj_create(screen);
    lv_obj_set_size(status_bar_, 128, 16);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_layout(status_bar_, LV_LAYOUT_NONE, 0);
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0);
    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(status_label_, 128);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(status_label_, "INITIALIZING");
    lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);

    auto content = lv_obj_create(container_);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_size(content, 128, 48);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto face = lv_obj_create(content);
    lv_obj_set_size(face, 128, 48);
    lv_obj_set_style_border_width(face, 0, 0);
    lv_obj_set_style_bg_opa(face, LV_OPA_TRANSP, 0);
    left_eye_ = lv_obj_create(face);
    right_eye_ = lv_obj_create(face);
    for (auto eye : {left_eye_, right_eye_}) {
        lv_obj_set_style_bg_color(eye, lv_color_black(), 0);
        lv_obj_set_style_border_width(eye, 0, 0);
        lv_obj_set_size(eye, 20, 20);
        lv_obj_set_style_radius(eye, 2, 0);
    }
    lv_obj_align(left_eye_, LV_ALIGN_CENTER, -30, -4);
    lv_obj_align(right_eye_, LV_ALIGN_CENTER, 30, -4);
    mouth_ = lv_obj_create(face);
    lv_obj_set_style_bg_color(mouth_, lv_color_black(), 0);
    lv_obj_set_style_border_width(mouth_, 0, 0);
    lv_obj_set_style_radius(mouth_, 4, 0);
}
