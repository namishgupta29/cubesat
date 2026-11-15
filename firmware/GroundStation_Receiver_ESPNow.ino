/*
 * ====================================================================
 *  Smart CubeSat Communication Testbed — Ground Station (Receiver)
 *  ESP32 #2 Firmware
 * ====================================================================
 *  Receives ESP-NOW telemetry packets from the CubeSat node
 *  Prints full JSON to Serial @ 115200 baud for PC consumption
 *  Tracks link statistics: packet count, losses, PDR %
 *
 *  Group: 23BEC061 – 23BEC075
 * ====================================================================
 */

#include <esp_now.h>
#include <WiFi.h>

// ======================== TELEMETRY STRUCT =======================
// Must match the CubeSat transmitter exactly
typedef struct __attribute__((packed)) struct_message {
  uint32_t packetID;
  uint32_t timestamp;

  float temperature;
  float pressure;
  float humidity;
  float altitude;

  float accelX, accelY, accelZ;
  float gyroX,  gyroY,  gyroZ;
  float roll, pitch, yaw;

  float    batteryVoltage;
  uint16_t batteryADC;
  uint8_t  sensorStatus;
} struct_message;

struct_message rxData;

// ======================== LINK STATISTICS ========================
uint32_t packetsReceived = 0;
uint32_t lastPacketID    = 0;
uint32_t packetsLost     = 0;
bool     firstPacket     = true;

// ======================== CALLBACK ==============================
void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if ((size_t)len < sizeof(rxData)) return;          // sanity check
  memcpy(&rxData, data, sizeof(rxData));
  packetsReceived++;

  // Detect gaps in packet IDs → estimate losses
  if (!firstPacket && rxData.packetID > lastPacketID + 1) {
    packetsLost += (rxData.packetID - lastPacketID - 1);
  }
  firstPacket   = false;
  lastPacketID  = rxData.packetID;

  float pdr = (float)packetsReceived /
              (float)(packetsReceived + packetsLost) * 100.0f;

  // Sender MAC string
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  // ---- Full JSON line ----
  Serial.printf("{\"event\":\"RX\","
    "\"from\":\"%s\","
    "\"id\":%u,\"ts\":%u,\"size\":%d,"
    "\"bme\":{\"t\":%.2f,\"p\":%.2f,\"h\":%.2f,\"alt\":%.2f},"
    "\"mpu\":{\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,"
    "\"gx\":%.3f,\"gy\":%.3f,\"gz\":%.3f,"
    "\"r\":%.2f,\"p\":%.2f,\"y\":%.2f},"
    "\"bat\":{\"v\":%.3f,\"adc\":%u},"
    "\"link\":{\"rxCount\":%u,\"lost\":%u,\"pdr\":%.1f},"
    "\"sensors\":\"0x%02X\"}\n",
    macStr,
    rxData.packetID, rxData.timestamp, len,
    rxData.temperature, rxData.pressure,
    rxData.humidity, rxData.altitude,
    rxData.accelX, rxData.accelY, rxData.accelZ,
    rxData.gyroX, rxData.gyroY, rxData.gyroZ,
    rxData.roll, rxData.pitch, rxData.yaw,
    rxData.batteryVoltage, rxData.batteryADC,
    packetsReceived, packetsLost, pdr,
    rxData.sensorStatus);
}

// ======================== SETUP =================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("{\"event\":\"BOOT\",\"node\":\"GROUND_STATION_RX\",\"version\":\"1.0\"}");

  WiFi.mode(WIFI_STA);
  Serial.printf("{\"event\":\"WIFI_MAC\",\"mac\":\"%s\"}\n",
                WiFi.macAddress().c_str());

  if (esp_now_init() != ESP_OK) {
    Serial.println("{\"event\":\"ESPNOW_INIT\",\"status\":\"FAIL\"}");
    return;
  }
  Serial.println("{\"event\":\"ESPNOW_INIT\",\"status\":\"OK\"}");

  esp_now_register_recv_cb(onDataRecv);
  Serial.println("{\"event\":\"READY\",\"msg\":\"Listening for CubeSat telemetry...\"}");
}

// ======================== LOOP ==================================
void loop() {
  // Periodic heartbeat every 10 s
  static unsigned long lastHB = 0;
  if (millis() - lastHB >= 10000) {
    lastHB = millis();
    float pdr = packetsReceived
        ? (float)packetsReceived / (packetsReceived + packetsLost) * 100.0f
        : 0.0f;
    Serial.printf("{\"event\":\"HEARTBEAT\",\"uptime\":%lu,"
      "\"rxTotal\":%u,\"lost\":%u,\"pdr\":%.1f}\n",
      millis() / 1000, packetsReceived, packetsLost, pdr);
  }
  delay(10);
}
