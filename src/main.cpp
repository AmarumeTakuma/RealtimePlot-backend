#include <iostream>
#include <string>
#include <vector>
#include <numeric>
#include <thread>
#include <chrono>
#include <atomic>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <iomanip> // 日時フォーマット用
#include <asio.hpp>

namespace fs = std::filesystem;

#pragma pack(push, 1)
struct TelemetryPacket {
    uint32_t time;
    float accel_x, accel_y, accel_z;
    float gyro_x, gyro_y, gyro_z;
    float mag_x, mag_y, mag_z;
    float pressure;
    float temperature; 
};
#pragma pack(pop)

// ==========================================
// 【共通化】テレメトリデータ・ロガークラス
// ==========================================
class TelemetryLogger {
public:
    bool open(const std::string& prefix) {
        if (!fs::exists("logs")) {
            fs::create_directory("logs");
        }

        // 現在時刻からファイル名を自動生成
        auto now_time = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now_time);
        std::stringstream ss_time;
        ss_time << std::put_time(std::localtime(&now_c), "%Y%m%d_%H%M%S");
        
        filename_ = "logs/" + prefix + "_" + ss_time.str() + ".csv";
        file_.open(filename_);

        if (file_.is_open()) {
            // ヘッダーを書き込み
            file_ << "time_ms,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,pressure,temperature,is_launched,base_time_ms\n";
            return true;
        }
        return false;
    }

    void log(uint32_t time_ms, float ax, float ay, float az, float gx, float gy, float gz, float press, float temp, bool is_launched, uint32_t base_time_ms) {
        if (file_.is_open()) {
            file_ << time_ms << ","
                  << ax << "," << ay << "," << az << ","
                  << gx << "," << gy << "," << gz << ","
                  << press << ","
                  << temp << ","
                  << (is_launched ? 1 : 0) << ","
                  << base_time_ms << "\n";
            file_.flush(); // 毎行強制書き込み
        }
    }

    std::string getFilename() const { return filename_; }

private:
    std::ofstream file_;
    std::string filename_;
};

std::atomic<bool> request_calibration(false);
std::atomic<bool> request_launch(false);

// コマンド受信スレッド
void commandListenerThread() {
    try {
        asio::io_context io_context;
        asio::ip::udp::socket socket(io_context, asio::ip::udp::endpoint(asio::ip::udp::v4(), 51601));
        char recv_buffer[1024];

        while (true) {
            asio::ip::udp::endpoint sender_endpoint;
            size_t len = socket.receive_from(asio::buffer(recv_buffer), sender_endpoint);
            std::string command(recv_buffer, len);
            
            if (command == "CMD_CALIBRATE") {
                std::cout << "\n[COMMAND] Calibration Requested!" << std::endl;
                request_calibration = true; 
            } else if (command == "CMD_LAUNCH") {
                std::cout << "\n[COMMAND] LAUNCH SIGNAL RECEIVED!" << std::endl;
                request_launch = true; 
            }
        }
    } catch (std::exception& e) {}
}

// CSVファイル選択ヘルパー
std::string selectCsvFile(const std::string& target_dir) {
    if (!fs::exists(target_dir)) {
        std::cout << "\n[ERROR] Directory not found: " << fs::absolute(target_dir).string() << std::endl;
        return "";
    }
    std::vector<std::string> csv_files;
    for (const auto& entry : fs::directory_iterator(target_dir)) {
        if (entry.path().extension() == ".csv") csv_files.push_back(entry.path().string());
    }
    if (csv_files.empty()) return "";

    std::cout << "\n--- Available CSV files in " << target_dir << " ---" << std::endl;
    for (size_t i = 0; i < csv_files.size(); ++i) {
        std::cout << "[" << i + 1 << "] " << fs::path(csv_files[i]).filename().string() << std::endl;
    }
    std::cout << "Select a file (1-" << csv_files.size() << "): ";
    std::string choice;
    std::getline(std::cin, choice);
    try {
        int idx = std::stoi(choice) - 1;
        if (idx >= 0 && idx < csv_files.size()) return csv_files[idx];
    } catch (...) {}
    return "";
}

enum class CsvFormat { PROLOGUE, CUSTOM };

