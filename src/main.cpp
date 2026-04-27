// ─── Air Pollution Detector Firmware ─────────────────────────────────────────
// ESP32 + BME688 (BSEC2) + SEN50-SDN-T + SCD40-D
// TensorFlow Lite Micro inference → BLE → phone
//
// I²C wiring (all sensors share the bus):
//   SDA → GPIO 21   (I2C_SDA)
//   SCL → GPIO 22   (I2C_SCL)
//   3.3 V and GND   → all sensor VDD / GND pins
//
//   BME688  addr 0x76 (SDO to GND)
//   SEN50   addr 0x69 (fixed)
//   SCD40   addr 0x62 (fixed)
//
// Data flow:
//   Sensors → ESP32 → TFLite inference → ExposureTracker → BLE → Phone
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <Wire.h>

#include "sensors/sensor_data.h"
#include "sensors/bme688.h"
#include "sensors/sen50.h"
#include "sensors/scd40.h"
#include "ml/inference.h"
#include "ble/ble_service.h"
#include "pollution/aqi_calculator.h"
#include "pollution/exposure_tracker.h"

// ─── Peripheral instances ─────────────────────────────────────────────────────
static BME688Sensor  bme688;
static SEN50Sensor   sen50;
static SCD40Sensor   scd40;
static InferenceEngine inference;
static BLEAirService ble_service;
static ExposureTracker exposure;

// ─── Shared reading ───────────────────────────────────────────────────────────
static SensorReading reading;

// ─── Timing ───────────────────────────────────────────────────────────────────
static uint32_t last_ble_notify_ms = 0;

// ─── Status LED (onboard LED on most ESP32 dev boards) ────────────────────────
static constexpr uint8_t LED_PIN = 2;

// ─── Helpers ──────────────────────────────────────────────────────────────────
static void handleBLECommand(const String &cmd);
static bool allSensorsReady();

// ═════════════════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n=== Air Pollution Detector v1.0 ===");

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // ── I²C bus ──────────────────────────────────────────────────────────────
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000); // 400 kHz fast mode

    // ── Sensors ───────────────────────────────────────────────────────────────
    bool ok = true;

    if (!bme688.begin(BME68X_I2C_ADDR_LOW)) {
        Serial.println("[INIT] BME688 FAILED – check wiring");
        ok = false;
    }
    if (!sen50.begin()) {
        Serial.println("[INIT] SEN50 FAILED – check wiring");
        ok = false;
    }
    if (!scd40.begin()) {
        Serial.println("[INIT] SCD40 FAILED – check wiring");
        ok = false;
    }

    if (!ok) {
        Serial.println("[INIT] One or more sensors failed – halting. Check I²C wiring.");
        // Blink SOS on LED to signal hardware fault
        while (true) {
            for (int i = 0; i < 3; i++) { digitalWrite(LED_PIN,HIGH); delay(200); digitalWrite(LED_PIN,LOW); delay(200); }
            for (int i = 0; i < 3; i++) { digitalWrite(LED_PIN,HIGH); delay(600); digitalWrite(LED_PIN,LOW); delay(200); }
            for (int i = 0; i < 3; i++) { digitalWrite(LED_PIN,HIGH); delay(200); digitalWrite(LED_PIN,LOW); delay(200); }
            delay(1000);
        }
    }

    // ── Inference engine (TFLite or heuristic) ────────────────────────────────
    inference.begin();
    Serial.printf("[INIT] Inference mode: %s\n",
                  inference.isMLMode() ? "TFLite ML" : "Heuristic");

    // ── Exposure tracker ──────────────────────────────────────────────────────
    exposure.begin();

    // ── BLE ───────────────────────────────────────────────────────────────────
    ble_service.begin();

    // Initialise reading struct
    memset(&reading, 0, sizeof(reading));
    reading.valid = false;

    Serial.println("[INIT] Setup complete – entering sensor loop");
    digitalWrite(LED_PIN, HIGH); // solid LED = running
}

