#include "aqi_calculator.h"

// ─── US EPA PM2.5 24-hour NowCast breakpoints ─────────────────────────────────
static const AQICalculator::Breakpoint kPM25[] = {
    {  0.0f,  12.0f,   0, 50},
    { 12.1f,  35.4f,  51, 100},
    { 35.5f,  55.4f, 101, 150},
    { 55.5f, 150.4f, 151, 200},
    {150.5f, 250.4f, 201, 300},
    {250.5f, 350.4f, 301, 400},
    {350.5f, 500.4f, 401, 500},
};

// ─── US EPA PM10 24-hour breakpoints ─────────────────────────────────────────
static const AQICalculator::Breakpoint kPM10[] = {
    {  0,  54,   0,  50},
    { 55, 154,  51, 100},
    {155, 254, 101, 150},
    {255, 354, 151, 200},
    {355, 424, 201, 300},
    {425, 504, 301, 400},
    {505, 604, 401, 500},
};

float AQICalculator::linearInterp(float c, const Breakpoint *bp, uint8_t n) {
    for (uint8_t i = 0; i < n; i++) {
        if (c <= bp[i].c_hi) {
            float ratio = (c - bp[i].c_lo) / (bp[i].c_hi - bp[i].c_lo);
            return bp[i].i_lo + ratio * (bp[i].i_hi - bp[i].i_lo);
        }
    }
    return 500.0f; // Beyond highest breakpoint
}

float AQICalculator::pm25ToAQI(float pm25) {
    pm25 = constrain(pm25, 0.0f, 500.4f);
    return linearInterp(pm25, kPM25, sizeof(kPM25) / sizeof(kPM25[0]));
}

float AQICalculator::pm10ToAQI(float pm10) {
    pm10 = constrain(pm10, 0.0f, 604.0f);
    return linearInterp(pm10, kPM10, sizeof(kPM10) / sizeof(kPM10[0]));
}

// CO₂ index: map ppm → 0–500 scale
// 400 ppm (ambient) = 0, 5000 ppm (immediately harmful) = 500
float AQICalculator::co2ToIndex(uint16_t co2_ppm) {
    if (co2_ppm <= 400)  return 0.0f;
    if (co2_ppm >= 5000) return 500.0f;
    return (co2_ppm - 400.0f) / (5000.0f - 400.0f) * 500.0f;
}

// Sensirion VOC index: 1–500 where 100 = typical outdoor air.
// Map to AQI scale: 100 → ~25 AQI (Good), 250 → ~150 (USG), 400+ → 300+ (Hazardous)
float AQICalculator::vocToIndex(float voc_index) {
    voc_index = constrain(voc_index, 1.0f, 500.0f);
    // Piecewise map: [1,100]→[0,50], [100,250]→[50,150], [250,400]→[150,300], [400,500]→[300,500]
    if (voc_index <= 100)  return (voc_index -   1.0f) / 99.0f  *  50.0f;
    if (voc_index <= 250)  return 50.0f  + (voc_index - 100.0f) / 150.0f * 100.0f;
    if (voc_index <= 400)  return 150.0f + (voc_index - 250.0f) / 150.0f * 150.0f;
    return               300.0f + (voc_index - 400.0f) / 100.0f * 200.0f;
}

void AQICalculator::calculate(SensorReading &r) {
    float aqi_pm25 = pm25ToAQI(r.pm2_5);
    float aqi_pm10 = pm10ToAQI(r.pm10);
    float aqi_co2  = co2ToIndex(r.co2_ppm);
    float aqi_voc  = vocToIndex(r.voc_index);

    // Weighted composite (PM2.5 is the dominant pollutant)
    r.aqi_composite = 0.50f * aqi_pm25 +
                      0.25f * aqi_pm10 +
                      0.15f * aqi_co2  +
                      0.10f * aqi_voc;

    r.aqi_composite = constrain(r.aqi_composite, 0.0f, 500.0f);
}
