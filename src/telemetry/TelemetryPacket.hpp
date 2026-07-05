#pragma once

#include <cstdint>

// コンパイラによるパディング（データの隙間空け）を無効化し、隙間なくバイナリを詰める設定
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