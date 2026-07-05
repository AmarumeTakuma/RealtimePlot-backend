#include "app/CommandListener.hpp"
#include <iostream>
#include <thread>
#include <string>
#include <asio.hpp>

void CommandListener::start() {
    std::thread([this]() {
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
        } catch (std::exception& e) {
            std::cerr << "[Warning] Command Listener error: " << e.what() << std::endl;
        }
    }).detach();
}