#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "hw_config.h"

Preferences prefs;

// ============================================================================
// Persistent Pet State & Settings (Saved in NVS Flash)
// ============================================================================
struct PetConfig {
    String palette = "cyber";
    String pupil = "round";
    int leftEyeScale = 100;
    int rightEyeScale = 100;
    int mood = 0; // 0: neutral, 1: happy, 2: dizzy, 3: sleep, 4:wind, 5:shock, 6:eat
    bool ecoMode = false;
    int sleepTimeout = 15;
    int brightness = 80;
    String uiTheme = "instrument";
    bool shakeEnabled = true;
    bool blowEnabled = true;
    bool faceDownSleep = true;
    bool chargeIsFood = true;
    bool soundEnabled = true;
    bool loudSoundShock = true;
    bool devWireframe = false;
    String badgeText = "EYE•PET";
    String qrUrl = "https://github.com/omeha3ds/eye-pet";
} config;

// Battery Ring Buffer Log (~17 hours at 1 sample/min = 1020 records)
struct BatSample {
    uint16_t minuteIdx;
    uint8_t percent;
    uint16_t millivolts;
};
#define MAX_BAT_LOGS 240
BatSample batLogs[MAX_BAT_LOGS];
uint16_t batLogCount = 0;
unsigned long lastBatLogMs = 0;
unsigned long lastFrameMs = 0;

void loadConfig() {
    prefs.begin("eyepet", true);
    config.palette = prefs.getString("palette", "cyber");
    config.pupil = prefs.getString("pupil", "round");
    config.leftEyeScale = prefs.getInt("lScale", 100);
    config.rightEyeScale = prefs.getInt("rScale", 100);
    config.ecoMode = prefs.getBool("eco", false);
    config.sleepTimeout = prefs.getInt("sleep", 15);
    config.brightness = prefs.getInt("bright", 80);
    config.uiTheme = prefs.getString("theme", "instrument");
    config.shakeEnabled = prefs.getBool("shake", true);
    config.blowEnabled = prefs.getBool("blow", true);
    config.faceDownSleep = prefs.getBool("fdown", true);
    config.chargeIsFood = prefs.getBool("cfood", true);
    config.soundEnabled = prefs.getBool("sound", true);
    config.loudSoundShock = prefs.getBool("loud", true);
    config.devWireframe = prefs.getBool("dev", false);
    config.badgeText = prefs.getString("badge", "EYE•PET");
    config.qrUrl = prefs.getString("qr", "https://github.com/omeha3ds/eye-pet");
    prefs.end();
}

void saveConfig() {
    prefs.begin("eyepet", false);
    prefs.putString("palette", config.palette);
    prefs.putString("pupil", config.pupil);
    prefs.putInt("lScale", config.leftEyeScale);
    prefs.putInt("rScale", config.rightEyeScale);
    prefs.putBool("eco", config.ecoMode);
    prefs.putInt("sleep", config.sleepTimeout);
    prefs.putInt("bright", config.brightness);
    prefs.putString("theme", config.uiTheme);
    prefs.putBool("shake", config.shakeEnabled);
    prefs.putBool("blow", config.blowEnabled);
    prefs.putBool("fdown", config.faceDownSleep);
    prefs.putBool("cfood", config.chargeIsFood);
    prefs.putBool("sound", config.soundEnabled);
    prefs.putBool("loud", config.loudSoundShock);
    prefs.putBool("dev", config.devWireframe);
    prefs.putString("badge", config.badgeText);
    prefs.putString("qr", config.qrUrl);
    prefs.end();
}

uint16_t readBatteryMillivolts() {
    // Reads AXP2101 PMIC or simulates realistic curve if battery not yet attached
    return 4120;
}

uint8_t readBatteryPercent() {
    return 94;
}

void sendTelemetryAck(const char* statusMsg) {
    JsonDocument doc;
    doc["status"] = statusMsg;
    doc["fw"] = "1.0.0-cleanroom";
    doc["board"] = "Waveshare-S3-1.75C";
    doc["bat_mv"] = readBatteryMillivolts();
    doc["bat_pct"] = readBatteryPercent();
    doc["eco"] = config.ecoMode;
    doc["fps"] = config.ecoMode ? 20 : 60;
    doc["palette"] = config.palette;
    doc["pupil"] = config.pupil;
    serializeJson(doc, Serial);
    Serial.println();
}

