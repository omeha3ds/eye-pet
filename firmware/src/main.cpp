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
    float blowThresholdDb   = 78.0f;
} rules;

struct DeviceConfig {
    char badgeName[32]  = "Nikita";
    char badgeRole[32]  = "Digital Media";
    char qrUrl1[96]     = "https://www.instagram.com/ni.kita_sm";
    char qrUrl2[96]     = "https://github.com/omeha3ds/eye-pet";
    uint16_t sleepSec   = 20;      // Енергозбереження v1.1.0: 15-30 сек за замовчуванням
    uint8_t brightness  = 85;
    uint32_t eyeColor   = 0x00F0FF; // Cyan за замовчуванням
    uint8_t styleIndex  = 0;
    bool soundEnabled   = true;
    bool hideNotifText  = true;    // Приватність: ховати текст пушів до кліку
} cfg;

// ============================================================================
// 4. СПИСОК УСІХ 15 ДОДАТКІВ ТА 6 МІНІ-ІГОР
// ============================================================================
enum AppId : uint8_t {
    APP_PET = 0,
    APP_CLOCK,
    APP_CARE,
    APP_QUICK_QR,
    APP_NOISE_METER,
    APP_BADGE,
    APP_TIMER,
    APP_STOPWATCH,
    APP_FLASHLIGHT,
    APP_STYLE,
    APP_BLUETOOTH,
    APP_NOTIFICATIONS,
    APP_MUSIC,
    APP_CAMERA,
    APP_PRESENTATION,
    APP_GAMES,
    APP_COUNT
};

enum MiniGameId : uint8_t {
    GAME_BALL = 0,
    GAME_INF_TICTACTOE,
    GAME_MAGIC_8BALL,
    GAME_COIN,
    GAME_DICE,
    GAME_MAZE,
    GAME_COUNT
};

AppId currentApp = APP_PET;
MiniGameId currentGame = GAME_BALL;

// ============================================================================
// 5. BLE, ПОВІДОМЛЕННЯ З ФІЛЬТРОМ ПРИВАТНОСТІ ТА HID-ПУЛЬТ
// ============================================================================
struct NotificationItem {
    char app[24];
    char title[40];
    char body[96];
    bool revealed;
    bool valid;
};

NotificationItem lastNotif = {"", "", "", false, false};
bool bleConnected = false;
BLECharacteristic* inputMedia = nullptr;
BLECharacteristic* inputKey   = nullptr;
BLECharacteristic* inputMouse = nullptr;

// Фільтр приватності: банки, паролі, коди підтвердження не зберігаються взагалі
bool isSensitiveNotification(const char* title, const char* body) {
    const char* blockedWords[] = {
        "OTP", "code", "код", "пароль", "password", "PKO", "IKO",
        "Monobank", "Privat", "BLIK", "2FA", "verification", "CVV", "банк", "Bank"
    };
    String combined = String(title) + " " + String(body);
    combined.toLowerCase();
    for (const char* w : blockedWords) {
        String kw = String(w);
        kw.toLowerCase();
        if (combined.indexOf(kw) >= 0) return true;
    }
    return false;
}

void onIncomingNotification(const char* appName, const char* title, const char* body) {
    if (isSensitiveNotification(title, body)) {
        return;
    }
    strncpy(lastNotif.app, appName, sizeof(lastNotif.app) - 1);
    strncpy(lastNotif.title, title, sizeof(lastNotif.title) - 1);
    strncpy(lastNotif.body, body, sizeof(lastNotif.body) - 1);
    lastNotif.revealed = !cfg.hideNotifText;
    lastNotif.valid = true;

    pet.forcedEmotion = EMO_SURPRISED;
    pet.forcedUntilMs = millis() + 3000;
}

void sendBleConsumerKey(uint16_t usageCode) {
    if (!bleConnected || !inputMedia) return;
    uint8_t report[2] = { (uint8_t)(usageCode & 0xFF), (uint8_t)(usageCode >> 8) };
    inputMedia->setValue(report, 2);
    inputMedia->notify();
    delay(20);
    uint8_t release[2] = { 0, 0 };
    inputMedia->setValue(release, 2);
    inputMedia->notify();
}

void sendPresentationSlide(bool nextSlide) {
    if (!bleConnected || !inputKey) return;
    uint8_t keyReport[8] = {0, 0, (uint8_t)(nextSlide ? 0x4F : 0x50), 0, 0, 0, 0, 0};
    inputKey->setValue(keyReport, 8);
    inputKey->notify();
    delay(20);
    memset(keyReport, 0, 8);
    inputKey->setValue(keyReport, 8);
    inputKey->notify();
}

