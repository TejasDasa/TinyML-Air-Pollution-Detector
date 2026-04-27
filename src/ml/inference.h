#pragma once
#include <Arduino.h>
#include "sensors/sensor_data.h"

// ─── On-device TFLite Micro inference ────────────────────────────────────────
// When MODEL_LOADED == 1 (model_data.h):
//   Uses the trained TFLite model for AQS and RDR prediction.
//
// When MODEL_LOADED == 0 (no model yet):
//   Falls back to a transparent heuristic that combines the BSEC2 IAQ score,
//   PM2.5 concentration, and CO2 level — still useful in the field.
//
// Either way the same two outputs are produced:
//   aqs  – Air Quality Score     [0,1]  (1 = excellent, 0 = very polluted)
//   rdr  – Respiratory Distress Risk [0,1]  (1 = high risk)
class InferenceEngine {
public:
    bool begin();     // Initialise TFLite allocator (or heuristic mode)
    bool isMLMode() const { return ml_mode_; }

    // Run inference and write r.aqs and r.rdr
    void predict(SensorReading &r);

private:
    bool ml_mode_ = false;

#if MODEL_LOADED
    // TFLite Micro allocator arena (16 KB – sufficient for our small model)
    static constexpr int kArenaSize = 16 * 1024;
    uint8_t arena_[kArenaSize];
#endif

    // ── Feature normalisation constants (must match train_model.py) ──────────
    // Format: {mean, std}   (applied as (x - mean) / std)
    static constexpr float kNormPM25[2]   = {  25.0f,  40.0f };
    static constexpr float kNormPM10[2]   = {  50.0f,  80.0f };
    static constexpr float kNormVOC[2]    = { 150.0f, 100.0f };
    static constexpr float kNormNOX[2]    = {  50.0f,  60.0f };
    static constexpr float kNormCO2[2]    = { 800.0f, 600.0f };
    static constexpr float kNormIAQ[2]    = { 100.0f, 120.0f };
    static constexpr float kNormTemp[2]   = {  22.0f,  10.0f };
    static constexpr float kNormHum[2]    = {  50.0f,  25.0f };
    static constexpr float kNormPres[2]   = {1013.0f,  20.0f };

    static float normalise(float value, const float params[2]);

    // Heuristic fallback (used when MODEL_LOADED == 0)
    void heuristicPredict(SensorReading &r);
};
