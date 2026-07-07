#pragma once

#include <string>
#include <asio.hpp>
#include "app/AppSetting.hpp"
#include "app/CommandListener.hpp"
#include "telemetry/TelemetryPacket.hpp"

class HardwareReceiver {
private:
    AppSetting& config;
    CommandListener& cmd;
    asio::io_context& io_context;
    asio::ip::udp::socket& send_socket;
    asio::ip::udp::endpoint& send_endpoint;

public:
    HardwareReceiver(AppSetting& cfg, CommandListener& c, asio::io_context& io, asio::ip::udp::socket& sock, asio::ip::udp::endpoint& ep);
    void run();
};