void sendPresentationMouse(int8_t dx, int8_t dy, int8_t wheel, uint8_t buttons) {
    if (!bleConnected || !inputMouse) return;
    uint8_t mouseReport[4] = { buttons, (uint8_t)dx, (uint8_t)dy, (uint8_t)wheel };
    inputMouse->setValue(mouseReport, 4);
    inputMouse->notify();
}

// ============================================================================
// 6. СЕНСОРИ (IMU, МІКРОФОН, ТАЧ) ТА ЕНЕРГОЗБЕРЕЖЕННЯ V1.1.0
// ============================================================================
float imuX = 0.0f, imuY = 0.0f, imuZ = 1.0f;
float prevImuZ = 1.0f;
float currentNoiseDb = 38.0f;
bool isSleepingScreen = false;
uint32_t lastActivityMs = 0;

void wakeUpDevice() {
    lastActivityMs = millis();
    if (isSleepingScreen) {
        isSleepingScreen = false;
        pet.forcedEmotion = EMO_WAKING_UP;
        pet.forcedUntilMs = millis() + 2000;
    }
}

void updatePowerSaving() {
    uint32_t idleMs = millis() - lastActivityMs;

    if (currentApp == APP_FLASHLIGHT || currentApp == APP_TIMER || currentApp == APP_STOPWATCH) {
        lastActivityMs = millis();
        return;
    }

    if (!isSleepingScreen && idleMs > (uint32_t)cfg.sleepSec * 1000UL) {
        isSleepingScreen = true;
    }

    // Пробудження від підняття в руку (Lift-to-Wake) у режимі сну
    if (isSleepingScreen) {
        float deltaZ = imuZ - prevImuZ;
        if (deltaZ > 0.45f && imuY < -0.25f) {
            wakeUpDevice();
        }
    }
    prevImuZ = imuZ;
}

// Обробка всіх 5 фізичних жестів Пипа
void processSensorInteractions(bool isBeingPetted) {
    float totalG = sqrtf(imuX * imuX + imuY * imuY + imuZ * imuZ);

    // 1. Погладжування по екрану
    if (isBeingPetted) {
        wakeUpDevice();
        pet.attention = min(100.0f, pet.attention + 0.8f);
        pet.fun = min(100.0f, pet.fun + 0.3f);
        pet.forcedEmotion = EMO_PETTED;
        pet.forcedUntilMs = millis() + 2500;
    }

    // 2. Тряска брелока (IMU)
    if (totalG > rules.shakeThresholdG) {
        wakeUpDevice();
        pet.forcedEmotion = (random(0, 2) == 0) ? EMO_DIZZY : EMO_ANGRY;
        pet.forcedUntilMs = millis() + 3500;
        pet.walk = min(100.0f, pet.walk + 0.4f);
    }

    // 3. Переворот екраном вниз (IMU вісь Z)
    if (imuZ < -0.75f && !isSleepingScreen) {
        pet.forcedEmotion = EMO_UPSIDE_DOWN;
        pet.forcedUntilMs = millis() + 1500;
    }

    // 4. Дуття в мікрофон (потік повітря)
    if (currentNoiseDb > rules.blowThresholdDb && !isSleepingScreen) {
        pet.forcedEmotion = EMO_BLOWN;
        pet.forcedUntilMs = millis() + 2500;
    }

    // 5. Нижня кнопка — знайомство з людьми
    if (digitalRead(BTN_BOT_PIN) == LOW) {
        wakeUpDevice();
        pet.attention = min(100.0f, pet.attention + 5.0f);
        pet.forcedEmotion = EMO_GREETING;
        pet.forcedUntilMs = millis() + 4000;
    }
}

// ============================================================================
// 7. ДЕРЕВО РІШЕНЬ НА 45 ЕМОЦІЙ
// ============================================================================
void evaluatePetEmotion(uint8_t currentHour) {
    if (pet.forcedEmotion < EMO_COUNT && millis() < pet.forcedUntilMs) {
        pet.currentEmotion = pet.forcedEmotion;
        return;
    }
    pet.forcedEmotion = EMO_COUNT;

    if (currentHour >= 23 || currentHour < 7) {
        pet.currentEmotion = (pet.sleep < 40.0f) ? EMO_DEEP_SLEEP : EMO_SLEEPING;
        return;
    }

    if (pet.hunger < 15.0f)         pet.currentEmotion = EMO_STARVING;
    else if (pet.hunger < 35.0f)    pet.currentEmotion = EMO_HUNGRY;
    else if (pet.sleep < 20.0f)     pet.currentEmotion = EMO_SLEEPY;
    else if (pet.sleep < 35.0f)     pet.currentEmotion = EMO_YAWN;
    else if (pet.attention < 20.0f) pet.currentEmotion = EMO_LONELY;
    else if (pet.attention < 35.0f) pet.currentEmotion = EMO_POUTING;
    else if (pet.walk < 25.0f)      pet.currentEmotion = EMO_BORED;
    else if (pet.fun > 85.0f && pet.hunger > 75.0f) {
        EmotionId happyPool[] = {EMO_HAPPY, EMO_JOY, EMO_PLAYFUL, EMO_WINK, EMO_PROUD, EMO_LOVE};
        pet.currentEmotion = happyPool[(millis() / 8000) % 6];
    } else {
        EmotionId calmPool[] = {EMO_NEUTRAL, EMO_CURIOUS, EMO_THINKING, EMO_FOCUSED, EMO_SMUG};
        pet.currentEmotion = calmPool[(millis() / 10000) % 5];
    }
}

