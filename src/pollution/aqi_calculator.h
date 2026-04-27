#pragma once
#include <Arduino.h>
#include "sensors/sensor_data.h"

// ─── AQI Calculator ───────────────────────────────────────────────────────────
// Computes a composite Air Quality Index using:
//   - PM2.5 AQI  (US EPA NowCast breakpoints, weighted 50%)
//   - PM10  AQI  (US EPA breakpoints, weighted 25%)
//   - CO2 sub-index  (ASHRAE / RESET thresholds, weighted 15%)
//   - VOC sub-index  (Sensirion VOC index mapped to 0–500, weighted 10%)
//
// Output: AQI 0–500  (same scale as US EPA AQI)
class AQICalculator {
public:
    // Compute composite AQI and fill r.aqi_composite
    static void calculate(SensorReading &r);

    // Sub-index helpers (public for unit testing)
    static float pm25ToAQI(float pm25);
    static float pm10ToAQI(float pm10);
    static float co2ToIndex(uint16_t co2_ppm);  // 0–500
    static float vocToIndex(float voc_index);   // Sensirion 1–500 → AQI 0–500

private:
    struct Breakpoint {
        float c_lo, c_hi;
        float i_lo, i_hi;
    };

    static float linearInterp(float c, const Breakpoint *bp, uint8_t n);
};
