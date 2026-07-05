#include "app/AppSetting.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

// コンストラクタの実装
AppSetting::AppSetting(const std::string& path) : config_path(path) {}

// load関数の実装
bool AppSetting::load() {
    if (!fs::exists(config_path)) {
        std::cerr << "[Error] Config file not found: " << config_path << std::endl;
        return false;
    }
    std::ifstream file(config_path);
    if (!file.is_open()) return false;
    
    file >> data;
    std::cout << "[Info] Configuration loaded successfully." << std::endl;
    return true;
}