// ═════════════════════════════════════════════════════════════════════════════
void loop() {
    // ── BSEC2 must be called every loop iteration ─────────────────────────────
    bme688.process();

    // ── Poll timed sensors ────────────────────────────────────────────────────
    bool sen50_new = sen50.read();
    bool scd40_new = scd40.read();

    // ── Collect data when all sensors have fresh readings ─────────────────────
    if (bme688.isDataReady() && sen50_new && scd40_new) {
        reading.timestamp_ms = millis();

        bme688.fillReading(reading);
        sen50.fillReading(reading);
        scd40.fillReading(reading);

        // ── Sanity check sensor values ────────────────────────────────────────
        reading.valid =
            (reading.pm2_5      >= 0    && reading.pm2_5    < 1000)  &&
            (reading.co2_ppm    >= 100  && reading.co2_ppm  < 10000) &&
            (reading.temperature >= -20  && reading.temperature < 85)  &&
            (reading.humidity   >= 0    && reading.humidity  <= 100)  &&
            (reading.iaq_accuracy >= 1);  // reject readings while BSEC2 is stabilising

        if (reading.valid) {
            // ── AQI composite ─────────────────────────────────────────────────
            AQICalculator::calculate(reading);

            // ── ML / heuristic inference ──────────────────────────────────────
            inference.predict(reading);

            // ── Exposure accumulator ──────────────────────────────────────────
            exposure.update(reading);
            exposure.fillReading(reading);
        }
    }

    // ── BLE notification at fixed interval ───────────────────────────────────
    if (millis() - last_ble_notify_ms >= BLE_NOTIFY_INTERVAL_MS) {
        last_ble_notify_ms = millis();

        if (reading.valid) {
            ble_service.sendReading(reading);
            ble_service.updateSession(exposure);

            if (Serial.availableForWrite()) {
                Serial.printf(
                    "[DATA] PM2.5=%.1f PM10=%.1f CO2=%u VOC=%.0f IAQ=%.0f(acc%d) "
                    "T=%.1fC RH=%.0f%% | AQS=%.3f RDR=%.3f AQI=%.0f | Exp=%.1f\n",
                    reading.pm2_5, reading.pm10, reading.co2_ppm,
                    reading.voc_index, reading.iaq, reading.iaq_accuracy,
                    reading.temperature, reading.humidity,
                    reading.aqs, reading.rdr, reading.aqi_composite,
                    reading.cumulative_exposure);
            }
        } else {
            // Not yet valid – BSEC2 still stabilising
            Serial.printf("[WAIT] Sensors stabilising... IAQ accuracy=%d\n",
                          reading.iaq_accuracy);
        }
    }

    // ── Process BLE commands from phone ──────────────────────────────────────
    if (ble_service.hasPendingCommand()) {
        handleBLECommand(ble_service.consumeCommand());
    }

    // Small yield to allow BLE stack to process events
    delay(10);
}

// ─── Handle commands sent from the phone app ─────────────────────────────────
// Supported commands (JSON strings written to BLE_CHAR_CONTROL_UUID):
//   {"cmd":"reset_session"}   – clear exposure accumulator and waypoint log
//   {"cmd":"save_bme_state"}  – force-save BSEC2 calibration to NVS
//   {"cmd":"status"}          – print status to Serial
static void handleBLECommand(const String &cmd) {
    if (cmd.indexOf("reset_session") >= 0) {
        exposure.reset();
        Serial.println("[CMD] Session reset");
    } else if (cmd.indexOf("save_bme_state") >= 0) {
        bme688.saveState();
        Serial.println("[CMD] BSEC2 state saved");
    } else if (cmd.indexOf("status") >= 0) {
        Serial.printf("[CMD] Status: connected=%d ml=%d waypoints=%u\n",
                      ble_service.isConnected(),
                      inference.isMLMode(),
                      exposure.waypointCount());
    } else {
        Serial.printf("[CMD] Unknown command: %s\n", cmd.c_str());
    }
}
