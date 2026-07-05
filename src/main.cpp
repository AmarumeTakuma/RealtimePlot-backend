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
#include <iomanip>

// FetchContentで取得したライブラリ
#include <asio.hpp>
#include <nlohmann/json.hpp>

#include "app/AppSetting.hpp"
#include "app/CommandListener.hpp"
#include "telemetry/Logger.hpp"
#include "telemetry/TelemetryPacket.hpp"
#include "simulator/SimulationCore.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

const auto VERSION = "1.0.0";

// =====================================================================
// ヘルパー関数群
// =====================================================================
std::string selectCsvFile(const std::string& target_dir) {
    fs::path dir_path = fs::current_path() / "application" / target_dir;
    if (!fs::exists(dir_path)) {
        std::cout << "\n[ERROR] Directory not found: " << dir_path.string() << std::endl;
        return "";
    }
    std::vector<std::string> csv_files;
    for (const auto& entry : fs::directory_iterator(dir_path)) {
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

// =====================================================================
// [src/main.cpp] エントリポイント
// =====================================================================
int main() {
    std::cout << "Telemetry Backend v" << VERSION << std::endl;

    // 1. JSON設定のロード
    fs::path config_path = fs::current_path() / "application" / "input" / "telemetry_config.json";
    AppSetting config(config_path.string());
    config.load(); // 失敗してもデフォルト値で動くように続行

    // 2. コマンドリスナーの起動
    CommandListener cmd_listener;
    cmd_listener.start();

    // 3. ネットワークセットアップ
    asio::io_context io_context;
    asio::ip::udp::socket send_socket(io_context, asio::ip::udp::endpoint(asio::ip::udp::v4(), 0));
    asio::ip::udp::endpoint send_endpoint(asio::ip::address::from_string("127.0.0.1"), 51600);

    // 4. モード選択と実行
    std::cout << "Select Mode:\n1: Serial Mode (Real ESP32)\n2: CSV Simulation (Prologue)\n3: CSV Simulation (Custom)\nChoice (1-3): ";
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
    return 0;
}