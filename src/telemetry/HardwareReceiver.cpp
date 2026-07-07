#include "telemetry/HardwareReceiver.hpp"
#include "telemetry/Logger.hpp"
#include <iostream>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

HardwareReceiver::HardwareReceiver(AppSetting& cfg, CommandListener& c, asio::io_context& io, asio::ip::udp::socket& sock, asio::ip::udp::endpoint& ep)
    : config(cfg), cmd(c), io_context(io), send_socket(sock), send_endpoint(ep) {}

void HardwareReceiver::run() {
    std::string port_name;
    std::cout << "Enter MCU COM port (e.g., COM3, /dev/ttyUSB0): ";
    std::getline(std::cin, port_name);

    // ==========================================
    // 1. JSONから通信設定を動的に読み込む
    // ==========================================
    std::vector<uint8_t> sync_word;
    if (config.data.contains("communication") && config.data["communication"].contains("sync_word")) {
        for (auto& val : config.data["communication"]["sync_word"]) {
            sync_word.push_back(val.get<uint8_t>());
        }
    } else {
        sync_word = {0xAA, 0xBB}; // JSONに無い場合のフォールバック
    }

    std::string checksum_type = "xor";
    if (config.data.contains("communication") && config.data["communication"].contains("checksum")) {
        checksum_type = config.data["communication"]["checksum"].get<std::string>();
    }

    TelemetryLogger logger;
    if (logger.open(config, "flight_log")) {
        std::cout << "[INFO] Logging telemetry data to: " << logger.getFilename() << std::endl;
    }

    try {
        asio::serial_port serial(io_context, port_name);
        // baud_rateもJSONから読めます（省略時は115200）
        uint32_t baud = config.data.contains("communication") ? config.data["communication"]["baud_rate"].get<uint32_t>() : 115200;
        serial.set_option(asio::serial_port::baud_rate(baud));

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

            // ==========================================
            // 2. 動的 Sync Word 待機処理
            // ==========================================
            size_t sync_match_count = 0;
            while (sync_match_count < sync_word.size()) {
                uint8_t b = 0;
                asio::read(serial, asio::buffer(&b, 1));
                if (b == sync_word[sync_match_count]) {
                    sync_match_count++; // 一致したら次のバイトへ
                } else {
                    // 外れたら最初から探し直し（現在のバイトが先頭文字と同じなら1から）
                    sync_match_count = (b == sync_word[0]) ? 1 : 0;
                }
            }

            // ==========================================
            // 3. データ本体の受信とチェックサム検証
            // ==========================================
            // JSONでチェックサムが有効なら +1 バイト多く読む
            size_t body_size = sizeof(TelemetryPacket) + (checksum_type == "xor" ? 1 : 0);
            std::vector<uint8_t> body(body_size);
            asio::read(serial, asio::buffer(body.data(), body.size()));

            if (checksum_type == "xor") {
                uint8_t cs = 0;
                for (size_t i = 0; i < sizeof(TelemetryPacket); ++i) {
                    cs ^= body[i];
                }
                // 送られてきたチェックサム（末尾）と計算結果が合わなければ破棄
                if (cs != body.back()) continue; 
            }

            TelemetryPacket packet;
            std::memcpy(&packet, body.data(), sizeof(TelemetryPacket));

            // ジャイロのキャリブレーション処理 (変更なし)
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

            // キャリブレーション値をパケット構造体に上書きして、そのままLoggerとJSONへ渡す
            packet.gyro_x -= offset_gx;
            packet.gyro_y -= offset_gy;
            packet.gyro_z -= offset_gz;

            // 動的になったLogger呼び出し
            logger.log(packet, is_launched, base_time_ms);

            // UDPで送る直前にPCの絶対時間を取得
            auto now = std::chrono::system_clock::now();
            uint64_t epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

            // ==========================================
            // JSONで定義された項目だけを抽出してUDP送信
            // ==========================================
            json j;
            j["pc_epoch_ms"] = epoch_ms;
            j["base_time"] = base_time_ms; // 必須項目

            if (config.data.contains("payload")) {
                for (const auto& item : config.data["payload"]) {
                    std::string name = item["name"].get<std::string>();
                    
                    if (name == "time_ms") j[name] = packet.time;
                    else if (name == "accel_x") j[name] = packet.accel_x;
                    else if (name == "accel_y") j[name] = packet.accel_y;
                    else if (name == "accel_z") j[name] = packet.accel_z;
                    else if (name == "gyro_x") j[name] = packet.gyro_x;
                    else if (name == "gyro_y") j[name] = packet.gyro_y;
                    else if (name == "gyro_z") j[name] = packet.gyro_z;
                    else if (name == "mag_x") j[name] = packet.mag_x;
                    else if (name == "mag_y") j[name] = packet.mag_y;
                    else if (name == "mag_z") j[name] = packet.mag_z;
                    else if (name == "pressure") j[name] = packet.pressure;
                    else if (name == "temperature") j[name] = packet.temperature;
                }
            }

            // UDP送信
            send_socket.send_to(asio::buffer(j.dump()), send_endpoint);
        }
    } catch (std::exception& e) {
        std::cerr << "Serial Error: " << e.what() << std::endl;
    }
}