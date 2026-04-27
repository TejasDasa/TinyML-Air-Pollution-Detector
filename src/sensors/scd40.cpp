#include "scd40.h"

bool SCD40Sensor::begin() {
    scd4x_.begin(Wire);

    // Stop any ongoing measurement first (required before reconfiguration)
    uint16_t err = scd4x_.stopPeriodicMeasurement();
    if (err) {
        Serial.printf("[SCD40] Stop failed: %u (may be first boot)\n", err);
    }
    delay(500);

    err = scd4x_.startPeriodicMeasurement();
    if (err) {
        Serial.printf("[SCD40] Start periodic measurement failed: %u\n", err);
        return false;
    }

    Serial.println("[SCD40] Ready (first reading in ~5 s)");
    return true;
}

bool SCD40Sensor::read() {
    if (millis() - last_read_ms_ < READ_INTERVAL_MS) return false;
    last_read_ms_ = millis();

    bool data_ready = false;
    uint16_t err = scd4x_.getDataReadyFlag(data_ready);
    if (err || !data_ready) return false;

    err = scd4x_.readMeasurement(co2_ppm_, temperature_, humidity_);
    if (err) {
        Serial.printf("[SCD40] Read error: %u\n", err);
        return false;
    }

    // SCD40 returns 0 ppm when sensor is still initialising
    if (co2_ppm_ == 0) return false;

    data_ready_ = true;
    return true;
}

void SCD40Sensor::fillReading(SensorReading &r) {
    r.co2_ppm   = co2_ppm_;
    data_ready_ = false;
}
