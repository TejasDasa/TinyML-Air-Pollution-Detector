#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <SensirionI2CSen5x.h>
#include "sensor_data.h"

// ─── SEN50-SDN-T wrapper ──────────────────────────────────────────────────────
// Reads PM1.0 / PM2.5 / PM4.0 / PM10, VOC index, and NOx index.
// The SEN50 fixed I2C address is 0x69.
class SEN50Sensor {
public:
    bool begin();
    bool read();   // Returns true when fresh data is available
    void fillReading(SensorReading &r);

    bool isDataReady() const { return data_ready_; }

private:
    SensirionI2CSen5x sen5x_;

    float pm1_0_    = 0;
    float pm2_5_    = 0;
    float pm4_0_    = 0;
    float pm10_     = 0;
    float voc_idx_  = 0;
    float nox_idx_  = 0;

    bool     data_ready_    = false;
    uint32_t last_read_ms_  = 0;

    static constexpr uint32_t READ_INTERVAL_MS = SEN50_INTERVAL_MS;
};
