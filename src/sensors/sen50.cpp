#include "sen50.h"

bool SEN50Sensor::begin() {
    sen5x_.begin(Wire);

    uint16_t err = sen5x_.deviceReset();
    if (err) {
        Serial.printf("[SEN50] Reset failed: %u\n", err);
        return false;
    }
    delay(100); // wait for reset

    err = sen5x_.startMeasurement();
    if (err) {
        Serial.printf("[SEN50] Start measurement failed: %u\n", err);
        return false;
    }

    Serial.println("[SEN50] Ready");
    return true;
}

bool SEN50Sensor::read() {
    if (millis() - last_read_ms_ < READ_INTERVAL_MS) return false;
    last_read_ms_ = millis();

    bool data_ready = false;
    uint16_t err = sen5x_.readDataReady(data_ready);
    if (err || !data_ready) return false;

    // Unused outputs from SEN50 (no T/RH on SEN50, only on SEN54/55)
    float ambient_humidity    = NAN;
    float ambient_temperature = NAN;

    err = sen5x_.readMeasuredValues(
        pm1_0_, pm2_5_, pm4_0_, pm10_,
        ambient_humidity, ambient_temperature,
        voc_idx_, nox_idx_);

    if (err) {
        Serial.printf("[SEN50] Read error: %u\n", err);
        return false;
    }

    data_ready_ = true;
    return true;
}

void SEN50Sensor::fillReading(SensorReading &r) {
    r.pm1_0     = pm1_0_;
    r.pm2_5     = pm2_5_;
    r.pm4_0     = pm4_0_;
    r.pm10      = pm10_;
    r.voc_index = voc_idx_;
    r.nox_index = nox_idx_;
    data_ready_ = false;
}
