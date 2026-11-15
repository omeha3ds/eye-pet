#pragma once

// ============================================================================
// Waveshare ESP32-S3-Touch-AMOLED-1.75C Hardware Pinout Definition
// Display: CO5300 466x466 QSPI AMOLED | Touch: CST816 | IMU: QMI8658 | PMIC: AXP2101
// ============================================================================

#define SCREEN_WIDTH       466
#define SCREEN_HEIGHT      466

// QSPI AMOLED (CO5300)
#define PIN_LCD_CS         12
#define PIN_LCD_SCK        38
#define PIN_LCD_D0         4
#define PIN_LCD_D1         5
#define PIN_LCD_D2         6
#define PIN_LCD_D3         7
#define PIN_LCD_RST        39

// I2C Bus (Touch CST816, IMU QMI8658, PMIC AXP2101, Audio ES7210/ES8311)
#define PIN_I2C_SDA        15
#define PIN_I2C_SCL        14
#define PIN_TOUCH_INT      11
#define PIN_TOUCH_RST      40

// I2C Device Addresses
#define ADDR_AXP2101       0x34
#define ADDR_QMI8658       0x6B
#define ADDR_CST816        0x15

// Buttons
#define PIN_BTN_TOP        0
