#ifndef DISPLAY_H
#define DISPLAY_H

#include <esp_err.h>

enum class FaceState { Idle, Listening, Speaking };

// Shared by the voice workflow and hardware diagnostics.
class Display {
public:
    virtual ~Display() = default;
    virtual void SetStatus(const char* status) = 0;
    virtual void SetFaceState(FaceState state) = 0;

protected:
    friend class DisplayLockGuard;
    virtual bool Lock(int timeout_ms = 0) = 0;
    virtual void Unlock() = 0;
};

class DisplayLockGuard {
public:
    explicit DisplayLockGuard(Display* display) : display_(display) {
        ESP_ERROR_CHECK(display_->Lock(30000) ? ESP_OK : ESP_ERR_TIMEOUT);
    }
    ~DisplayLockGuard() { display_->Unlock(); }

private:
    Display* display_;
};

#endif
