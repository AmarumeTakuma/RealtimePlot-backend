#pragma once

#include <string>
#include <fstream>
#include <vector>
#include <cstdint>
#include "app/AppSetting.hpp"
#include "telemetry/TelemetryPacket.hpp"

class TelemetryLogger {
private:
    std::ofstream file_;
    std::string filename_;
    std::vector<std::string> active_fields; // JSONで指定された有効なフィールド名

public:
    bool open(const AppSetting& config, const std::string& default_prefix);
    
    // 引数を個別に渡すのをやめ、パケットを丸ごと渡す
    void log(const TelemetryPacket& packet, bool is_launched, uint32_t base_time_ms);
    
    std::string getFilename() const;
};