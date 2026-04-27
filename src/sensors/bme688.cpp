#include "bme688.h"
#include <Preferences.h>

BME688Sensor *BME688Sensor::instance_ = nullptr;

// ─── Sensor list requested from BSEC2 ─────────────────────────────────────────
static const bsec_virtual_sensor_t kSensorList[] = {
    BSEC_OUTPUT_IAQ,
    BSEC_OUTPUT_STATIC_IAQ,
    BSEC_OUTPUT_CO2_EQUIVALENT,
    BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
    BSEC_OUTPUT_RAW_TEMPERATURE,
    BSEC_OUTPUT_RAW_PRESSURE,
    BSEC_OUTPUT_RAW_HUMIDITY,
    BSEC_OUTPUT_RAW_GAS,
    BSEC_OUTPUT_STABILIZATION_STATUS,
    BSEC_OUTPUT_RUN_IN_STATUS,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
    BSEC_OUTPUT_COMPENSATED_GAS,
    BSEC_OUTPUT_GAS_PERCENTAGE,
};
static constexpr uint8_t kSensorCount = sizeof(kSensorList) / sizeof(kSensorList[0]);

bool BME688Sensor::begin(uint8_t i2c_addr) {
    instance_ = this;

    if (!bsec_.begin(i2c_addr, Wire)) {
        Serial.printf("[BME688] Init failed – check I2C address and wiring. BSEC err: %d\n",
                      bsec_.status);
        return false;
    }

    loadState();

    if (!bsec_.updateSubscription(kSensorList, kSensorCount, BSEC_SAMPLE_RATE_LP)) {
        Serial.printf("[BME688] Subscription failed: %d\n", bsec_.status);
        return false;
    }

    bsec_.attachCallback(bsecCallback);

    Serial.printf("[BME688] Ready – BSEC2 v%d.%d.%d.%d\n",
                  bsec_.version.major, bsec_.version.minor,
                  bsec_.version.major_bugfix, bsec_.version.minor_bugfix);
    return true;
}

void BME688Sensor::process() {
    if (!bsec_.run()) {
        if (bsec_.status < BSEC_OK) {
            Serial.printf("[BME688] BSEC error: %d\n", bsec_.status);
        }
    }

    // Periodically save calibration state to NVS
    if (iaq_accuracy_ >= 3 &&
        millis() - last_state_save_ms_ > STATE_SAVE_INTERVAL_MS) {
        saveState();
        last_state_save_ms_ = millis();
    }
}

void BME688Sensor::fillReading(SensorReading &r) {
    r.temperature    = temperature_;
    r.humidity       = humidity_;
    r.pressure       = pressure_;
    r.iaq            = iaq_;
    r.iaq_accuracy   = iaq_accuracy_;
    r.gas_resistance = gas_resistance_;
    data_ready_      = false;
}

// ─── BSEC2 callback (static, routes to singleton) ────────────────────────────
void BME688Sensor::bsecCallback(const bme68xData data,
                                const bsecOutputs outputs,
                                Bsec2 /*bsec*/) {
    if (!instance_ || !outputs.nOutputs) return;

    for (uint8_t i = 0; i < outputs.nOutputs; i++) {
        const bsecData &o = outputs.output[i];
        switch (o.sensor_id) {
            case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE:
                instance_->temperature_ = o.signal;
                break;
            case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY:
                instance_->humidity_ = o.signal;
                break;
            case BSEC_OUTPUT_RAW_PRESSURE:
                instance_->pressure_ = o.signal / 100.0f; // Pa → hPa
                break;
            case BSEC_OUTPUT_IAQ:
                instance_->iaq_          = o.signal;
                instance_->iaq_accuracy_ = o.accuracy;
                break;
            case BSEC_OUTPUT_RAW_GAS:
                instance_->gas_resistance_ = o.signal;
                break;
            default:
                break;
        }
    }
    instance_->data_ready_ = true;
}

// ─── NVS state persistence ────────────────────────────────────────────────────
void BME688Sensor::loadState() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, /*readOnly=*/true);
    size_t len = prefs.getBytesLength(NVS_KEY);
    if (len > 0) {
        uint8_t buf[BSEC_MAX_STATE_BLOB_SIZE];
        prefs.getBytes(NVS_KEY, buf, len);
        bsec_.setState(buf);
        Serial.println("[BME688] BSEC2 calibration state loaded from NVS");
    } else {
        Serial.println("[BME688] No saved state – starting fresh BSEC2 calibration");
    }
    prefs.end();
}

void BME688Sensor::saveState() {
    uint8_t buf[BSEC_MAX_STATE_BLOB_SIZE];
    uint32_t len = 0;
    bsec_.getState(buf);
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, /*readOnly=*/false);
    prefs.putBytes(NVS_KEY, buf, BSEC_MAX_STATE_BLOB_SIZE);
    prefs.end();
    Serial.println("[BME688] BSEC2 calibration state saved to NVS");
}
