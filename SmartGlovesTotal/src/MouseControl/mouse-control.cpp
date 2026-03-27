// ===========================================
// MOUSE CONTROL MODULE
// Mouse/keyboard → BLE HID (unchanged)
// Letter data    → WiFi TCP (replaces Serial + BLE UART)
// ===========================================

#include "mouse-control.h"
#include "WiFiTcp/wifi-tcp.h"        // ← WiFi TCP replaces bleUartSend
#include <BleCombo.h>
#include <Wire.h>
#include <Arduino.h>

// ===========================================
// SENSOR DEFINITIONS
// ===========================================
#define MPU6050_ADDR         0x68
#define MPU6050_SMPLRT_DIV   0x19
#define MPU6050_CONFIG       0x1a
#define MPU6050_GYRO_CONFIG  0x1b
#define MPU6050_ACCEL_CONFIG 0x1c
#define MPU6050_WHO_AM_I     0x75
#define MPU6050_PWR_MGMT_1   0x6b

// ===========================================
// BUTTON CONFIGURATION
// ===========================================
#define ENABLE_BTN 2
#define LETTER_BTN 18

// ===========================================
// SENSOR VARIABLES
// ===========================================
double offsetX = 0, offsetY = 0, offsetZ = 0;
float dpsX, dpsY, dpsZ;
float accelX, accelY, accelZ;

// ===========================================
// FILTERING
// ===========================================
float ema_alpha = 0.3;
float ema_x = 0, ema_y = 0, ema_z = 0;

struct KalmanFilter {
  float Q = 0.001, R = 0.03, P = 1.0, K = 0, X = 0;
};
KalmanFilter kalman_x, kalman_y, kalman_z;

float DEAD_ZONE        = 2.0;
float ACCEL_THRESHOLD  = 25.0;
float ACCEL_MULTIPLIER = 2.4;
float x_sensitivity    = 0.5;
float y_sensitivity    = 0.4;
float adaptive_offset_x = 0, adaptive_offset_y = 0;
unsigned long last_drift_correction = 0;
const unsigned long DRIFT_CORRECTION_INTERVAL = 5000;

// ===========================================
// BUTTON
// ===========================================
struct Button {
  int pin;
  bool lastState, currentState, isPressed, clickProcessed;
  unsigned long lastDebounceTime, debounceDelay, pressStartTime, releaseTime, lastClickTime;
  int clickCount;

  Button(int p = 0) {
    pin = p; lastState = HIGH; currentState = HIGH;
    lastDebounceTime = 0; debounceDelay = 50;
    isPressed = false; pressStartTime = 0; releaseTime = 0;
    lastClickTime = 0; clickCount = 0; clickProcessed = false;
  }
};

Button enableBtn(ENABLE_BTN);
Button letterBtn(LETTER_BTN);
const unsigned long DOUBLE_CLICK_WINDOW  = 400;
const unsigned long LONG_PRESS_THRESHOLD = 300;
const unsigned long CLICK_TIMEOUT        = 500;
bool movementEnabled = false;

// ===========================================
// LETTER RECOGNITION
// ===========================================
bool          letterModeActive        = false;
unsigned long letterModeStart         = 0;
const unsigned long LETTER_RECORDING_DURATION = 2000;  // ms

// ===========================================
// FILTERS
// ===========================================
float kalmanUpdate(KalmanFilter &kf, float m) {
  kf.P += kf.Q;
  kf.K  = kf.P / (kf.P + kf.R);
  kf.X += kf.K * (m - kf.X);
  kf.P  = (1 - kf.K) * kf.P;
  return kf.X;
}
float emaFilter(float p, float c, float a)       { return a*c + (1-a)*p; }
float applyDeadZone(float v, float t)             { if(abs(v)<t) return 0; return v>0?(abs(v)-t):-(abs(v)-t); }
float applyAcceleration(float v, float t, float m){ if(abs(v)>t) return v>0?(t+(abs(v)-t)*m):-(t+(abs(v)-t)*m); return v; }

// ===========================================
// SENSOR
// ===========================================
void calcRotation() {
  Wire.beginTransmission(MPU6050_ADDR); Wire.write(0x43);
  Wire.endTransmission(false); Wire.requestFrom(MPU6050_ADDR, 6, true);
  dpsX = (float)(int16_t)(Wire.read()<<8|Wire.read()) / 65.5f;
  dpsY = (float)(int16_t)(Wire.read()<<8|Wire.read()) / 65.5f;
  dpsZ = (float)(int16_t)(Wire.read()<<8|Wire.read()) / 65.5f;
}

void calcAcceleration() {
  Wire.beginTransmission(MPU6050_ADDR); Wire.write(0x3B);
  Wire.endTransmission(false); Wire.requestFrom(MPU6050_ADDR, 6, true);
  accelX = (float)(int16_t)(Wire.read()<<8|Wire.read()) / 16384.0f;
  accelY = (float)(int16_t)(Wire.read()<<8|Wire.read()) / 16384.0f;
  accelZ = (float)(int16_t)(Wire.read()<<8|Wire.read()) / 16384.0f;
}

