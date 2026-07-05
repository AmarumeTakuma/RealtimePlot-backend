#include "telemetry/Logger.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <filesystem>

namespace fs = std::filesystem;

bool TelemetryLogger::open(const AppSetting& config, const std::string& default_prefix) {
    fs::path logs_dir = fs::current_path() / "application" / "logs";
    if (!fs::exists(logs_dir)) {
        fs::create_directories(logs_dir);
    }

    std::string prefix = default_prefix;
    if (config.data.contains("logging") && config.data["logging"].contains("log_prefix")) {
        prefix = config.data["logging"]["log_prefix"].get<std::string>();
    }

    auto now_time = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now_time);
    std::stringstream ss_time;
    ss_time << std::put_time(std::localtime(&now_c), "%Y%m%d_%H%M%S");
    
    filename_ = (logs_dir / (prefix + "_" + ss_time.str() + ".csv")).string();
    file_.open(filename_);

    if (file_.is_open()) {
        file_ << "time_ms,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,pressure,temperature,is_launched,base_time_ms\n";
        return true;
    }
    return false;
}

void TelemetryLogger::log(uint32_t time_ms, float ax, float ay, float az, float gx, float gy, float gz, float press, float temp, bool is_launched, uint32_t base_time_ms) {
    if (file_.is_open()) {
        file_ << time_ms << "," << ax << "," << ay << "," << az << ","
              << gx << "," << gy << "," << gz << "," << press << ","
              << temp << "," << (is_launched ? 1 : 0) << "," << base_time_ms << "\n";
        file_.flush();
    }
}

std::string TelemetryLogger::getFilename() const { return filename_; }