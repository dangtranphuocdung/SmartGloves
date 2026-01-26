// ===========================================
// LIBRARIES
// ===========================================
#include <BleCombo.h> 
#include <Wire.h>
#include <Arduino.h>

#define MPU6050_ADDR         0x68
#define MPU6050_SMPLRT_DIV   0x19
#define MPU6050_CONFIG       0x1a
#define MPU6050_GYRO_CONFIG  0x1b
#define MPU6050_ACCEL_CONFIG 0x1c
#define MPU6050_WHO_AM_I     0x75
#define MPU6050_PWR_MGMT_1   0x6b

// ===========================================
// SINGLE BUTTON CONFIGURATION
// ===========================================
#define ENABLE_BTN 18 

double offsetX = 0, offsetY = 0, offsetZ = 0;
float dpsX, dpsY, dpsZ;

// ===========================================
// FILTERING & VARIABLES
// ===========================================
float ema_alpha = 0.3;
float ema_x = 0, ema_y = 0, ema_z = 0;

struct KalmanFilter {
  float Q = 0.001; 
  float R = 0.03; 
  float P = 1.0; 
  float K = 0; 
  float X = 0;
};
KalmanFilter kalman_x, kalman_y, kalman_z;

// Movement Settings
float DEAD_ZONE = 2.0;
float ACCEL_THRESHOLD = 25.0;
float ACCEL_MULTIPLIER = 2.4;
float x_sensitivity = 0.5;
float y_sensitivity = 0.4;
float adaptive_offset_x = 0, adaptive_offset_y = 0;
unsigned long last_drift_correction = 0;
const unsigned long DRIFT_CORRECTION_INTERVAL = 5000;

// Gesture Settings
float SWIPE_THRESHOLD = 450;
unsigned long SWIPE_COOLDOWN = 800; 
unsigned long lastSwipeTime = 0;
bool swipeInProgress = false;

enum SwipeDirection { NO_SWIPE, SWIPE_LEFT, SWIPE_RIGHT };

// Button Struct
struct Button {
  int pin;
  bool lastState; 
  bool currentState;
  unsigned long lastDebounceTime; 
  unsigned long debounceDelay;
  bool isPressed; 
  unsigned long pressStartTime; 
  unsigned long releaseTime;
  unsigned long lastClickTime; 
  int clickCount; 
  bool clickProcessed;
  
  Button(int p = 0) {
    pin = p; 
    lastState = HIGH; 
    currentState = HIGH; 
    lastDebounceTime = 0; 
    debounceDelay = 50;
    isPressed = false; 
    pressStartTime = 0; 
    releaseTime = 0; 
    lastClickTime = 0; 
    clickCount = 0; 
    clickProcessed = false;
  }
};

Button enableBtn(ENABLE_BTN);
const unsigned long DOUBLE_CLICK_WINDOW = 400;
const unsigned long LONG_PRESS_THRESHOLD = 300;
const unsigned long CLICK_TIMEOUT = 500;
bool movementEnabled = false;

// ===========================================
// FILTER FUNCTIONS
// ===========================================
float kalmanUpdate(KalmanFilter &kf, float measurement) {
  kf.P = kf.P + kf.Q;
  kf.K = kf.P / (kf.P + kf.R);
  kf.X = kf.X + kf.K * (measurement - kf.X);
  kf.P = (1 - kf.K) * kf.P;
  return kf.X;
}

float emaFilter(float p, float c, float a) { 
  return a * c + (1 - a) * p; 
}

float applyDeadZone(float v, float t) {
  if (abs(v) < t) return 0;
  return (v > 0) ? (abs(v) - t) : -(abs(v) - t);
}

float applyAcceleration(float v, float t, float m) {
  if (abs(v) > t) return (v > 0) ? (t + (abs(v) - t) * m) : -(t + (abs(v) - t) * m);
  return v;
}

// ===========================================
// SENSOR FUNCTIONS
// ===========================================
void calcRotation() {
  Wire.beginTransmission(MPU6050_ADDR); 
  Wire.write(0x43); 
  Wire.endTransmission(false);
  Wire.requestFrom(MPU6050_ADDR, 6, true);
  int16_t rx = Wire.read() << 8 | Wire.read();
  int16_t ry = Wire.read() << 8 | Wire.read();
  int16_t rz = Wire.read() << 8 | Wire.read();
  dpsX = ((float)rx) / 65.5; 
  dpsY = ((float)ry) / 65.5; 
  dpsZ = ((float)rz) / 65.5;
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
  unsigned long currentTime = millis();
  
  if (!movementEnabled && currentTime - last_drift_correction > DRIFT_CORRECTION_INTERVAL) {
    if (abs(ema_x) < 0.5 && abs(ema_y) < 0.5 && abs(ema_z) < 0.5) {
      adaptive_offset_x += ema_x * 0.1;
      adaptive_offset_y += ema_y * 0.1;
      Serial.println("[Drift] Auto-correction applied");
    }
    last_drift_correction = currentTime;
  }
}