void processSensorData(float &fx, float &fy, float &fz) {
  float cx = dpsX - offsetX - adaptive_offset_x;
  float cy = dpsY - offsetY - adaptive_offset_y;
  float cz = dpsZ - offsetZ;
  ema_x = emaFilter(ema_x, cx, ema_alpha);
  ema_y = emaFilter(ema_y, cy, ema_alpha);
  ema_z = emaFilter(ema_z, cz, ema_alpha);
  fx = applyAcceleration(applyDeadZone(kalmanUpdate(kalman_x, ema_x), DEAD_ZONE), ACCEL_THRESHOLD, ACCEL_MULTIPLIER);
  fy = applyAcceleration(applyDeadZone(kalmanUpdate(kalman_y, ema_y), DEAD_ZONE), ACCEL_THRESHOLD, ACCEL_MULTIPLIER);
  fz = applyAcceleration(applyDeadZone(kalmanUpdate(kalman_z, ema_z), DEAD_ZONE), ACCEL_THRESHOLD, ACCEL_MULTIPLIER);
}

void correctDrift() {
  unsigned long ct = millis();
  if (!movementEnabled && ct - last_drift_correction > DRIFT_CORRECTION_INTERVAL) {
    if (abs(ema_x) < 0.5 && abs(ema_y) < 0.5 && abs(ema_z) < 0.5) {
      adaptive_offset_x += ema_x * 0.1;
      adaptive_offset_y += ema_y * 0.1;
    }
    last_drift_correction = ct;
  }
}

// ===========================================
// BUTTON LOGIC
// ===========================================
void updateButton(Button &btn) {
  int reading = digitalRead(btn.pin);
  unsigned long ct = millis();
  if (reading != btn.lastState) btn.lastDebounceTime = ct;
  if ((ct - btn.lastDebounceTime) > btn.debounceDelay) {
    if (reading != btn.currentState) {
      btn.currentState = reading;
      if (btn.currentState == LOW) {
        btn.isPressed = true; btn.pressStartTime = ct;
      } else {
        btn.isPressed = false; btn.releaseTime = ct;
        if (btn.releaseTime - btn.pressStartTime < LONG_PRESS_THRESHOLD) {
          btn.clickCount++; btn.lastClickTime = ct; btn.clickProcessed = false;
        }
      }
    }
  }
  btn.lastState = reading;
}

enum ClickType { NO_CLICK, SINGLE_CLICK, DOUBLE_CLICK };
ClickType detectClickType(Button &btn) {
  unsigned long ct = millis();
  if (btn.clickCount > 0 && (ct - btn.lastClickTime) > CLICK_TIMEOUT) { btn.clickCount = 0; btn.clickProcessed = false; }
  if (btn.clickCount > 0 && !btn.clickProcessed) {
    if (btn.clickCount >= 2)                                              { btn.clickCount = 0; btn.clickProcessed = true; return DOUBLE_CLICK; }
    if (btn.clickCount == 1 && (ct - btn.lastClickTime) > DOUBLE_CLICK_WINDOW) { btn.clickCount = 0; btn.clickProcessed = true; return SINGLE_CLICK; }
  }
  return NO_CLICK;
}

void handleMovementControl() {
  updateButton(enableBtn);
  bool was = movementEnabled;
  movementEnabled = enableBtn.isPressed;
  if (movementEnabled && !was)  Serial.println("[Control] Mouse ENABLED");
  if (!movementEnabled && was)  Serial.println("[Control] Mouse DISABLED");
}

void handleMouseClicks() {
  if (detectClickType(enableBtn) == DOUBLE_CLICK) {
    Serial.println("[Mouse] Double-click → LEFT");
    Mouse.click(MOUSE_LEFT);
  }
}

// ===========================================
// LETTER RECOGNITION — data sent over WiFi TCP
// BLE HID is completely separate and unaffected.
// ===========================================
void handleLetterRecognition() {
  updateButton(letterBtn);
  unsigned long ct = millis();

  // ── Button pressed → open TCP socket and start recording ─────────────────
  if (letterBtn.isPressed && !letterModeActive) {
    if (!wifiIsConnected()) {
      Serial.println("[Letter] ⚠️  No WiFi — cannot send data. Check hotspot.");
      return;
    }

    if (!tcpConnect()) {
      Serial.println("[Letter] ⚠️  Python server unreachable. Is it running?");
      return;
    }

    letterModeActive = true;
    letterModeStart  = ct;

    tcpSendLn("LETTER_MODE_START");
    Serial.println("[Letter] Recording started — streaming over WiFi TCP");
  }

  // ── Recording in progress → stream CSV rows ───────────────────────────────
  if (letterModeActive) {
    unsigned long elapsed = ct - letterModeStart;

    if (elapsed < LETTER_RECORDING_DURATION) {
      calcRotation();
      calcAcceleration();

      char csv[80];
      snprintf(csv, sizeof(csv), "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f",
               accelX, accelY, accelZ, dpsX, dpsY, dpsZ);
      tcpSendLn(csv);

    } else {
      // ── Done → send end marker, close socket ─────────────────────────────
      tcpSendLn("LETTER_MODE_END");
      tcpDisconnect();

      letterModeActive = false;
      Serial.println("[Letter] Recording complete — sent to Python.");
    }
  }
}

