#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include "display.h"
#include <lvgl.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

class OledDisplay : public Display {
public:
    OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel);
    ~OledDisplay() override;
    void SetStatus(const char* status) override;
    void SetFaceState(FaceState state) override;

private:
    esp_lcd_panel_io_handle_t panel_io_;
    esp_lcd_panel_handle_t panel_;
    lv_display_t* display_ = nullptr;
    lv_timer_t* face_timer_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* left_eye_ = nullptr;
    lv_obj_t* right_eye_ = nullptr;
    lv_obj_t* mouth_ = nullptr;
    FaceState face_state_ = FaceState::Idle;
    int blink_phase_ = 0;
    int idle_move_offset_x_ = 0;
    int idle_move_offset_y_ = 0;
    uint32_t speak_last_update_ = 0;
    int speak_mouth_target_ = 4;
    int speak_mouth_current_ = 4;

    void SetupUI();
    void UpdateFace();
    void IdleBehavior(int base_eye_height);
    void ListeningBehavior(int base_eye_height);
    void SpeakingBehavior(int base_eye_height);
    bool Lock(int timeout_ms = 0) override;
    void Unlock() override;
};

#endif