// ===========================================
// SWIPE DETECTION
// ===========================================
SwipeDirection detectSwipe(float raw_z) {
  unsigned long currentTime = millis();
  if (currentTime - lastSwipeTime < SWIPE_COOLDOWN) return NO_SWIPE;
  
  float corrected_z = raw_z - offsetZ;
  
  if (abs(corrected_z) > SWIPE_THRESHOLD) {
    if (!swipeInProgress) {
      swipeInProgress = true;
      lastSwipeTime = currentTime;
      if (corrected_z > SWIPE_THRESHOLD) return SWIPE_RIGHT;
      else if (corrected_z < -SWIPE_THRESHOLD) return SWIPE_LEFT;
    }
  } else {
    if (abs(corrected_z) < SWIPE_THRESHOLD / 2) swipeInProgress = false;
  }
  return NO_SWIPE;
}

void handleSwipeGestures(float raw_z) {
  if (!movementEnabled) {
    SwipeDirection swipe = detectSwipe(raw_z);
    
    if (swipe == SWIPE_RIGHT) {
      Serial.println("[Gesture] SWIPE RIGHT -> Arrow Right");
      Keyboard.press(KEY_RIGHT_ARROW);
      Keyboard.releaseAll();
    } else if (swipe == SWIPE_LEFT) {
      Serial.println("[Gesture] SWIPE LEFT -> Arrow Left");
      Keyboard.press(KEY_LEFT_ARROW);
      Keyboard.releaseAll();
    }
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
        btn.isPressed = true; 
        btn.pressStartTime = ct;
      } else {
        btn.isPressed = false; 
        btn.releaseTime = ct;
        if (btn.releaseTime - btn.pressStartTime < LONG_PRESS_THRESHOLD) {
          btn.clickCount++; 
          btn.lastClickTime = ct; 
          btn.clickProcessed = false;
          Serial.print("[Button] Click detected! Count: ");
          Serial.println(btn.clickCount);
        }
      }
    }
  }
  btn.lastState = reading;
}

enum ClickType {
  NO_CLICK,
  SINGLE_CLICK,
  DOUBLE_CLICK
};

ClickType detectClickType(Button &btn) {
  unsigned long currentTime = millis();
  
  if (btn.clickCount > 0 && (currentTime - btn.lastClickTime) > CLICK_TIMEOUT) {
    btn.clickCount = 0;
    btn.clickProcessed = false;
  }
  
  if (btn.clickCount > 0 && !btn.clickProcessed) {
    
    if (btn.clickCount >= 2) {
      btn.clickCount = 0;
      btn.clickProcessed = true;
      return DOUBLE_CLICK;
      
    } else if (btn.clickCount == 1) {
      if ((currentTime - btn.lastClickTime) > DOUBLE_CLICK_WINDOW) {
        btn.clickCount = 0;
        btn.clickProcessed = true;
        return SINGLE_CLICK;
      }
    }
  }
  
  return NO_CLICK;
}

void handleMovementControl() {
  updateButton(enableBtn);
  
  bool wasEnabled = movementEnabled;
  movementEnabled = enableBtn.isPressed;
  
  if (movementEnabled && !wasEnabled) {
    Serial.println("[Control] Movement ENABLED - Mouse active");
  } else if (!movementEnabled && wasEnabled) {
    Serial.println("[Control] Movement DISABLED - Gesture mode active");
  }
}

void handleMouseClicks() {
  ClickType clickType = detectClickType(enableBtn);
  
  if (clickType == DOUBLE_CLICK) {
    Serial.println("[Mouse] DOUBLE-CLICK → Left Mouse Button");
    Mouse.click(MOUSE_LEFT);
  }
}

// ===========================================
// MPU6050 HELPERS
// ===========================================
void writeMPU6050(byte reg, byte data) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);   
  Wire.write(data);
  Wire.endTransmission();
}

byte readMPU6050(byte reg) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);
  Wire.endTransmission(true);
  Wire.requestFrom(MPU6050_ADDR, 1); 
  return Wire.read();
}

