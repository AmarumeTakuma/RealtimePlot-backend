#pragma once
#include <atomic>
#include <cstdint>  // uint16_t用

class CommandListener {
public:
    std::atomic<bool> request_calibration{false};
    std::atomic<bool> request_launch{false};

    // 引数にポート番号を受け取る
    void start(uint16_t port);
};