EyePose getTargetPoseForEmotion(EmotionId emo) {
    EyePose p = {95, 115, 95, 115, 0, 0, 0.0f, 0.0f, 28};
    switch (emo) {
        case EMO_HAPPY:
        case EMO_JOY:
        case EMO_PETTED:
            p.bottomLidRaise = 0.65f; p.leftH = 85; p.rightH = 85; break;
        case EMO_LOVE:
        case EMO_GREETING:
            p.bottomLidRaise = 0.45f; p.leftW = 105; p.rightW = 105; break;
        case EMO_WINK:
            p.leftH = 14; p.rightH = 115; p.bottomLidRaise = 0.3f; break;
        case EMO_ANGRY:
        case EMO_FURIOUS:
            p.topLidSlope = 0.75f; p.leftH = 75; p.rightH = 75; break;
        case EMO_SAD:
        case EMO_CRYING:
        case EMO_LONELY:
            p.topLidSlope = -0.65f; p.lookY = 18; p.leftH = 80; p.rightH = 80; break;
        case EMO_SURPRISED:
        case EMO_SHOCKED:
            p.leftW = 115; p.leftH = 135; p.rightW = 115; p.rightH = 135; p.radius = 45; break;
        case EMO_SLEEPY:
        case EMO_TIRED:
            p.leftH = 40; p.rightH = 40; p.lookY = 12; break;
        case EMO_SLEEPING:
        case EMO_DEEP_SLEEP:
            p.leftH = 10; p.rightH = 10; break;
        case EMO_SKEPTICAL:
        case EMO_SUSPICIOUS:
            p.leftH = 55; p.rightH = 110; p.lookX = 20; break;
        case EMO_DIZZY:
            p.lookX = sinf(millis() * 0.015f) * 30.0f;
            p.lookY = cosf(millis() * 0.015f) * 20.0f;
            break;
        case EMO_BLOWN:
            p.leftW = 70; p.leftH = 35; p.rightW = 70; p.rightH = 35;
            p.lookX = (random(-5, 6)); break;
        default:
            break;
    }
    return p;
}

// ============================================================================
// 8. МІНІ-ІГРИ (НЕСКІНЧЕННІ ХРЕСТИКИ-НУЛИКИ НА 3 ФІШКИ)
// ============================================================================
struct InfiniteTicTacToe {
    uint8_t board[9] = {0};
    int8_t historyX[3] = {-1, -1, -1};
    int8_t historyO[3] = {-1, -1, -1};
    uint8_t countX = 0, countO = 0;
    bool turnX = true;

    void placeMove(uint8_t cell) {
        if (cell >= 9 || board[cell] != 0) return;
        if (turnX) {
            if (countX == 3) {
                board[historyX[0]] = 0;
                historyX[0] = historyX[1];
                historyX[1] = historyX[2];
                countX = 2;
            }
            historyX[countX++] = cell;
            board[cell] = 1;
        } else {
            if (countO == 3) {
                board[historyO[0]] = 0;
                historyO[0] = historyO[1];
                historyO[1] = historyO[2];
                countO = 2;
            }
            historyO[countO++] = cell;
            board[cell] = 2;
        }
        turnX = !turnX;
        pet.fun = min(100.0f, pet.fun + 2.5f);
    }
} tttGame;

const char* MAGIC_8BALL_ANSWERS[] = {
    "Однозначно так!", "Без сумнівів", "Спробуй ще раз",
    "Зараз не час", "Дуже ймовірно", "Навіть не думай",
    "Зірки кажуть ТАК", "Спитай пізніше"
};

