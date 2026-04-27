#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "sensors/sensor_data.h"
#include "pollution/exposure_tracker.h"

// ─── BLE Service UUIDs ────────────────────────────────────────────────────────
// Primary service: Air Quality Monitor
#define BLE_SERVICE_UUID         "4fafc201-1fb5-459e-8fcc-c5c9c331914b"

// Notify: live sensor + ML output packet (binary, 60 bytes)
#define BLE_CHAR_LIVE_UUID       "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// Read + Notify: session summary (JSON string, <512 bytes)
#define BLE_CHAR_SESSION_UUID    "beb5483e-36e1-4688-b7f5-ea07361b26a9"

// Write: control commands from phone (JSON, e.g. {"cmd":"reset_session"})
#define BLE_CHAR_CONTROL_UUID    "beb5483e-36e1-4688-b7f5-ea07361b26aa"

// ─── Live data packet (binary, little-endian) ─────────────────────────────────
// Sent via BLE notification every BLE_NOTIFY_INTERVAL_MS milliseconds.
// Phone app must parse this struct (or the JSON alternative – see sendReading).
struct __attribute__((packed)) LivePacket {
    uint32_t timestamp_ms;

    // Raw sensor values
    float pm1_0;
    float pm2_5;
    float pm10;
    float voc_index;
    float nox_index;
    uint16_t co2_ppm;
    float temperature;
    float humidity;
    float pressure;
    float iaq;
    uint8_t iaq_accuracy;

    // ML / heuristic outputs
    float aqs;          // Air Quality Score 0–1
    float rdr;          // Respiratory Distress Risk 0–1
    float aqi_composite;

    // Session accumulator
    float cumulative_exposure;
    float session_avg_aqs;
    float session_min_aqs;
    float session_max_aqs;
};

// ─── BLE Service class ────────────────────────────────────────────────────────
class BLEAirService : public BLEServerCallbacks {
public:
    void begin();

    // Send a live data notification (if a client is subscribed)
    void sendReading(const SensorReading &r);

    // Update the session characteristic (called after every update)
    void updateSession(const ExposureTracker &tracker);

    bool isConnected() const { return connected_; }

    // Pending command from the phone (cleared after being consumed)
    bool hasPendingCommand() const { return has_command_; }
    String consumeCommand();

private:
    // BLEServerCallbacks
    void onConnect(BLEServer *server) override;
    void onDisconnect(BLEServer *server) override;

    BLEServer          *server_       = nullptr;
    BLECharacteristic  *char_live_    = nullptr;
    BLECharacteristic  *char_session_ = nullptr;
    BLECharacteristic  *char_control_ = nullptr;

    bool    connected_   = false;
    bool    has_command_ = false;
    String  pending_cmd_;

    // Inner class to handle Write callbacks
    class ControlCallback : public BLECharacteristicCallbacks {
    public:
        explicit ControlCallback(BLEAirService *svc) : svc_(svc) {}
        void onWrite(BLECharacteristic *c) override;
    private:
        BLEAirService *svc_;
    };
};
