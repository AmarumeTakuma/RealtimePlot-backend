#include "simulator/CsvSimulator.hpp"
#include "telemetry/Logger.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

CsvSimulator::CsvSimulator(AppSetting& cfg, CommandListener& c, asio::io_context& io, asio::ip::udp::socket& sock, asio::ip::udp::endpoint& ep)
    : config(cfg), cmd(c), io_context(io), send_socket(sock), send_endpoint(ep) {}

void CsvSimulator::sendJson(uint32_t t, uint32_t bt, double temp, double press, double az, double gx, double gy, double gz) {
    json j;
    j["base_time"] = bt;
    
    // HardwareReceiverと同じように、JSONの定義に従って動的にパケットを作る
    if (config.data.contains("payload")) {
        for (const auto& item : config.data["payload"]) {
            std::string name = item["name"].get<std::string>();
            if (name == "time_ms") j[name] = t;
            else if (name == "accel_z") j[name] = az;
            else if (name == "gyro_x") j[name] = gx;
            else if (name == "gyro_y") j[name] = gy;
            else if (name == "gyro_z") j[name] = gz;
            else if (name == "pressure") j[name] = press;
            else if (name == "temperature") j[name] = temp;
            else j[name] = 0.0; // シミュレーションデータに無い項目（地磁気など）は0で埋める
        }
    }
    send_socket.send_to(asio::buffer(j.dump()), send_endpoint);
}

void CsvSimulator::run(const std::string& filename, CsvFormat format) {
    std::ifstream file(filename);
    if (!file.is_open()) return;

    std::string line, col;
    std::getline(file, line); 
    std::stringstream ss(line);
    std::vector<std::string> headers;
    while (std::getline(ss, col, ',')) {
        if (!col.empty() && col.back() == '\r') col.pop_back();
        headers.push_back(col);
    }

    std::string h_time = (format == CsvFormat::PROLOGUE) ? "time_from_launch[s]" : "time";
    std::string h_press = (format == CsvFormat::PROLOGUE) ? "pressure[Pa]" : "pressure";
    std::string h_temp = (format == CsvFormat::PROLOGUE) ? "temperature[C]" : "temperature";
    std::string h_accel = (format == CsvFormat::PROLOGUE) ? "longitudinal accel[m/s2]" : "accel_z";

    int idx_time = -1, idx_press = -1, idx_temp = -1, idx_accel = -1;
    for (size_t i = 0; i < headers.size(); ++i) {
        if (headers[i] == h_time) idx_time = static_cast<int>(i);
        else if (headers[i] == h_press) idx_press = static_cast<int>(i);
        else if (headers[i] == h_temp) idx_temp = static_cast<int>(i);
        else if (headers[i] == h_accel) idx_accel = static_cast<int>(i);
    }

    double init_press = 1013.25, init_temp = 15.0, init_accel = 9.81;
    std::streampos data_start_pos = file.tellg(); 
    if (std::getline(file, line)) {
        std::stringstream row_ss(line);
        std::vector<std::string> row;
        while (std::getline(row_ss, col, ',')) row.push_back(col);
        try {
            init_press = (format == CsvFormat::PROLOGUE) ? std::stod(row[idx_press]) / 100.0 : std::stod(row[idx_press]);
            init_temp = std::stod(row[idx_temp]);
            init_accel = std::stod(row[idx_accel]);
        } catch(...) {}
    }
    file.seekg(data_start_pos); 

    TelemetryLogger logger;
    if (logger.open(config, "sim_log")) {
        std::cout << "[INFO] Logging simulation data to: " << logger.getFilename() << std::endl;
    }

    auto system_start_time = std::chrono::steady_clock::now();
    cmd.request_launch = false;
    uint32_t base_time_ms = 0; 

    std::cout << "--- HILS Standby: Waiting for CMD_LAUNCH... ---" << std::endl;

    while (!cmd.request_launch) {
        auto now = std::chrono::steady_clock::now();
        uint32_t current_time_ms = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - system_start_time).count());
        
        // 構造体を作って渡す形に修正
        TelemetryPacket pkt{};
        pkt.time = current_time_ms;
        pkt.accel_z = static_cast<float>(init_accel);
        pkt.pressure = static_cast<float>(init_press);
        pkt.temperature = static_cast<float>(init_temp);
        logger.log(pkt, false, 0);
        
        sendJson(current_time_ms, 0, init_temp, init_press, init_accel, 0.0, 0.0, 0.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    auto now = std::chrono::steady_clock::now();
    base_time_ms = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - system_start_time).count()) + 5000;
    std::cout << ">>> T-MINUS 5 SECONDS AND COUNTING... <<<" << std::endl;

    while (true) {
        now = std::chrono::steady_clock::now();
        uint32_t current_time_ms = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - system_start_time).count());
        if (current_time_ms >= base_time_ms) break; 
        
        TelemetryPacket pkt{};
        pkt.time = current_time_ms;
        pkt.accel_z = static_cast<float>(init_accel);
        pkt.pressure = static_cast<float>(init_press);
        pkt.temperature = static_cast<float>(init_temp);
        logger.log(pkt, true, base_time_ms);
        
        sendJson(current_time_ms, base_time_ms, init_temp, init_press, init_accel, 0.0, 0.0, 0.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    std::cout << ">>> LIFTOFF! Playing CSV data... <<<" << std::endl;
    auto launch_real_time = std::chrono::steady_clock::now();
    double last_sent_csv_time = -1.0;

    while (std::getline(file, line)) {
        std::stringstream row_ss(line);
        std::vector<std::string> row;
        while (std::getline(row_ss, col, ',')) row.push_back(col);
        if (row.size() <= static_cast<size_t>(std::max({idx_time, idx_press, idx_temp, idx_accel}))) continue;

        try {
            double t_sec = std::stod(row[idx_time]);
            double press = (format == CsvFormat::PROLOGUE) ? std::stod(row[idx_press]) / 100.0 : std::stod(row[idx_press]);
            double temp = std::stod(row[idx_temp]);
            double accel = std::stod(row[idx_accel]);

            if (t_sec - last_sent_csv_time < 0.02) continue; 
            last_sent_csv_time = t_sec;

            auto t_now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration<double>(t_now - launch_real_time).count();
            if (t_sec > elapsed) std::this_thread::sleep_for(std::chrono::duration<double>(t_sec - elapsed));

            uint32_t current_time_ms = base_time_ms + static_cast<uint32_t>(t_sec * 1000);
            
            TelemetryPacket pkt{};
            pkt.time = current_time_ms;
            pkt.accel_z = static_cast<float>(accel);
            pkt.pressure = static_cast<float>(press);
            pkt.temperature = static_cast<float>(temp);
            logger.log(pkt, true, base_time_ms);
            
            sendJson(current_time_ms, base_time_ms, temp, press, accel, 0.0, 0.0, 0.0);
        } catch (...) {}
    }
}