// ============================================================================
// 9. ПРОТОКОЛ ОБМІНУ З БРАУЗЕРОМ (WEB SERIAL / WEB BLE / DEBUG.HTML)
// ============================================================================
void handleSerialAndBleCommands() {
    if (!Serial.available()) return;
    String line = Serial.readStringUntil('\n');
    line.trim();

    if (line == "GET_STATE") {
        Serial.printf(
            "{\"emotion\":\"%s\",\"emoId\":%d,\"hunger\":%.1f,\"sleep\":%.1f,"
            "\"fun\":%.1f,\"attention\":%.1f,\"walk\":%.1f,\"noiseDb\":%.1f,"
            "\"badge\":\"%s\",\"sleepSec\":%d}\n",
            EMOTION_NAMES[pet.currentEmotion], pet.currentEmotion,
            pet.hunger, pet.sleep, pet.fun, pet.attention, pet.walk,
            currentNoiseDb, cfg.badgeName, cfg.sleepSec
        );
    } else if (line.startsWith("FORCE_EMO:")) {
        int id = line.substring(10).toInt();
        if (id >= 0 && id < EMO_COUNT) {
            wakeUpDevice();
            pet.forcedEmotion = (EmotionId)id;
            pet.forcedUntilMs = millis() + 10000;
            Serial.println("{\"ok\":true}");
        }
    } else if (line.startsWith("SET_BADGE:")) {
        String name = line.substring(10);
        strncpy(cfg.badgeName, name.c_str(), sizeof(cfg.badgeName) - 1);
        prefs.putString("badgeName", cfg.badgeName);
        Serial.println("{\"ok\":true}");
    } else if (line.startsWith("SET_QR1:")) {
        String url = line.substring(8);
        strncpy(cfg.qrUrl1, url.c_str(), sizeof(cfg.qrUrl1) - 1);
        prefs.putString("qrUrl1", cfg.qrUrl1);
        Serial.println("{\"ok\":true}");
    } else if (line.startsWith("SET_SLEEP:")) {
        cfg.sleepSec = constrain(line.substring(10).toInt(), 10, 300);
        prefs.putUShort("sleepSec", cfg.sleepSec);
        Serial.println("{\"ok\":true}");
    } else if (line.startsWith("ACTION:")) {
        String act = line.substring(7);
        if (act == "FEED")  { pet.hunger = min(100.0f, pet.hunger + 30.0f); pet.forcedEmotion = EMO_EATING; }
        if (act == "SLEEP") { pet.sleep  = min(100.0f, pet.sleep + 40.0f);  pet.forcedEmotion = EMO_SLEEPING; }
        if (act == "PLAY")  { pet.fun    = min(100.0f, pet.fun + 25.0f);    pet.forcedEmotion = EMO_EXCITED; }
        if (act == "WALK")  { pet.walk   = min(100.0f, pet.walk + 30.0f);   pet.forcedEmotion = EMO_HAPPY; }
        pet.forcedUntilMs = millis() + 4000;
        Serial.println("{\"ok\":true}");
    }
}

// ============================================================================
// 10. SETUP ТА ГОЛОВНИЙ ЦИКЛ
// ============================================================================
void setup() {
    Serial.begin(115200);
    pinMode(BTN_TOP_PIN, INPUT_PULLUP);
    pinMode(BTN_BOT_PIN, INPUT_PULLUP);

    Wire.begin(I2C_SDA, I2C_SCL);

    prefs.begin("pip_pet", false);
    String savedBadge = prefs.getString("badgeName", cfg.badgeName);
    strncpy(cfg.badgeName, savedBadge.c_str(), sizeof(cfg.badgeName) - 1);
    String savedQr1 = prefs.getString("qrUrl1", cfg.qrUrl1);
    strncpy(cfg.qrUrl1, savedQr1.c_str(), sizeof(cfg.qrUrl1) - 1);
    cfg.sleepSec  = prefs.getUShort("sleepSec", 20);
    pet.hunger    = prefs.getFloat("hunger", 80.0f);
    pet.sleep     = prefs.getFloat("sleep", 90.0f);
    pet.fun       = prefs.getFloat("fun", 75.0f);
    pet.attention = prefs.getFloat("attention", 80.0f);
    pet.walk      = prefs.getFloat("walk", 70.0f);

    BLEDevice::init("Pip");

    lastActivityMs = millis();
}

uint32_t lastDecayTick = 0;

void loop() {
    handleSerialAndBleCommands();
    updatePowerSaving();

    if (millis() - lastDecayTick >= 60000UL) {
        lastDecayTick = millis();
        pet.hunger    = max(0.0f, pet.hunger - rules.hungerDecayPerMin);
        pet.sleep     = max(0.0f, pet.sleep  - rules.sleepDecayPerMin);
        pet.fun       = max(0.0f, pet.fun    - rules.funDecayPerMin);
        pet.attention = max(0.0f, pet.attention - 0.10f);
        pet.walk      = max(0.0f, pet.walk - 0.08f);

        prefs.putFloat("hunger", pet.hunger);
        prefs.putFloat("sleep", pet.sleep);
        prefs.putFloat("fun", pet.fun);
    }

    if (isSleepingScreen) {
        if (digitalRead(BTN_TOP_PIN) == LOW || digitalRead(BTN_BOT_PIN) == LOW) {
            wakeUpDevice();
        }
        delay(40);
        return;
    }

    processSensorInteractions(false);
    evaluatePetEmotion(14);
    delay(16);
}