// ==========================================
// CSVダミーモード（共通ロガー使用）
// ==========================================
void runCsvDummyMode(asio::io_context& io_context, asio::ip::udp::socket& send_socket, asio::ip::udp::endpoint& send_endpoint, const std::string& filename, CsvFormat format) {
    std::ifstream file(filename);
    if (!file.is_open()) return;

    std::string line;
    std::getline(file, line); 
    std::stringstream ss(line);
    std::string col;
    std::vector<std::string> headers;
    while (std::getline(ss, col, ',')) {
        if (!col.empty() && col.back() == '\r') col.pop_back();
        headers.push_back(col);
    }

    std::string h_time, h_press, h_temp, h_accel;
    if (format == CsvFormat::PROLOGUE) {
        h_time = "time_from_launch[s]"; h_press = "pressure[Pa]";
        h_temp = "temperature[C]"; h_accel = "longitudinal accel[m/s2]";
    } else {
        h_time = "time"; h_press = "pressure";
        h_temp = "temperature"; h_accel = "accel_z";
    }

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

    // ロガーの初期化
    TelemetryLogger logger;
    if (logger.open("sim_log")) {
        std::cout << "[INFO] Logging simulation data to: " << logger.getFilename() << std::endl;
    }

    auto system_start_time = std::chrono::steady_clock::now();
    request_launch = false;
    uint32_t base_time_ms = 0; 

    std::cout << "--- HILS Standby: Waiting for CMD_LAUNCH... ---" << std::endl;

    // 【待機フェーズ】
    while (!request_launch) {
        auto now = std::chrono::steady_clock::now();
        uint32_t current_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - system_start_time).count();

        // 共通ロガーで保存
        logger.log(current_time_ms, 0.0f, 0.0f, init_accel, 0.0f, 0.0f, 0.0f, init_press, init_temp, false, 0);

        std::string json_str = "{";
        json_str += "\"time\":" + std::to_string(current_time_ms) + ",";
        json_str += "\"base_time\":0,"; 
        json_str += "\"temp\":" + std::to_string(init_temp) + ",";
        json_str += "\"press\":" + std::to_string(init_press) + ",";
        json_str += "\"accel_z\":" + std::to_string(init_accel) + ",";
        json_str += "\"gyro_x\":0.0,\"gyro_y\":0.0,\"gyro_z\":0.0";
        json_str += "}";

        send_socket.send_to(asio::buffer(json_str), send_endpoint);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // 【カウントダウンフェーズ】
    auto now = std::chrono::steady_clock::now();
    base_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - system_start_time).count() + 5000;
    
    std::cout << ">>> T-MINUS 5 SECONDS AND COUNTING... <<<" << std::endl;

    while (true) {
        now = std::chrono::steady_clock::now();
        uint32_t current_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - system_start_time).count();
        if (current_time_ms >= base_time_ms) break; 

        // 共通ロガーで保存
        logger.log(current_time_ms, 0.0f, 0.0f, init_accel, 0.0f, 0.0f, 0.0f, init_press, init_temp, true, base_time_ms);

        std::string json_str = "{";
        json_str += "\"time\":" + std::to_string(current_time_ms) + ",";
        json_str += "\"base_time\":" + std::to_string(base_time_ms) + ",";
        json_str += "\"temp\":" + std::to_string(init_temp) + ",";
        json_str += "\"press\":" + std::to_string(init_press) + ",";
        json_str += "\"accel_z\":" + std::to_string(init_accel) + ",";
        json_str += "\"gyro_x\":0.0,\"gyro_y\":0.0,\"gyro_z\":0.0";
        json_str += "}";

        send_socket.send_to(asio::buffer(json_str), send_endpoint);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // 【フライトフェーズ】
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

            // 共通ロガーで保存
            logger.log(current_time_ms, 0.0f, 0.0f, accel, 0.0f, 0.0f, 0.0f, press, temp, true, base_time_ms);

            std::string json_str = "{";
            json_str += "\"time\":" + std::to_string(current_time_ms) + ",";
            json_str += "\"base_time\":" + std::to_string(base_time_ms) + ",";
            json_str += "\"temp\":" + std::to_string(temp) + ",";
            json_str += "\"press\":" + std::to_string(press) + ",";
            json_str += "\"accel_z\":" + std::to_string(accel) + ",";
            json_str += "\"gyro_x\":0.0,\"gyro_y\":0.0,\"gyro_z\":0.0";
            json_str += "}";

            send_socket.send_to(asio::buffer(json_str), send_endpoint);

        } catch (...) {}
    }
}

