#include <Arduino.h>

// PC側と完全に一致させる構造体
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

void setup() {
  Serial.begin(115200);
  while (!Serial) { ; }
}

void loop() {
  float t = millis() / 1000.0;

  // 1. 構造体にダミーデータを詰め込む
  TelemetryPacket packet;
  packet.time = millis();
  packet.accel_x = 0.1 * sin(t);
  packet.accel_y = 0.2 * cos(t);
  packet.accel_z = 9.81 + 2.0 * sin(t * 2.0); // 擬似重力+推力
  packet.gyro_x = 0.01 * sin(t);
  packet.gyro_y = 0.02 * cos(t);
  packet.gyro_z = 0.5 * sin(t);               // 擬似ロール回転
  packet.mag_x = 30.0;
  packet.mag_y = 10.0;
  packet.mag_z = -40.0;
  packet.pressure = 1013.25 - (t * 0.1);      // 気圧低下（上昇）
  packet.temperature = 23.5 + 0.5 * sin(t);   // 擬似温度データ

  // 2. チェックサム（XOR）を計算
  uint8_t* packet_bytes = (uint8_t*)&packet;
  uint8_t checksum = 0;
  for (size_t i = 0; i < sizeof(TelemetryPacket); i++) {
    checksum ^= packet_bytes[i];
  }

  // 3. シリアル（USB）へバイナリとして一気にぶっ放す！
  Serial.write(0xAA); // ヘッダー1
  Serial.write(0xBB); // ヘッダー2
  Serial.write(packet_bytes, sizeof(TelemetryPacket)); // データ本体（48バイト）
  Serial.write(checksum); // チェックサム1バイト

  delay(100); // 10Hz（100ms間隔）で送信
}