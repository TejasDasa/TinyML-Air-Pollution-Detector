#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <bsec2.h>
#include "sensor_data.h"

// ─── BME688 wrapper using Bosch BSEC2 library ────────────────────────────────
// Provides heat-compensated temperature/humidity, pressure, and the BSEC2
// Indoor Air Quality (IAQ) index in addition to raw gas resistance.
//
// BSEC2 calibration state is persisted to ESP32 NVS (Preferences) so that
// the IAQ algorithm does not restart cold after every reboot.
class BME688Sensor {
public:
    // Call once in setup() after Wire.begin()
    bool begin(uint8_t i2c_addr = BME68X_I2C_ADDR_LOW);

    // Must be called every loop iteration – BSEC2 is time-driven
    void process();

    // True if BSEC2 has produced a fresh output since last call to getReading()
    bool isDataReady() const { return data_ready_; }

    // Fill the BSEC2-owned fields in a SensorReading struct.
    // Resets the data_ready_ flag.
    void fillReading(SensorReading &r);

    // Load / save BSEC2 state to NVS for calibration continuity
    void loadState();
    void saveState();

    // Raw access for diagnostics
    float getIAQ()          const { return iaq_; }
    uint8_t getIAQAccuracy() const { return iaq_accuracy_; }

private:
    static void bsecCallback(const bme68xData data,
                             const bsecOutputs outputs,
                             Bsec2 bsec);

    static BME688Sensor *instance_;   // singleton for static callback

    Bsec2   bsec_;
    float   temperature_   = 0;
    float   humidity_      = 0;
    float   pressure_      = 0;
    float   iaq_           = 0;
    uint8_t iaq_accuracy_  = 0;
    float   gas_resistance_= 0;
    bool    data_ready_    = false;

    uint32_t last_state_save_ms_ = 0;
    static constexpr uint32_t STATE_SAVE_INTERVAL_MS = 10UL * 60UL * 1000UL; // 10 min
    static constexpr const char* NVS_NAMESPACE = "bsec2";
    static constexpr const char* NVS_KEY       = "state";
};
