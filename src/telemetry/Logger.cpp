#include "telemetry/Logger.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

bool TelemetryLogger::open(const AppSetting& config, const std::string& default_prefix) {
    // =====================================================================
    // main.cppのgetBasePath()と同じ賢いロジックで基準パスを決定
    // =====================================================================
    fs::path base_path = fs::current_path();

    if (fs::exists("input")) {
        // 1. exeと同じ階層に input がある場合（リリース版として実行時）
        base_path = fs::current_path();
    } else if (fs::exists("application/input")) {
        // 2. １つ下に application/input がある場合（VSCodeから実行時）
        base_path = fs::current_path() / "application";
    }

    // 必ず正しい基準パスの直下に logs フォルダを作る
    fs::path logs_dir = base_path / "logs";
    if (!fs::exists(logs_dir)) {
        fs::create_directories(logs_dir);
    }
    // =====================================================================

    std::string prefix = default_prefix;
    if (config.data.contains("logging") && config.data["logging"].contains("log_prefix")) {
        prefix = config.data["logging"]["log_prefix"].get<std::string>();
    }

    auto now_time     = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now_time);
    std::stringstream ss_time;
    ss_time << std::put_time(std::localtime(&now_c), "%Y%m%d_%H%M%S");

    filename_ = (logs_dir / (prefix + "_" + ss_time.str() + ".csv")).string();
    file_.open(filename_);

    if (file_.is_open()) {
        active_fields.clear();

        file_ << "pc_epoch_ms,";

        if (config.data.contains("payload")) {
            for (const auto& item : config.data["payload"]) {
                std::string name = item["name"].get<std::string>();
                active_fields.push_back(name);
                file_ << name << ",";
            }
        }
        file_ << "is_launched,base_time_ms\n";
        return true;
    }
    return false;
}

void TelemetryLogger::log(const TelemetryPacket& packet, bool is_launched, uint32_t base_time_ms) {
    if (!file_.is_open())
        return;

    // ログを書き込む瞬間にPCの絶対時間を取得
    auto now          = std::chrono::system_clock::now();
    uint64_t epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    file_ << epoch_ms << ",";

    // 2. JSONで有効化されたフィールドだけをCSVに書き込む
    for (const auto& field : active_fields) {
        if (field == "time_ms")
            file_ << packet.time << ",";
        else if (field == "accel_x")
            file_ << packet.accel_x << ",";
        else if (field == "accel_y")
            file_ << packet.accel_y << ",";
        else if (field == "accel_z")
            file_ << packet.accel_z << ",";
        else if (field == "gyro_x")
            file_ << packet.gyro_x << ",";
        else if (field == "gyro_y")
            file_ << packet.gyro_y << ",";
        else if (field == "gyro_z")
            file_ << packet.gyro_z << ",";
        else if (field == "mag_x")
            file_ << packet.mag_x << ",";
        else if (field == "mag_y")
            file_ << packet.mag_y << ",";
        else if (field == "mag_z")
            file_ << packet.mag_z << ",";
        else if (field == "pressure")
            file_ << packet.pressure << ",";
        else if (field == "temperature")
            file_ << packet.temperature << ",";
        else
            file_ << "0,";  // 未知のフィールド
    }

    file_ << (is_launched ? 1 : 0) << "," << base_time_ms << "\n";
    file_.flush();
}

std::string TelemetryLogger::getFilename() const {
    return filename_;
}