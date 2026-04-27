#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <SensirionI2CScd4x.h>
#include "sensor_data.h"

// ─── SCD40-D wrapper ──────────────────────────────────────────────────────────
// Reads CO₂ concentration (ppm) plus temperature and humidity cross-checks.
// The SCD40 fixed I2C address is 0x62.
// Periodic measurement mode delivers a new sample every 5 seconds.
class SCD40Sensor {
public:
    bool begin();
    bool read();   // Returns true when fresh CO2 data is available
    void fillReading(SensorReading &r);

    bool     isDataReady() const { return data_ready_; }
    uint16_t getCO2()      const { return co2_ppm_; }

private:
    SensirionI2CScd4x scd4x_;

    uint16_t co2_ppm_         = 0;
    float    temperature_     = 0;  // cross-check only – BME688 is primary
    float    humidity_        = 0;  // cross-check only

    bool     data_ready_      = false;
    uint32_t last_read_ms_    = 0;

    static constexpr uint32_t READ_INTERVAL_MS = SCD40_INTERVAL_MS;
};
