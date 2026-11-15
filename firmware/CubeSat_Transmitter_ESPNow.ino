/*
 * ====================================================================
 *  Smart CubeSat Communication Testbed — CubeSat Node (Transmitter)
 *  ESP32 #1 Firmware
 * ====================================================================
 *  Sensors : BME280 (I2C 0x76/0x77), MPU6050 (I2C 0x68)
 *  Battery : 10k+10k voltage divider → GPIO 34 ADC
 *  Protocol: ESP-NOW (2.4 GHz peer-to-peer, max 250 bytes)
 *  Output  : JSON telemetry on Serial @ 115200 baud
 *  Rate    : 2 Hz (500 ms interval)
 *
 *  Group: 23BEC061 – 23BEC075
 * ====================================================================
 */

#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_MPU6050.h>

// ======================== PIN DEFINITIONS ========================
#define I2C_SDA           21
#define I2C_SCL           22
#define BATTERY_ADC_PIN   34        // ADC1_CH6
#define SEALEVELPRESSURE_HPA 1013.25

// ======================== SENSOR OBJECTS =========================
Adafruit_BME280  bme;
Adafruit_MPU6050 mpu;

// ======================== ESP-NOW CONFIG =========================
// *** REPLACE with your Ground-Station ESP32 MAC address ***
uint8_t groundStationMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ======================== TELEMETRY STRUCT =======================
// Total ≈ 71 bytes — well within ESP-NOW's 250-byte limit
typedef struct __attribute__((packed)) struct_message {
  uint32_t packetID;          //  4 B  – sequential counter
  uint32_t timestamp;         //  4 B  – millis()

  // BME280
  float temperature;          //  4 B  – °C
  float pressure;             //  4 B  – hPa
  float humidity;             //  4 B  – %RH
  float altitude;             //  4 B  – m ASL

  // MPU6050
  float accelX, accelY, accelZ;   // 12 B – m/s²
  float gyroX,  gyroY,  gyroZ;   // 12 B – rad/s
  float roll, pitch, yaw;        // 12 B – degrees (complementary filter)

  // Battery
  float    batteryVoltage;    //  4 B  – V (actual, ×2 from divider)
  uint16_t batteryADC;        //  2 B  – raw 12-bit ADC

  // Status flags
  uint8_t  sensorStatus;      //  1 B  – bit 0 = BME, bit 1 = MPU
} struct_message;

struct_message telemetry;
uint32_t packetCounter = 0;

// Complementary-filter state
float cfRoll  = 0.0f;
float cfPitch = 0.0f;
float cfYaw   = 0.0f;
unsigned long lastIMUTime = 0;

// ======================== CALLBACKS =============================
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("{\"event\":\"TX_STATUS\",\"id\":");
  Serial.print(telemetry.packetID);
  Serial.print(",\"status\":\"");
  Serial.print(status == ESP_NOW_SEND_SUCCESS ? "DELIVERED" : "FAILED");
  Serial.println("\"}");
}

// ======================== SENSOR READERS ========================
void readBME280() {
  telemetry.temperature = bme.readTemperature();
  telemetry.pressure    = bme.readPressure() / 100.0f;
  telemetry.humidity    = bme.readHumidity();
  telemetry.altitude    = bme.readAltitude(SEALEVELPRESSURE_HPA);
}

void readMPU6050() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  telemetry.accelX = a.acceleration.x;
  telemetry.accelY = a.acceleration.y;
  telemetry.accelZ = a.acceleration.z;
  telemetry.gyroX  = g.gyro.x;
  telemetry.gyroY  = g.gyro.y;
  telemetry.gyroZ  = g.gyro.z;

  // ---- Complementary filter for Roll / Pitch / Yaw ----
  unsigned long now = millis();
  float dt = (now - lastIMUTime) / 1000.0f;
  if (dt <= 0 || dt > 1.0f) dt = 0.01f;
  lastIMUTime = now;

  float accelRoll  = atan2(a.acceleration.y, a.acceleration.z) * 57.2958f;
  float accelPitch = atan2(-a.acceleration.x,
      sqrtf(a.acceleration.y * a.acceleration.y +
            a.acceleration.z * a.acceleration.z)) * 57.2958f;

  cfRoll  = 0.96f * (cfRoll  + g.gyro.x * dt * 57.2958f) + 0.04f * accelRoll;
  cfPitch = 0.96f * (cfPitch + g.gyro.y * dt * 57.2958f) + 0.04f * accelPitch;
  cfYaw  += g.gyro.z * dt * 57.2958f;              // gyro-only (no magnetometer)

  telemetry.roll  = cfRoll;
  telemetry.pitch = cfPitch;
  telemetry.yaw   = cfYaw;
}

