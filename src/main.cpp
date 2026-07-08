#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// FetchContentで取得したライブラリ
#include <asio.hpp>
#include <nlohmann/json.hpp>

#include "app/AppSetting.hpp"
#include "app/CommandListener.hpp"
#include "simulator/CsvSimulator.hpp"
#include "telemetry/HardwareReceiver.hpp"
#include "telemetry/Logger.hpp"
#include "telemetry/TelemetryPacket.hpp"

using json   = nlohmann::json;
namespace fs = std::filesystem;

const auto VERSION = "1.0.0";

// ==========================================
// 賢いパス解決関数
// ==========================================
fs::path getBasePath() {
    // 1. exeと同じ階層に input フォルダがある場合（リリース版として実行）
    if (fs::exists("input")) {
        return fs::current_path();
    }
    // 2. １つ下に application フォルダがある場合（VSCodeから実行）
    if (fs::exists("application/input")) {
        return fs::current_path() / "application";
    }
    // どちらでもない場合はとりあえずカレントを返す
    return fs::current_path();
}

// =====================================================================
// ヘルパー関数群
// =====================================================================
std::string selectCsvFile(const std::string& target_dir) {
    fs::path dir_path = getBasePath() / target_dir;
    if (!fs::exists(dir_path)) {
        std::cout << "\n[ERROR] Directory not found: " << dir_path.string() << std::endl;
        return "";
    }
    std::vector<std::string> csv_files;
    for (const auto& entry : fs::directory_iterator(dir_path)) {
        if (entry.path().extension() == ".csv")
            csv_files.push_back(entry.path().string());
    }
    if (csv_files.empty())
        return "";

    std::cout << "\n--- Available CSV files in " << target_dir << " ---" << std::endl;
    for (size_t i = 0; i < csv_files.size(); ++i) {
        std::cout << "[" << i + 1 << "] " << fs::path(csv_files[i]).filename().string() << std::endl;
    }
    std::cout << "Select a file (1-" << csv_files.size() << "): ";
    std::string choice;
    std::getline(std::cin, choice);
    try {
        int idx = std::stoi(choice) - 1;
        if (idx >= 0 && idx < csv_files.size())
            return csv_files[idx];
    } catch (...) {
    }
    return "";
}

// =====================================================================
// [src/main.cpp] エントリポイント
// =====================================================================
int main() {
    std::cout << "RealtimePlot Backend v" << VERSION << std::endl;

    // 1. JSON設定のロード
    fs::path config_path = getBasePath() / "input" / "realtimeplot_config.json";
    AppSetting config(config_path.string());
    config.load();  // 失敗してもデフォルト値で動くように続行

    // 2. コマンドリスナーの起動 (JSONからポートを読んで渡す)
    uint16_t recv_port = 51601;  // デフォルト値
    if (config.data.contains("communication") && config.data["communication"].contains("udp_recv_port")) {
        recv_port = config.data["communication"]["udp_recv_port"].get<uint16_t>();
    }
    CommandListener cmd_listener;
    cmd_listener.start(recv_port);  // ポート番号を引数で渡すように変更！

    // 3. ネットワークセットアップ (JSONから送信先IPとポートを読む)
    std::string target_ip = "127.0.0.1";
    uint16_t send_port    = 51600;

    if (config.data.contains("communication")) {
        if (config.data["communication"].contains("udp_send_port")) {
            send_port = config.data["communication"]["udp_send_port"].get<uint16_t>();
        }
        if (config.data["communication"].contains("target_ip")) {
            target_ip = config.data["communication"]["target_ip"].get<std::string>();
        }
    }

    asio::io_context io_context;
    asio::ip::udp::socket send_socket(io_context, asio::ip::udp::endpoint(asio::ip::udp::v4(), 0));
    asio::ip::udp::endpoint send_endpoint(asio::ip::address::from_string(target_ip), send_port);

    // 4. モード選択と実行
    std::cout << "Select Mode:\n1: Serial Mode (Real MCU)\n2: CSV Simulation (Prologue)\n3: CSV Simulation "
                 "(Custom)\nChoice (1-3): ";
    std::string choice;
    std::getline(std::cin, choice);

    if (choice == "2") {
        std::string file = selectCsvFile("dummy_data/prologue");
        if (!file.empty()) {
            CsvSimulator sim(config, cmd_listener, io_context, send_socket, send_endpoint);
            sim.run(file, CsvFormat::PROLOGUE);
        }
    } else if (choice == "3") {
        std::string file = selectCsvFile("dummy_data/custom");
        if (!file.empty()) {
            CsvSimulator sim(config, cmd_listener, io_context, send_socket, send_endpoint);
            sim.run(file, CsvFormat::CUSTOM);
        }
    } else if (choice == "1") {
        HardwareReceiver hw(config, cmd_listener, io_context, send_socket, send_endpoint);
        hw.run();
    }

    // ==========================================
    // プログラムが即終了して画面が消えるのを防ぐ
    // ==========================================
    std::cout << "\n[System] Press Enter to exit..." << std::endl;
    std::string dummy;
    std::getline(std::cin, dummy);

    return 0;
}