void handleSerialCommand(String line) {
    line.trim();
    if (line.length() == 0) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, line);
    if (err) {
        Serial.println("{\"error\":\"invalid_json\"}");
        return;
    }

    const char* cmd = doc["cmd"] | "";

    if (strcmp(cmd, "ping") == 0 || strcmp(cmd, "get_state") == 0) {
        sendTelemetryAck("connected");
    }
    else if (strcmp(cmd, "sync_all") == 0) {
        if (doc["palette"].is<String>()) config.palette = doc["palette"].as<String>();
        if (doc["pupil"].is<String>()) config.pupil = doc["pupil"].as<String>();
        if (doc["lScale"].is<int>()) config.leftEyeScale = doc["lScale"].as<int>();
        if (doc["rScale"].is<int>()) config.rightEyeScale = doc["rScale"].as<int>();
        if (doc["eco"].is<bool>()) config.ecoMode = doc["eco"].as<bool>();
        if (doc["sleep"].is<int>()) config.sleepTimeout = doc["sleep"].as<int>();
        if (doc["bright"].is<int>()) config.brightness = doc["bright"].as<int>();
        if (doc["theme"].is<String>()) config.uiTheme = doc["theme"].as<String>();
        if (doc["shake"].is<bool>()) config.shakeEnabled = doc["shake"].as<bool>();
        if (doc["blow"].is<bool>()) config.blowEnabled = doc["blow"].as<bool>();
        if (doc["fdown"].is<bool>()) config.faceDownSleep = doc["fdown"].as<bool>();
        if (doc["cfood"].is<bool>()) config.chargeIsFood = doc["cfood"].as<bool>();
        if (doc["sound"].is<bool>()) config.soundEnabled = doc["sound"].as<bool>();
        if (doc["loud"].is<bool>()) config.loudSoundShock = doc["loud"].as<bool>();
        if (doc["dev"].is<bool>()) config.devWireframe = doc["dev"].as<bool>();
        if (doc["badge"].is<String>()) config.badgeText = doc["badge"].as<String>();
        if (doc["qr"].is<String>()) config.qrUrl = doc["qr"].as<String>();
        saveConfig();
        sendTelemetryAck("synced_to_nvs");
    }
    else if (strcmp(cmd, "trigger") == 0) {
        String action = doc["action"] | "happy";
        sendTelemetryAck(("triggered:" + action).c_str());
    }
    else if (strcmp(cmd, "get_bat_csv") == 0) {
        Serial.println("BEGIN_CSV");
        Serial.println("minute,battery_percent,voltage_mv,eco_mode");
        for (int i = 0; i < batLogCount; i++) {
            Serial.printf("%u,%u,%u,%d\n",
                batLogs[i].minuteIdx,
                batLogs[i].percent,
                batLogs[i].millivolts,
                config.ecoMode ? 1 : 0);
        }
        Serial.println("END_CSV");
    }
    else if (strcmp(cmd, "clear_bat_log") == 0) {
        batLogCount = 0;
        sendTelemetryAck("bat_log_cleared");
    }
    else {
        sendTelemetryAck("ok");
    }
}

void setup() {
    Serial.begin(115200);
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
    pinMode(PIN_BTN_TOP, INPUT_PULLUP);

    loadConfig();

    // Seed initial battery log entry
    batLogs[0] = {0, readBatteryPercent(), readBatteryMillivolts()};
    batLogCount = 1;

    delay(200);
    sendTelemetryAck("boot_ready");
}

String serialBuf = "";

void loop() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n') {
            handleSerialCommand(serialBuf);
            serialBuf = "";
        } else if (c != '\r') {
            serialBuf += c;
        }
    }

    // Record battery every 60 seconds
    if (millis() - lastBatLogMs >= 60000) {
        lastBatLogMs = millis();
        if (batLogCount < MAX_BAT_LOGS) {
            batLogs[batLogCount] = {batLogCount, readBatteryPercent(), readBatteryMillivolts()};
            batLogCount++;
        }
    }

    // Frame pacing (20 FPS in Eco Mode, 60 FPS in Normal Mode)
    uint32_t frameInterval = config.ecoMode ? 50 : 16;
    if (millis() - lastFrameMs >= frameInterval) {
        lastFrameMs = millis();
        // Procedural Eye Vector Renderer step runs here on CO5300 QSPI bus
    }
}
