#include "simulator/SimulationCore.hpp"
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

// =====================================================================
// CsvSimulator の実装
// =====================================================================
CsvSimulator::CsvSimulator(AppSetting& cfg, CommandListener& c, asio::io_context& io, asio::ip::udp::socket& sock, asio::ip::udp::endpoint& ep)
    : config(cfg), cmd(c), io_context(io), send_socket(sock), send_endpoint(ep) {}

void CsvSimulator::sendJson(uint32_t t, uint32_t bt, double temp, double press, double az, double gx, double gy, double gz) {
    json j;
    j["time"] = t; j["base_time"] = bt; j["temp"] = temp; j["press"] = press;
    j["accel_z"] = az; j["gyro_x"] = gx; j["gyro_y"] = gy; j["gyro_z"] = gz;
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
        if (headers[i] == h_time) idx_time = i;
        else if (headers[i] == h_press) idx_press = i;
        else if (headers[i] == h_temp) idx_temp = i;
        else if (headers[i] == h_accel) idx_accel = i;
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
        uint32_t current_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - system_start_time).count();
        logger.log(current_time_ms, 0.0f, 0.0f, init_accel, 0.0f, 0.0f, 0.0f, init_press, init_temp, false, 0);
        sendJson(current_time_ms, 0, init_temp, init_press, init_accel, 0.0, 0.0, 0.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    auto now = std::chrono::steady_clock::now();
    base_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - system_start_time).count() + 5000;
    std::cout << ">>> T-MINUS 5 SECONDS AND COUNTING... <<<" << std::endl;

    while (true) {
        now = std::chrono::steady_clock::now();
        uint32_t current_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - system_start_time).count();
        if (current_time_ms >= base_time_ms) break; 
        logger.log(current_time_ms, 0.0f, 0.0f, init_accel, 0.0f, 0.0f, 0.0f, init_press, init_temp, true, base_time_ms);
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
        if (row.size() <= std::max({idx_time, idx_press, idx_temp, idx_accel})) continue;

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

            uint32_t current_time_ms = base_time_ms + (uint32_t)(t_sec * 1000);
            logger.log(current_time_ms, 0.0f, 0.0f, accel, 0.0f, 0.0f, 0.0f, press, temp, true, base_time_ms);
            sendJson(current_time_ms, base_time_ms, temp, press, accel, 0.0, 0.0, 0.0);
        } catch (...) {}
    }
}

// =====================================================================
// HardwareReceiver の実装
// =====================================================================
HardwareReceiver::HardwareReceiver(AppSetting& cfg, CommandListener& c, asio::io_context& io, asio::ip::udp::socket& sock, asio::ip::udp::endpoint& ep)
    : config(cfg), cmd(c), io_context(io), send_socket(sock), send_endpoint(ep) {}

void HardwareReceiver::run() {
    std::string port_name;
    std::cout << "Enter ESP32 COM port (e.g., COM3, /dev/ttyUSB0): ";
    std::getline(std::cin, port_name);

    TelemetryLogger logger;
    if (logger.open(config, "flight_log")) {
        std::cout << "[INFO] Logging telemetry data to: " << logger.getFilename() << std::endl;
    }

    try {
        asio::serial_port serial(io_context, port_name);
        serial.set_option(asio::serial_port::baud_rate(115200));

        bool is_calibrating = false, is_launched = false, first_packet_received = false;
        int calib_count = 0;
        float sum_gx = 0, sum_gy = 0, sum_gz = 0;
        float offset_gx = 0, offset_gy = 0, offset_gz = 0;
        uint32_t base_time_ms = 0;

        cmd.request_launch = false;
        cmd.request_calibration = false;

        while (true) {
            if (cmd.request_calibration) {
                cmd.request_calibration = false;
                is_calibrating = true;
                calib_count = sum_gx = sum_gy = sum_gz = 0;
            }

            uint8_t b1 = 0, b2 = 0;
            asio::read(serial, asio::buffer(&b1, 1));
            if (b1 != 0xAA) continue;
            asio::read(serial, asio::buffer(&b2, 1));
            if (b2 != 0xBB) continue;

            std::vector<uint8_t> body(sizeof(TelemetryPacket) + 1);
            asio::read(serial, asio::buffer(body.data(), body.size()));

            uint8_t cs = 0;
            for (size_t i = 0; i < sizeof(TelemetryPacket); ++i) cs ^= body[i];
            if (cs != body.back()) continue;

            TelemetryPacket packet;
            std::memcpy(&packet, body.data(), sizeof(TelemetryPacket));

            if (is_calibrating) {
                sum_gx += packet.gyro_x; sum_gy += packet.gyro_y; sum_gz += packet.gyro_z;
                if (++calib_count >= 100) {
                    offset_gx = sum_gx / 100.0f; offset_gy = sum_gy / 100.0f; offset_gz = sum_gz / 100.0f;
                    is_calibrating = false;
                    std::cout << "[SUCCESS] Offsets Calibrated!" << std::endl;
                }
                continue;
            }

            if (!first_packet_received) {
                base_time_ms = packet.time;
                first_packet_received = true;
            }

            if (cmd.request_launch && !is_launched) {
                cmd.request_launch = false; 
                is_launched = true;
                base_time_ms = packet.time;
                std::cout << "\n[LIFTOFF] MANUAL IGNITION TRIGGERED! T-0 set to " << base_time_ms << " ms" << std::endl;
            }

            float calib_gx = packet.gyro_x - offset_gx;
            float calib_gy = packet.gyro_y - offset_gy;
            float calib_gz = packet.gyro_z - offset_gz;

            logger.log(packet.time, packet.accel_x, packet.accel_y, packet.accel_z, 
                       calib_gx, calib_gy, calib_gz, packet.pressure, packet.temperature, 
                       is_launched, base_time_ms);

            json j;
            j["time"] = packet.time; j["base_time"] = base_time_ms;
            j["temp"] = packet.temperature; j["press"] = packet.pressure; j["accel_z"] = packet.accel_z;
            j["gyro_x"] = calib_gx; j["gyro_y"] = calib_gy; j["gyro_z"] = calib_gz;
            send_socket.send_to(asio::buffer(j.dump()), send_endpoint);
        }
    } catch (std::exception& e) {
        std::cerr << "Serial Error: " << e.what() << std::endl;
    }
}