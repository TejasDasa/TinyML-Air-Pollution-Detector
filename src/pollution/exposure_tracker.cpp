#include "exposure_tracker.h"

void ExposureTracker::begin() {
    reset();
}

void ExposureTracker::reset() {
    session_start_ms_   = millis();
    last_update_ms_     = millis();
    cumulative_exposure_= 0;
    aqs_sum_            = 0;
    sample_count_       = 0;
    session_min_aqs_    = 1.0f;
    session_max_aqs_    = 0.0f;
    wp_count_           = 0;
}

void ExposureTracker::update(const SensorReading &r) {
    if (!r.valid) return;

    uint32_t now = millis();
    float dt_sec = (now - last_update_ms_) / 1000.0f;
    last_update_ms_ = now;

    // Clamp dt to avoid huge jumps after a long idle (e.g. first call)
    if (dt_sec > 30.0f) dt_sec = 30.0f;

    // Exposure integral: higher AQI = faster dose accumulation
    cumulative_exposure_ += r.aqi_composite * dt_sec;

    // Running statistics on AQS
    aqs_sum_    += r.aqs;
    sample_count_++;
    if (r.aqs < session_min_aqs_) session_min_aqs_ = r.aqs;
    if (r.aqs > session_max_aqs_) session_max_aqs_ = r.aqs;

    // Record waypoint (circular – overwrite oldest when full)
    uint16_t idx = wp_count_ < MAX_WAYPOINTS ? wp_count_ : (MAX_WAYPOINTS - 1);
    waypoints_[idx] = { r.aqs, r.rdr, r.aqi_composite, now };
    if (wp_count_ < MAX_WAYPOINTS) wp_count_++;
}

void ExposureTracker::fillReading(SensorReading &r) const {
    r.cumulative_exposure = cumulative_exposure_;
    r.session_avg_aqs     = getSessionAvgAQS();
    r.session_min_aqs     = session_min_aqs_;
    r.session_max_aqs     = session_max_aqs_;
}

float ExposureTracker::getSessionAvgAQS() const {
    return sample_count_ > 0 ? aqs_sum_ / sample_count_ : 0.0f;
}

float ExposureTracker::getSessionDurationSec() const {
    return (millis() - session_start_ms_) / 1000.0f;
}