// ===========================================
// MPU6050 HELPERS
// ===========================================
void writeMPU6050(byte reg, byte data) {
  Wire.beginTransmission(MPU6050_ADDR); Wire.write(reg); Wire.write(data); Wire.endTransmission();
}
byte readMPU6050(byte reg) {
  Wire.beginTransmission(MPU6050_ADDR); Wire.write(reg); Wire.endTransmission(true);
  Wire.requestFrom(MPU6050_ADDR, 1); return Wire.read();
}

// ===========================================
// PUBLIC
// ===========================================
bool  isMovementEnabled()  { return movementEnabled; }
bool  isLetterModeActive() { return letterModeActive; }
float getRawGyroZ()        { return dpsZ; }
void  getProcessedSensorData(float &fx, float &fy, float &fz) { calcRotation(); processSensorData(fx, fy, fz); }

// ===========================================
// SETUP
// ===========================================
void setupMouse() {
  pinMode(ENABLE_BTN, INPUT_PULLUP);
  pinMode(LETTER_BTN, INPUT_PULLUP);
  Wire.begin(); Wire.setClock(400000);
  Serial.begin(115200);
  Mouse.begin();

  Serial.println("\n╔═══════════════════════════════════════════╗");
  Serial.println("║       Accessibility Glove - Mouse Module  ║");
  Serial.println("╚═══════════════════════════════════════════╝");

  delay(100);
  Wire.beginTransmission(MPU6050_ADDR); Wire.write(0x6B); Wire.write(0); Wire.endTransmission(true);
  delay(100);

  Serial.print("MPU6050... ");
  if (readMPU6050(MPU6050_WHO_AM_I) != 0x68) { Serial.println("FAILED!"); while(true) delay(1000); }
  Serial.println("✓");

  writeMPU6050(MPU6050_SMPLRT_DIV, 0x00);
  writeMPU6050(MPU6050_CONFIG,      0x03);
  writeMPU6050(MPU6050_GYRO_CONFIG, 0x08);
  writeMPU6050(MPU6050_ACCEL_CONFIG,0x00);
  writeMPU6050(MPU6050_PWR_MGMT_1,  0x01);

  Serial.println("\n[Calibration] Keep hand STILL...");
  delay(1000);
  for (int i = 0; i < 2000; i++) {
    calcRotation();
    offsetX += dpsX; offsetY += dpsY; offsetZ += dpsZ;
    if (i % 200 == 0) Serial.print("█");
    delay(2);
  }
  Serial.println(" 100%");
  offsetX /= 2000; offsetY /= 2000; offsetZ /= 2000;
  Serial.println("✓ Calibration done.");
}

// ===========================================
// LOOP
// ===========================================
void loopMouse() {
  handleLetterRecognition();
  if (letterModeActive) return;

  float fx, fy, fz;
  getProcessedSensorData(fx, fy, fz);
  handleMovementControl();
  handleMouseClicks();
  correctDrift();

  if (Serial.available()) {
    char cmd = Serial.read();
    if      (cmd=='x') { x_sensitivity=Serial.parseFloat(); Serial.print("X sens: "); Serial.println(x_sensitivity); }
    else if (cmd=='y') { y_sensitivity=Serial.parseFloat(); Serial.print("Y sens: "); Serial.println(y_sensitivity); }
    else if (cmd=='d') { DEAD_ZONE    =Serial.parseFloat(); Serial.print("Dead zone: "); Serial.println(DEAD_ZONE); }
    else if (cmd=='r') {
      offsetX=0; offsetY=0; offsetZ=0;
      for(int i=0;i<1000;i++){calcRotation();offsetX+=dpsX;offsetY+=dpsY;offsetZ+=dpsZ;delay(2);}
      offsetX/=1000; offsetY/=1000; offsetZ/=1000;
      Serial.println("✓ Recalibrated.");
    } else if (cmd=='s') {
      Serial.printf("\nX=%.2f Y=%.2f DZ=%.2f Mode=%s\n", x_sensitivity, y_sensitivity, DEAD_ZONE, movementEnabled?"MOUSE":"GESTURE");
    }
    while(Serial.available()) Serial.read();
  }

  if (movementEnabled) {
    int mx = constrain((int)(-fz * x_sensitivity), -127, 127);
    int my = constrain((int)( fy * y_sensitivity), -127, 127);
    Mouse.move(mx, my, 0);
  }

  delay(10);
}