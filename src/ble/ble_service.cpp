#include "ble_service.h"
#include <Arduino.h>

// ─── Initialise BLE stack and GATT server ─────────────────────────────────────
void BLEAirService::begin() {
    BLEDevice::init(BLE_DEVICE_NAME);

    // Increase BLE MTU for larger payloads (max 517 on ESP32)
    BLEDevice::setMTU(247);

    server_ = BLEDevice::createServer();
    server_->setCallbacks(this);

    BLEService *service = server_->createService(BLE_SERVICE_UUID);

    // ── Live data characteristic (notify only) ────────────────────────────────
    char_live_ = service->createCharacteristic(
        BLE_CHAR_LIVE_UUID,
        BLECharacteristic::PROPERTY_NOTIFY);
    char_live_->addDescriptor(new BLE2902());

    // ── Session summary characteristic (read + notify) ────────────────────────
    char_session_ = service->createCharacteristic(
        BLE_CHAR_SESSION_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    char_session_->addDescriptor(new BLE2902());

    // ── Control characteristic (write) ────────────────────────────────────────
    char_control_ = service->createCharacteristic(
        BLE_CHAR_CONTROL_UUID,
        BLECharacteristic::PROPERTY_WRITE);
    char_control_->setCallbacks(new ControlCallback(this));

    service->start();

    // Start advertising
    BLEAdvertising *adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(BLE_SERVICE_UUID);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06); // helps iPhone connection
    adv->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.printf("[BLE] Advertising as \"%s\"\n", BLE_DEVICE_NAME);
}

// ─── Send a live data notification ───────────────────────────────────────────
void BLEAirService::sendReading(const SensorReading &r) {
    if (!connected_) return;

    LivePacket pkt;
    pkt.timestamp_ms       = r.timestamp_ms;
    pkt.pm1_0              = r.pm1_0;
    pkt.pm2_5              = r.pm2_5;
    pkt.pm10               = r.pm10;
    pkt.voc_index          = r.voc_index;
    pkt.nox_index          = r.nox_index;
    pkt.co2_ppm            = r.co2_ppm;
    pkt.temperature        = r.temperature;
    pkt.humidity           = r.humidity;
    pkt.pressure           = r.pressure;
    pkt.iaq                = r.iaq;
    pkt.iaq_accuracy       = r.iaq_accuracy;
    pkt.aqs                = r.aqs;
    pkt.rdr                = r.rdr;
    pkt.aqi_composite      = r.aqi_composite;
    pkt.cumulative_exposure= r.cumulative_exposure;
    pkt.session_avg_aqs    = r.session_avg_aqs;
    pkt.session_min_aqs    = r.session_min_aqs;
    pkt.session_max_aqs    = r.session_max_aqs;

    char_live_->setValue(reinterpret_cast<uint8_t *>(&pkt), sizeof(pkt));
    char_live_->notify();
}

// ─── Update session summary characteristic ───────────────────────────────────
void BLEAirService::updateSession(const ExposureTracker &tracker) {
    // Build a compact JSON string for the session characteristic
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"dur\":%.0f,\"exp\":%.1f,\"avg_aqs\":%.3f,"
        "\"min_aqs\":%.3f,\"max_aqs\":%.3f,\"waypoints\":%u}",
        tracker.getSessionDurationSec(),
        tracker.getCumulativeExposure(),
        tracker.getSessionAvgAQS(),
        tracker.getSessionMinAQS(),
        tracker.getSessionMaxAQS(),
        tracker.waypointCount());

    char_session_->setValue(reinterpret_cast<uint8_t *>(buf), strlen(buf));
    if (connected_) char_session_->notify();
}

// ─── Command handling ─────────────────────────────────────────────────────────
String BLEAirService::consumeCommand() {
    has_command_ = false;
    return pending_cmd_;
}

void BLEAirService::ControlCallback::onWrite(BLECharacteristic *c) {
    std::string val = c->getValue();
    if (val.length() > 0) {
        svc_->pending_cmd_ = String(val.c_str());
        svc_->has_command_ = true;
        Serial.printf("[BLE] Command received: %s\n", val.c_str());
    }
}

// ─── Connection callbacks ─────────────────────────────────────────────────────
void BLEAirService::onConnect(BLEServer *server) {
    connected_ = true;
    Serial.println("[BLE] Client connected");
}

void BLEAirService::onDisconnect(BLEServer *server) {
    connected_ = false;
    Serial.println("[BLE] Client disconnected – restarting advertising");
    BLEDevice::startAdvertising();
}
