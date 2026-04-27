#pragma once
#include <Arduino.h>
#include "sensors/sensor_data.h"

// ─── Exposure Tracker ─────────────────────────────────────────────────────────
// Accumulates a time-weighted pollution exposure index over the current session.
//
// Exposure integral: E += AQI_composite * Δt_seconds
// This gives a value proportional to the "pollution dose" received.
// Normalising by session duration gives a mean exposure rate.
//
// Also tracks route/waypoint statistics: each call to addWaypoint() stores the
// current AQS so the phone can display a spatial heat-map of the walk.
class ExposureTracker {
public:
    static constexpr uint16_t MAX_WAYPOINTS = 512;

    struct Waypoint {
        float aqs;           // Air Quality Score at this point
        float rdr;           // Respiratory Distress Risk at this point
        float aqi;           // Composite AQI
        uint32_t timestamp_ms;
    };

    void begin();

    // Call once per valid SensorReading cycle
    void update(const SensorReading &r);

    // Fill accumulated statistics back into a reading (mutates r)
    void fillReading(SensorReading &r) const;

    // Reset session (call at start of a new walk)
    void reset();

    // Waypoint log for path visualisation on the phone
    uint16_t       waypointCount()           const { return wp_count_; }
    const Waypoint &waypoint(uint16_t idx)   const { return waypoints_[idx]; }

    float getCumulativeExposure() const { return cumulative_exposure_; }
    float getSessionAvgAQS()     const;
    float getSessionMinAQS()     const { return session_min_aqs_; }
    float getSessionMaxAQS()     const { return session_max_aqs_; }
    float getSessionDurationSec() const;

private:
    uint32_t session_start_ms_    = 0;
    uint32_t last_update_ms_      = 0;

    float cumulative_exposure_    = 0;  // AQI · seconds
    float aqs_sum_                = 0;
    uint32_t sample_count_        = 0;
    float session_min_aqs_        = 1.0f;
    float session_max_aqs_        = 0.0f;

    Waypoint waypoints_[MAX_WAYPOINTS];
    uint16_t wp_count_            = 0;
};
