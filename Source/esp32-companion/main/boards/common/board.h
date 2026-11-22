#ifndef BOARD_H
#define BOARD_H

class AudioCodec;
class Display;

// The companion has one fixed XIAO ESP32-S3 hardware configuration.
class Board {
public:
    static Board& GetInstance();
    AudioCodec* GetAudioCodec();
    Display* GetDisplay();

private:
    Board();
    Board(const Board&) = delete;
    Board& operator=(const Board&) = delete;
    Display* display_ = nullptr;
};

#endif
