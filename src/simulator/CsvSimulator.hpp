#pragma once

#include <asio.hpp>
#include <string>

#include "app/AppSetting.hpp"
#include "app/CommandListener.hpp"
#include "telemetry/TelemetryPacket.hpp"

enum class CsvFormat { PROLOGUE, CUSTOM };

class CsvSimulator {
private:
    AppSetting& config;
    CommandListener& cmd;
    asio::io_context& io_context;
    asio::ip::udp::socket& send_socket;
    asio::ip::udp::endpoint& send_endpoint;

    void sendJson(
        uint32_t t, uint32_t bt, double temp, double press, double az, double gx, double gy, double gz, double alt);

public:
    CsvSimulator(AppSetting& cfg,
                 CommandListener& c,
                 asio::io_context& io,
                 asio::ip::udp::socket& sock,
                 asio::ip::udp::endpoint& ep);
    void run(const std::string& filename, CsvFormat format);
};