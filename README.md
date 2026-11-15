# 🛰️ Smart CubeSat Communication Testbed

**Project Group:** 23BEC061 – 23BEC075

---

## 1. Overview

This project implements a **Smart CubeSat Communication Testbed** using the **ESP-NOW** protocol for peer-to-peer telemetry between two ESP32 microcontrollers. One ESP32 acts as a miniature satellite (CubeSat), reading environmental and attitude sensors plus battery voltage and transmitting telemetry at 2 Hz over 2.4 GHz. The second ESP32 acts as the Ground Station, receiving packets and printing JSON to Serial for PC analysis.

A full **browser-based interactive simulator** (plain HTML + CSS + Vanilla JS, Three.js, Chart.js) lets you visualise and experiment with the system without any hardware.

### Key Features

| Feature | Details |
|---------|---------|
| **Protocol** | ESP-NOW — ultra-fast, connectionless, no Wi-Fi router required |
| **Sensors** | BME280 (temp / pressure / humidity) · MPU6050 (6-axis IMU) |
| **Battery** | LiPo monitor via 10 kΩ + 10 kΩ resistor divider → GPIO 34 ADC |
| **3D Viz** | Live Three.js CubeSat model controlled by simulated attitude |
| **Charts** | Real-time Chart.js graphs for environment, battery & link quality |
| **Packet Inspector** | Scrolling terminal showing ESP-NOW C-struct payloads |
| **Circuit Diagram** | Interactive SVG schematic with hover-highlight pins |

---

## 2. System Architecture

```
┌──────────────────────────────────────┐          ┌──────────────────────────┐
│          ESP32 #1  (CubeSat)         │  ESP-NOW │    ESP32 #2  (Ground     │
│                                      │ 2.4 GHz  │       Station)           │
│  ┌────────┐  ┌────────┐             │◄────────►│                          │
│  │ BME280 │  │MPU6050 │  I2C bus    │ P2P link │   USB-C ──► PC Serial    │
│  │ 0x76   │  │ 0x68   │──►GPIO21/22│ ≤250 B   │   115200 baud JSON       │
│  └────────┘  └────────┘             │          └──────────────────────────┘
│                                      │
│  ┌──────────────────┐                │
│  │ 10k+10k V-div    │──► GPIO 34    │
│  │ Battery monitor   │   (ADC1_CH6) │
│  └──────────────────┘                │
│                                      │
│  Power Bank 5 V ──► VIN             │
└──────────────────────────────────────┘
```

---

## 3. Bill of Materials (BOM)

| # | Component | Qty | Specification | Purpose |
|---|-----------|-----|---------------|---------|
| 1 | ESP32 DevKit V1 | 2 | Dual-core 240 MHz, Wi-Fi + BT, 520 KB SRAM | CubeSat node + Ground station |
| 2 | BME280 breakout | 1 | I2C, ±1 °C / ±1 hPa / ±3 %RH | Environmental telemetry |
| 3 | MPU6050 breakout | 1 | I2C, 3-axis accel + 3-axis gyro | Attitude determination |
| 4 | 10 kΩ resistor | 2 | ¼ W, ±5 % | Battery voltage divider |
| 5 | Breadboard (830-pt) | 2 | Full size | Prototyping |
| 6 | Jumper wires | ~15 | M-M / M-F assorted | Connections |
| 7 | USB-C data cable | 2 | USB 2.0+ | Programming & power |
| 8 | Power bank | 1 | 5 V / 1 A output | CubeSat node power |

---

## 4. Circuit Connections

### 4.1 ESP32 #1 — CubeSat Node

| Component | Component Pin | ESP32 Pin | Wire Colour |
|-----------|--------------|-----------|-------------|
| BME280 | SDA | GPIO 21 | Blue |
| BME280 | SCL | GPIO 22 | Yellow |
| BME280 | VCC | 3.3 V | Red |
| BME280 | GND | GND | Black |
| MPU6050 | SDA | GPIO 21 (shared I2C bus) | Blue |
| MPU6050 | SCL | GPIO 22 (shared I2C bus) | Yellow |
| MPU6050 | VCC | 3.3 V | Red |
| MPU6050 | GND | GND | Black |
| R1 (10 kΩ) | Terminal A | V_BAT+ (power bank 5 V) | Orange |
| R1 (10 kΩ) | Terminal B | Junction (→ R2 + GPIO 34) | — |
| R2 (10 kΩ) | Terminal A | Junction | — |
| R2 (10 kΩ) | Terminal B | GND | Black |
| Divider mid-point | — | GPIO 34 (ADC) | Green |
| Power bank | 5 V | VIN | Red |
| Power bank | GND | GND | Black |

> **Note:** BME280 (addr 0x76) and MPU6050 (addr 0x68) share the same I2C bus on GPIO 21/22.

### 4.2 ESP32 #2 — Ground Station

| Connection | Detail |
|-----------|--------|
| USB-C | Plugged into the PC; provides power and Serial data |
| Serial | 115200 baud, JSON output via Arduino Serial Monitor |

---

## 5. Firmware

### 5.1 `firmware/CubeSat_Transmitter_ESPNow.ino`

