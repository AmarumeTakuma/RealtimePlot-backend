#pragma once

#include <string>
#include <fstream>
#include <cstdint>
#include "app/AppSetting.hpp"
#include "TelemetryPacket.hpp"

class TelemetryLogger {
private:
    std::ofstream file_;
    std::string filename_;

public:
    bool open(const AppSetting& config, const std::string& default_prefix);
    void log(uint32_t time_ms, float ax, float ay, float az, float gx, float gy, float gz, float press, float temp, bool is_launched, uint32_t base_time_ms);
    std::string getFilename() const;
};