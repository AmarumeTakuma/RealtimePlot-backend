#pragma once

#include <atomic>

class CommandListener {
public:
    std::atomic<bool> request_calibration{false};
    std::atomic<bool> request_launch{false};

    void start();
};