// ==========================================
// 実機通信モード（共通ロガー使用）
// ==========================================
void runSerialMode(asio::io_context& io_context, asio::ip::udp::socket& send_socket, asio::ip::udp::endpoint& send_endpoint) {
    std::string port_name;
    std::cout << "Enter ESP32 COM port (e.g., COM3, /dev/ttyUSB0): ";
    std::getline(std::cin, port_name);

    // ロガーの初期化
    TelemetryLogger logger;
    if (logger.open("flight_log")) {
        std::cout << "[INFO] Logging telemetry data to: " << logger.getFilename() << std::endl;
    } else {
        std::cerr << "[WARNING] Failed to create log file!" << std::endl;
    }

    try {
        asio::serial_port serial(io_context, port_name);
        serial.set_option(asio::serial_port::baud_rate(115200));

        bool is_calibrating = false;
        int calib_count = 0;
        float sum_gx = 0, sum_gy = 0, sum_gz = 0;
        float offset_gx = 0, offset_gy = 0, offset_gz = 0;

        bool is_launched = false;
        uint32_t base_time_ms = 0;
        bool first_packet_received = false;

        request_launch = false;
        request_calibration = false;

        while (true) {
            if (request_calibration) {
                request_calibration = false;
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

            if (request_launch) {
                request_launch = false; 
                if (!is_launched) { // 1回きりのロック機構（元に戻しました）
                    is_launched = true;
                    base_time_ms = packet.time;
                    std::cout << "\n[LIFTOFF] MANUAL IGNITION TRIGGERED! T-0 set to " << base_time_ms << " ms" << std::endl;
                }
            }

            float calib_gx = packet.gyro_x - offset_gx;
            float calib_gy = packet.gyro_y - offset_gy;
            float calib_gz = packet.gyro_z - offset_gz;

            // 共通ロガーで保存
            logger.log(packet.time, packet.accel_x, packet.accel_y, packet.accel_z, 
                       calib_gx, calib_gy, calib_gz, packet.pressure, packet.temperature, 
                       is_launched, base_time_ms);

            // PythonへのJSON送信
            std::string json_str = "{";
            json_str += "\"time\":" + std::to_string(packet.time) + ",";
            json_str += "\"base_time\":" + std::to_string(base_time_ms) + ",";
            json_str += "\"temp\":" + std::to_string(packet.temperature) + ",";
            json_str += "\"press\":" + std::to_string(packet.pressure) + ",";
            json_str += "\"accel_z\":" + std::to_string(packet.accel_z) + ",";
            json_str += "\"gyro_x\":" + std::to_string(calib_gx) + ",";
            json_str += "\"gyro_y\":" + std::to_string(calib_gy) + ",";
            json_str += "\"gyro_z\":" + std::to_string(calib_gz);
            json_str += "}";

            send_socket.send_to(asio::buffer(json_str), send_endpoint);
        }
    } catch (std::exception& e) {
        std::cerr << "Serial Error: " << e.what() << std::endl;
    }
}

int main() {
    std::thread listener(commandListenerThread);
    listener.detach(); 

    asio::io_context io_context;
    asio::ip::udp::socket send_socket(io_context, asio::ip::udp::endpoint(asio::ip::udp::v4(), 0));
    asio::ip::udp::endpoint send_endpoint(asio::ip::address::from_string("127.0.0.1"), 51600);

    std::cout << "Select Mode:\n1: Serial Mode (Real ESP32)\n2: CSV Simulation (Prologue)\n3: CSV Simulation (Custom)\nChoice (1-3): ";
    std::string choice;
    std::getline(std::cin, choice);

    if (choice == "2") {
        std::string file = selectCsvFile("dummy_data/prologue");
        if (!file.empty()) runCsvDummyMode(io_context, send_socket, send_endpoint, file, CsvFormat::PROLOGUE);
    } else if (choice == "3") {
        std::string file = selectCsvFile("dummy_data/custom");
        if (!file.empty()) runCsvDummyMode(io_context, send_socket, send_endpoint, file, CsvFormat::CUSTOM);
    } else if (choice == "1") {
        runSerialMode(io_context, send_socket, send_endpoint);
    }
    return 0;
}