// ===========================================
// SETUP
// ===========================================
void setupMouse() {
  pinMode(ENABLE_BTN, INPUT_PULLUP);
  
  Wire.begin(); 
  Wire.setClock(400000); 
  Serial.begin(115200);

  Keyboard.begin(); 
  Mouse.begin();
  
  Serial.println("\n╔═══════════════════════════════════════════╗");
  Serial.println("║  Accessibility Glove - PHASE 1 COMPLETE   ║");
  Serial.println("╚═══════════════════════════════════════════╝");
  Serial.println("║                                           ║");
  Serial.println("║  MOUSE MODE (Button Held):                ║");
  Serial.println("║  • Hold button     → Move cursor          ║");
  Serial.println("║  • Double-click    → Left Mouse Button    ║");
  Serial.println("║                                           ║");
  Serial.println("║  GESTURE MODE (Button Released):          ║");
  Serial.println("║  • Swipe RIGHT     → Right Arrow Key      ║");
  Serial.println("║  • Swipe LEFT      → Left Arrow Key       ║");
  Serial.println("║                                           ║");
  Serial.println("╚═══════════════════════════════════════════╝\n");
  
  delay(100);
  
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x6B); 
  Wire.write(0); 
  Wire.endTransmission(true);
  
  delay(100);
  
  Serial.print("Checking MPU6050... ");
  if (readMPU6050(MPU6050_WHO_AM_I) != 0x68) {
    Serial.println("FAILED!");
    Serial.println("ERROR: MPU6050 not detected!");
    while (true) delay(1000);
  }
  Serial.println("✓ Connected");

  writeMPU6050(MPU6050_SMPLRT_DIV, 0x00);
  writeMPU6050(MPU6050_CONFIG, 0x03);
  writeMPU6050(MPU6050_GYRO_CONFIG, 0x08);
  writeMPU6050(MPU6050_ACCEL_CONFIG, 0x00);
  writeMPU6050(MPU6050_PWR_MGMT_1, 0x01);

  Serial.println("\n╔═══════════════════════════════════════════╗");
  Serial.println("║          CALIBRATION IN PROGRESS          ║");
  Serial.println("║     Keep your hand COMPLETELY STILL!      ║");
  Serial.println("╚═══════════════════════════════════════════╝");
  
  delay(1000);
  
  for (int i = 0; i < 2000; i++) {
    calcRotation();
    offsetX += dpsX;
    offsetY += dpsY;
    offsetZ += dpsZ;
    
    if (i % 200 == 0) {
      Serial.print("█");
    }
    
    delay(2);
  }
  Serial.println(" 100%");

  offsetX /= 2000;
  offsetY /= 2000;
  offsetZ /= 2000;
  
  Serial.println("\n✓ Calibration Complete!");
  Serial.print("Offsets → X: "); Serial.print(offsetX, 2);
  Serial.print(" | Y: "); Serial.print(offsetY, 2);
  Serial.print(" | Z: "); Serial.println(offsetZ, 2);
  
  Serial.println("\n╔═══════════════════════════════════════════╗");
  Serial.println("║         WAITING FOR BLUETOOTH...          ║");
  Serial.println("╚═══════════════════════════════════════════╝");
}

// ===========================================
// MAIN LOOP
// ===========================================
void loopMouse() {
  calcRotation();
  
  float fx, fy, fz;
  processSensorData(fx, fy, fz);
  
  handleMovementControl();
  handleMouseClicks();
  handleSwipeGestures(dpsZ);
  correctDrift();
  
  // Serial commands
  if (Serial.available()) {
    char cmd = Serial.read();
    
    if (cmd == 'x') {
      x_sensitivity = Serial.parseFloat();
      Serial.print("→ X sensitivity: "); Serial.println(x_sensitivity);
    } else if (cmd == 'y') {
      y_sensitivity = Serial.parseFloat();
      Serial.print("→ Y sensitivity: "); Serial.println(y_sensitivity);
    } else if (cmd == 'd') {
      DEAD_ZONE = Serial.parseFloat();
      Serial.print("→ Dead zone: "); Serial.println(DEAD_ZONE);
    } else if (cmd == 'g') {
      SWIPE_THRESHOLD = Serial.parseFloat();
      Serial.print("→ Swipe threshold: "); Serial.println(SWIPE_THRESHOLD);
    } else if (cmd == 'r') {
      Serial.println("→ Recalibrating...");
      offsetX = 0; offsetY = 0; offsetZ = 0;
      for (int i = 0; i < 1000; i++) {
        calcRotation();
        offsetX += dpsX;
        offsetY += dpsY;
        offsetZ += dpsZ;
        delay(2);
      }
      offsetX /= 1000; offsetY /= 1000; offsetZ /= 1000;
      Serial.println("✓ Done!");
    } else if (cmd == 's') {
      Serial.println("\n═══ SETTINGS ═══");
      Serial.print("X Sensitivity: "); Serial.println(x_sensitivity);
      Serial.print("Y Sensitivity: "); Serial.println(y_sensitivity);
      Serial.print("Dead Zone: "); Serial.println(DEAD_ZONE);
      Serial.print("Swipe Threshold: "); Serial.println(SWIPE_THRESHOLD);
      Serial.print("Mode: "); Serial.println(movementEnabled ? "MOUSE" : "GESTURE");
      Serial.println("════════════════\n");
    }
    
    while (Serial.available()) Serial.read();
  }
  
  if (Keyboard.isConnected()) {
    
    if (movementEnabled) {
      int mx = -fz * x_sensitivity;
      int my = fy * y_sensitivity;
      mx = constrain(mx, -127, 127);
      my = constrain(my, -127, 127);
      Mouse.move(mx, my, 0);
    }
    
  } else {
    static unsigned long lastStatus = 0;
    if (millis() - lastStatus > 3000) {
      Serial.println("[BLE] Waiting for connection...");
      lastStatus = millis();
    }
  }
  
  delay(10);
}