#pragma once
#include <stdint.h>

// ─── Aggregated reading from all three sensors ────────────────────────────────
// Populated each measurement cycle and passed through the inference + BLE layer.
struct SensorReading {
    // ── SEN50-SDN-T (particulate + VOC/NOx) ──────────────────────────────────
    float pm1_0;        // μg/m³
    float pm2_5;        // μg/m³
    float pm4_0;        // μg/m³
    float pm10;         // μg/m³
    float voc_index;    // 1–500  (Sensirion VOC index, 100 = typical clean air)
    float nox_index;    // 1–500  (Sensirion NOx index, 1  = typical clean air)

    // ── SCD40-D (CO₂) ────────────────────────────────────────────────────────
    uint16_t co2_ppm;   // ppm   (400 = ambient, >1000 = poor, >2000 = unsafe)

    // ── BME688 + BSEC2 (environment + IAQ) ───────────────────────────────────
    float temperature;  // °C    (from BSEC2 heat-compensated output)
    float humidity;     // %RH   (from BSEC2 heat-compensated output)
    float pressure;     // hPa
    float iaq;          // 0–500 (BSEC2 IAQ: <50 excellent, >200 very unhealthy)
    uint8_t iaq_accuracy; // 0=stabilizing, 1=uncertain, 2=calibrating, 3=calibrated
    float gas_resistance; // Ω    (raw BME688 gas sensor)

    // ── Derived / ML outputs ─────────────────────────────────────────────────
    float aqs;          // Air Quality Score  0–1  (1 = clean, 0 = heavily polluted)
    float rdr;          // Respiratory Distress Risk 0–1 (1 = high risk)
    float aqi_composite; // Standard AQI (0–500)

    // ── Session exposure accumulator ─────────────────────────────────────────
    float cumulative_exposure; // Dimensionless exposure index (∫ pollution dt)
    float session_avg_aqs;
    float session_min_aqs;
    float session_max_aqs;

    // ── Timestamp ────────────────────────────────────────────────────────────
    uint32_t timestamp_ms;  // millis() at time of reading
    bool     valid;         // all sensors returned plausible data
};

// ─── Input feature vector fed to TFLite model (must match training) ───────────
// Order must be identical to the feature order used in train_model.py
struct ModelInputs {
    // Normalised to [0,1] before inference
    float pm2_5_norm;
    float pm10_norm;
    float voc_norm;
    float nox_norm;
    float co2_norm;
    float iaq_norm;
    float temperature_norm;
    float humidity_norm;
    float pressure_norm;

    static constexpr int SIZE = 9;
};

// ─── Output of the TFLite model ───────────────────────────────────────────────
struct ModelOutputs {
    float aqs;  // Air Quality Score     0–1
    float rdr;  // Respiratory Distress Risk 0–1

    static constexpr int SIZE = 2;
};
