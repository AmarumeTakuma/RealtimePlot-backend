#pragma once

#include <string>
#include <nlohmann/json.hpp>

class AppSetting {
public:
    nlohmann::json data;
    std::string config_path;

    AppSetting(const std::string& path);
    bool load();
};