void readBattery() {
  telemetry.batteryADC = analogRead(BATTERY_ADC_PIN);
  //  Vdiv = ADC_raw × 3.3 / 4095       (voltage at divider mid-point)
  //  Vbat = Vdiv × 2                    (10 k + 10 k divider)
  float vDiv = (telemetry.batteryADC / 4095.0f) * 3.3f;
  telemetry.batteryVoltage = vDiv * 2.0f;
}

// ======================== SETUP =================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("{\"event\":\"BOOT\",\"node\":\"CUBESAT_TX\",\"version\":\"1.0\"}");

  // I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  // ---- BME280 ----
  telemetry.sensorStatus = 0;
  if (bme.begin(0x76) || bme.begin(0x77)) {
    telemetry.sensorStatus |= 0x01;
    bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                    Adafruit_BME280::SAMPLING_X2,
                    Adafruit_BME280::SAMPLING_X16,
                    Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::FILTER_X16,
                    Adafruit_BME280::STANDBY_MS_500);
    Serial.println("{\"event\":\"SENSOR_INIT\",\"sensor\":\"BME280\",\"status\":\"OK\"}");
  } else {
    Serial.println("{\"event\":\"SENSOR_INIT\",\"sensor\":\"BME280\",\"status\":\"FAIL\"}");
  }

  // ---- MPU6050 ----
  if (mpu.begin()) {
    telemetry.sensorStatus |= 0x02;
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    Serial.println("{\"event\":\"SENSOR_INIT\",\"sensor\":\"MPU6050\",\"status\":\"OK\"}");
  } else {
    Serial.println("{\"event\":\"SENSOR_INIT\",\"sensor\":\"MPU6050\",\"status\":\"FAIL\"}");
  }

  // ---- ADC ----
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);   // 0-3.3 V full-scale

  // ---- WiFi + ESP-NOW ----
  WiFi.mode(WIFI_STA);
  Serial.printf("{\"event\":\"WIFI_MAC\",\"mac\":\"%s\"}\n", WiFi.macAddress().c_str());

  if (esp_now_init() != ESP_OK) {
    Serial.println("{\"event\":\"ESPNOW_INIT\",\"status\":\"FAIL\"}");
    return;
  }
  Serial.println("{\"event\":\"ESPNOW_INIT\",\"status\":\"OK\"}");
  esp_now_register_send_cb(onDataSent);

  // Register peer
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, groundStationMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("{\"event\":\"PEER_ADD\",\"status\":\"FAIL\"}");
    return;
  }
  Serial.println("{\"event\":\"PEER_ADD\",\"status\":\"OK\"}");

  lastIMUTime = millis();
}

// ======================== LOOP ==================================
void loop() {
  // Read sensors
  if (telemetry.sensorStatus & 0x01) readBME280();
  if (telemetry.sensorStatus & 0x02) readMPU6050();
  readBattery();

  // Metadata
  telemetry.packetID  = packetCounter++;
  telemetry.timestamp = millis();

  // Transmit
  esp_err_t result = esp_now_send(groundStationMAC,
                                  (uint8_t *)&telemetry,
                                  sizeof(telemetry));

  // JSON serial dump
  Serial.printf("{\"event\":\"TELEMETRY\",\"id\":%u,\"ts\":%u,"
    "\"bme\":{\"t\":%.2f,\"p\":%.2f,\"h\":%.2f,\"alt\":%.2f},"
    "\"mpu\":{\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,"
    "\"gx\":%.3f,\"gy\":%.3f,\"gz\":%.3f,"
    "\"r\":%.2f,\"p\":%.2f,\"y\":%.2f},"
    "\"bat\":{\"v\":%.3f,\"adc\":%u},"
    "\"tx\":\"%s\"}\n",
    telemetry.packetID, telemetry.timestamp,
    telemetry.temperature, telemetry.pressure,
    telemetry.humidity, telemetry.altitude,
    telemetry.accelX, telemetry.accelY, telemetry.accelZ,
    telemetry.gyroX, telemetry.gyroY, telemetry.gyroZ,
    telemetry.roll, telemetry.pitch, telemetry.yaw,
    telemetry.batteryVoltage, telemetry.batteryADC,
    (result == ESP_OK) ? "QUEUED" : "ERROR");

  delay(500);   // 2 Hz telemetry rate
}
