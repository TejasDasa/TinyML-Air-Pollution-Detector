#include "inference.h"
#include "model_data.h"

#if MODEL_LOADED
#include <EloquentTinyML.h>
#include <eloquent_tinyml/tensorflow.h>
static Eloquent::TinyML::TensorFlow::TensorFlow<
    ModelInputs::SIZE,   // inputs
    ModelOutputs::SIZE,  // outputs
    16 * 1024            // tensor arena bytes
> tflite_model;
#endif

bool InferenceEngine::begin() {
#if MODEL_LOADED
    if (!tflite_model.begin(model_data).isOk()) {
        Serial.printf("[ML] TFLite init failed: %s\n",
                      tflite_model.exception.toString().c_str());
        Serial.println("[ML] Falling back to heuristic mode");
        ml_mode_ = false;
        return false;
    }
    ml_mode_ = true;
    Serial.println("[ML] TFLite model loaded – ML inference active");
#else
    ml_mode_ = false;
    Serial.println("[ML] No model deployed – heuristic scoring active");
    Serial.println("[ML] Run training/train_model.py to generate a model");
#endif
    return true;
}

void InferenceEngine::predict(SensorReading &r) {
#if MODEL_LOADED
    if (ml_mode_) {
        float inputs[ModelInputs::SIZE] = {
            normalise(r.pm2_5,        kNormPM25),
            normalise(r.pm10,         kNormPM10),
            normalise(r.voc_index,    kNormVOC),
            normalise(r.nox_index,    kNormNOX),
            normalise((float)r.co2_ppm, kNormCO2),
            normalise(r.iaq,          kNormIAQ),
            normalise(r.temperature,  kNormTemp),
            normalise(r.humidity,     kNormHum),
            normalise(r.pressure,     kNormPres),
        };

        float outputs[ModelOutputs::SIZE] = {};
        tflite_model.predict(inputs, outputs);

        r.aqs = constrain(outputs[0], 0.0f, 1.0f);
        r.rdr = constrain(outputs[1], 0.0f, 1.0f);
        return;
    }
#endif
    heuristicPredict(r);
}

// ─── Heuristic fallback ───────────────────────────────────────────────────────
// Produces interpretable scores directly from sensor values using well-known
// pollution health thresholds. Accuracy is lower than the trained model but
// still useful for field operation while training data is being collected.
void InferenceEngine::heuristicPredict(SensorReading &r) {
    // ── AQS: invert normalised composite pollution load ───────────────────────
    // Each factor mapped to [0,1] badness; AQS = 1 - weighted average of badness
    float pm25_bad  = constrain(r.pm2_5     / 150.0f, 0.0f, 1.0f); // 150 = Very Unhealthy
    float pm10_bad  = constrain(r.pm10      / 354.0f, 0.0f, 1.0f); // 354 = Very Unhealthy
    float co2_bad   = constrain((r.co2_ppm  - 400.0f) / 4600.0f, 0.0f, 1.0f);
    float voc_bad   = constrain((r.voc_index - 1.0f)  / 499.0f, 0.0f, 1.0f);
    float iaq_bad   = constrain(r.iaq       / 500.0f, 0.0f, 1.0f);

    float pollution_load = 0.35f * pm25_bad +
                           0.20f * pm10_bad +
                           0.20f * iaq_bad  +
                           0.15f * co2_bad  +
                           0.10f * voc_bad;

    r.aqs = 1.0f - constrain(pollution_load, 0.0f, 1.0f);

    // ── RDR: respiratory distress risk ───────────────────────────────────────
    // Weighted towards PM2.5 and PM10 which most strongly affect respiratory health.
    // Thresholds from WHO guidelines and EPA sensitive groups advisories.
    float rdr_pm25  = constrain(r.pm2_5     / 55.0f,  0.0f, 1.0f); // 55 = Unhealthy
    float rdr_pm10  = constrain(r.pm10      / 154.0f, 0.0f, 1.0f);
    float rdr_co2   = constrain((r.co2_ppm  - 1000.0f) / 3000.0f, 0.0f, 1.0f);
    float rdr_voc   = constrain((r.voc_index - 150.0f) / 350.0f, 0.0f, 1.0f);
    float rdr_iaq   = constrain((r.iaq      - 100.0f) / 400.0f, 0.0f, 1.0f);
    float rdr_nox   = constrain((r.nox_index - 1.0f)  / 199.0f, 0.0f, 1.0f);

    r.rdr = constrain(
        0.30f * rdr_pm25 +
        0.20f * rdr_pm10 +
        0.20f * rdr_iaq  +
        0.15f * rdr_co2  +
        0.10f * rdr_nox  +
        0.05f * rdr_voc,
        0.0f, 1.0f);
}

float InferenceEngine::normalise(float value, const float params[2]) {
    // Z-score normalisation; clamp to [-3, 3] to handle outliers
    return constrain((value - params[0]) / params[1], -3.0f, 3.0f);
}
