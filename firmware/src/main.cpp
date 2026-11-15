#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ============================================================================
// 1. НАЛАШТУВАННЯ ПІНІВ WAVESHARE ESP32-S3-TOUCH-AMOLED-1.75C (466x466)
// ============================================================================
#define SCREEN_WIDTH  466
#define SCREEN_HEIGHT 466
#define I2C_SDA       15
#define I2C_SCL       14
#define BTN_TOP_PIN   0   // Підтвердження / Назад / Пробудження
#define BTN_BOT_PIN   47  // Нижня кнопка: Знайомство з людьми / Дія

Preferences prefs;

// ============================================================================
// 2. СПИСОК УСІХ 45 ЕМОЦІЙ (ЯК У PIP-KEYPET)
// ============================================================================
enum EmotionId : uint8_t {
    EMO_NEUTRAL = 0, EMO_HAPPY, EMO_JOY, EMO_EXCITED, EMO_LOVE,
    EMO_BLUSH, EMO_PROUD, EMO_PLAYFUL, EMO_WINK, EMO_SMUG,
    EMO_CURIOUS, EMO_SURPRISED, EMO_SHOCKED, EMO_CONFUSED, EMO_THINKING,
    EMO_FOCUSED, EMO_SKEPTICAL, EMO_SUSPICIOUS, EMO_BORED, EMO_TIRED,
    EMO_SLEEPY, EMO_SLEEPING, EMO_DEEP_SLEEP, EMO_YAWN, EMO_WAKING_UP,
    EMO_HUNGRY, EMO_STARVING, EMO_EATING, EMO_FULL, EMO_LONELY,
    EMO_SAD, EMO_CRYING, EMO_POUTING, EMO_SHY, EMO_SCARED,
    EMO_ANGRY, EMO_FURIOUS, EMO_ANNOYED, EMO_DIZZY, EMO_BLOWN,
    EMO_UPSIDE_DOWN, EMO_PETTED, EMO_GREETING, EMO_MUSIC_VIBE, EMO_LOW_BATTERY,
    EMO_COUNT // 45
};

const char* EMOTION_NAMES[EMO_COUNT] = {
    "NEUTRAL", "HAPPY", "JOY", "EXCITED", "LOVE",
    "BLUSH", "PROUD", "PLAYFUL", "WINK", "SMUG",
    "CURIOUS", "SURPRISED", "SHOCKED", "CONFUSED", "THINKING",
    "FOCUSED", "SKEPTICAL", "SUSPICIOUS", "BORED", "TIRED",
    "SLEEPY", "SLEEPING", "DEEP_SLEEP", "YAWN", "WAKING_UP",
    "HUNGRY", "STARVING", "EATING", "FULL", "LONELY",
    "SAD", "CRYING", "POUTING", "SHY", "SCARED",
    "ANGRY", "FURIOUS", "ANNOYED", "DIZZY", "BLOWN",
    "UPSIDE_DOWN", "PETTED", "GREETING", "MUSIC_VIBE", "LOW_BATTERY"
};

struct EyePose {
    float leftW, leftH;
    float rightW, rightH;
    float lookX, lookY;
    float topLidSlope;    // Нахил верхньої повіки (-1..1: сумний -> злий)
    float bottomLidRaise; // Прищур знизу (0..1: радість/усмішка очима)
    uint8_t radius;
};

// ============================================================================
// 3. СИСТЕМА СТАНІВ ПИТОМЦЯ, НАЛАШТУВАНЬ ТА ПРАВИЛ (NVS + DEBUG)
// ============================================================================
struct PetState {
    float hunger    = 80.0f; // 0..100 (100 = ситий)
    float sleep     = 90.0f; // 0..100 (100 = виспаний)
    float fun       = 75.0f; // 0..100 (100 = веселий)
    float attention = 80.0f; // 0..100 (100 = обласканий)
    float walk      = 70.0f; // 0..100 (100 = нагуляний)
    EmotionId currentEmotion = EMO_NEUTRAL;
    EmotionId forcedEmotion  = EMO_COUNT; // Якщо < EMO_COUNT — примусовий режим з debug.html
    uint32_t forcedUntilMs   = 0;
} pet;

struct RuleWeights {
    float hungerDecayPerMin = 0.15f;
    float sleepDecayPerMin  = 0.10f;
    float funDecayPerMin    = 0.12f;
    float shakeThresholdG   = 2.3f;
    float blowThresholdDb   = 78.0