* Initialises BME280, MPU6050, ADC on GPIO 34
* Reads all sensors every 500 ms (2 Hz)
* Packs data into `struct_message` (71 bytes, packed)
* Sends via `esp_now_send()` with delivery callback
* Dumps JSON telemetry to Serial for debugging

### 5.2 `firmware/GroundStation_Receiver_ESPNow.ino`

* Registers an ESP-NOW receive callback
* Parses `struct_message`, tracks packet-ID gaps for loss estimation
* Outputs comprehensive JSON (telemetry + link stats) at 115200 baud
* Sends a heartbeat every 10 s with cumulative PDR %

### Arduino IDE Setup

1. **Board:** ESP32 Dev Module
2. **Libraries required (install via Library Manager):**
   * `Adafruit BME280 Library`
   * `Adafruit MPU6050`
   * `Adafruit Unified Sensor`
3. Flash **CubeSat_Transmitter_ESPNow.ino** to ESP32 #1
4. Flash **GroundStation_Receiver_ESPNow.ino** to ESP32 #2
5. Note each board's MAC address from boot JSON, then set `groundStationMAC[]` in the transmitter sketch and re-flash.

---

## 6. ESP-NOW Packet Structure

```c
typedef struct __attribute__((packed)) struct_message {
  uint32_t packetID;              //  4 B  – sequential counter
  uint32_t timestamp;             //  4 B  – millis()
  float    temperature;           //  4 B  – BME280 °C
  float    pressure;              //  4 B  – BME280 hPa
  float    humidity;              //  4 B  – BME280 %RH
  float    altitude;              //  4 B  – BME280 m
  float    accelX, accelY, accelZ;//  12 B – MPU6050 m/s²
  float    gyroX, gyroY, gyroZ;   //  12 B – MPU6050 rad/s
  float    roll, pitch, yaw;      //  12 B – degrees
  float    batteryVoltage;        //  4 B  – V (divider × 2)
  uint16_t batteryADC;            //  2 B  – raw 12-bit ADC
  uint8_t  sensorStatus;          //  1 B  – bit flags
};                                // Total: 71 bytes  (limit: 250)
```

---

## 7. Web Simulation — How It Models the Hardware

Open **`index.html`** in any modern browser (Chrome recommended). No npm install needed — Chart.js and Three.js load from CDN.

### 7.1 RF Link Model

The simulator uses the **Free-Space Path Loss** equation at 2.4 GHz:

```
FSPL (dB)  = 20·log₁₀(d_m) + 20·log₁₀(2.4 × 10⁹) + 20·log₁₀(4π / c)
           ≈ 40 + 20·log₁₀(d)

RSSI (dBm) = Tx_Power + Antenna_Gain − FSPL − Noise_Margin
           = 20 + 4 − 40 − 20·log₁₀(d) − k·NoiseSlider
```

### 7.2 Packet Delivery Ratio (PDR)

PDR is modelled with a sigmoid centred at −75 dBm:

```
PDR (%) = 100 / (1 + e^(0.2 × (−75 − RSSI)))
```

For each simulated packet, a uniform random draw decides delivery vs. drop.

### 7.3 Sensor Simulation

| Sensor | Base value | Slider effect |
|--------|-----------|---------------|
| Temperature | 25 °C | ± anomaly slider + Gaussian σ = 0.3 °C |
| Pressure | 1013.25 hPa | ± anomaly slider + Gaussian σ = 0.5 hPa |
| Humidity | 50 %RH | ± anomaly slider + Gaussian σ = 1 % |
| Roll / Pitch / Yaw | 0° | sinusoidal tumble at slider frequency |
| Battery | slider V | ADC = (V/2) × 4095/3.3 + noise |

### 7.4 Running the Simulation

```bash
# Option A — Python local server
cd Smart_CubeSat_Testbed
python -m http.server 8000
# open http://localhost:8000

# Option B — just double-click index.html
```

---

## 8. Web App Tabs

| Tab | Content |
|-----|---------|
| **1 — Live ESP-NOW Mission Control** | Sliders, 3D CubeSat (Three.js), real-time charts (Chart.js), packet terminal |
| **2 — Interactive Circuit Diagram** | SVG schematic with hover-highlight pins + pinout table |
| **3 — Project Overview & Viva** | Architecture block diagram, packet frame visual, link-budget table, BOM |

---

## 9. Link Budget Summary

| Parameter | Value |
|-----------|-------|
| Tx Power (ESP32 max) | +20 dBm |
| Antenna Gain (PCB, each) | +2 dBi |
| Frequency | 2.4 GHz |
| Rx Sensitivity (ESP32) | −97 dBm |
| **Distance** | **Path Loss** | **RSSI** | **Margin** |
| 1 m | 40 dB | −16 dBm | 81 dB ✅ |
| 10 m | 60 dB | −36 dBm | 61 dB ✅ |
| 50 m | 74 dB | −50 dBm | 47 dB ✅ |
| 100 m | 80 dB | −56 dBm | 41 dB ✅ |
| 200 m | 86 dB | −62 dBm | 35 dB ✅ |
| 500 m | 94 dB | −70 dBm | 27 dB ⚠️ |

---

## 10. License

Developed for academic purposes — Electronics & Communication Engineering curriculum.

---

**Smart CubeSat Communication Testbed** · Group 23BEC061 – 23BEC075
