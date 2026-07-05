#pragma once
#include <nlohmann/json.hpp>
#include <random>
#include <chrono>

using json = nlohmann::json;

class DummyDataGenerator {
private:
    std::mt19937 rng;
    std::uniform_real_distribution<double> dist_accel;
    std::uniform_real_distribution<double> dist_temp;
    std::uniform_real_distribution<double> dist_gps_lat;

public:
    DummyDataGenerator() : rng(std::random_device{}()), 
                           dist_accel(-9.8, 9.8), 
                           dist_temp(15.0, 35.0),
                           dist_gps_lat(35.0, 36.0) {}

    // データを生成してJSON文字列として返す関数
    std::string generateJsonString() {
        // 現在のタイムスタンプ
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        // JSONオブジェクトの構築
        json telemetry = {
            {"timestamp", timestamp},
            {"module", "E220_DUMMY"},
            {"accel", {{"x", dist_accel(rng)}, {"y", dist_accel(rng)}, {"z", dist_accel(rng)}}},
            {"temperature", dist_temp(rng)},
            {"pressure", 1013.25 + (dist_accel(rng) * 2)}, // 適当な気圧変動
            {"gps", {{"lat", dist_gps_lat(rng)}, {"lon", 139.0 + dist_accel(rng)/10.0}}}
        };

        return telemetry.dump(); // 文字列に変換
    }
};