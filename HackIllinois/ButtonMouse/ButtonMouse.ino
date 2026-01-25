#include <Wire.h>
#include <BleMouse.h>

// --- PIN DEFINITIONS ---
#define SDA_PIN_MPU 21
#define SCL_PIN_MPU 22
#define LEFT_CLICK_PIN 16
#define RIGHT_CLICK_PIN 17
#define SCROLL_PIN 5
#define STOP_BUTTON_PIN 18      // NEW: Hold to stop, double-click to left-click
#define TOUCH_ENABLE T0

// --- MPU SETTINGS ---
#define MPU_ADDR 0x68
#define MPU_GYRO_CONFIG 0x1B
#define MPU_PWR_MGMT 0x6B

// --- TUNING (Adjust these for your preference) ---
#define GYRO_DEADZONE 1.5      // Ignore small movements
#define SENSITIVITY_X 0.1      // Horizontal speed (try 0.8-2.0)
#define SENSITIVITY_Y 0.1      // Vertical speed (try 0.8-2.0)
#define SCROLL_SPEED 0.15      // Scroll sensitivity (try 0.1-0.3)

BleMouse bleMouse;

// --- VARIABLES ---
int16_t gyroX, gyroY, gyroZ;
float gx, gy, gz;
float offsetGx = 0, offsetGy = 0, offsetGz = 0;

// Smoothing
float smoothZ = 0, smoothY = 0;
float residueX = 0, residueY = 0;

// Stop button variables
bool mouseMovementEnabled = true;  // False when stop button is held
unsigned long lastStopPress = 0;
unsigned long lastStopRelease = 0;
int stopClickCount = 0;
#define DOUBLE_CLICK_TIME 300  // milliseconds

void setup() {
  Serial.begin(115200);

  // Setup Pins
  pinMode(LEFT_CLICK_PIN, INPUT_PULLUP);
  pinMode(RIGHT_CLICK_PIN, INPUT_PULLUP);
  pinMode(SCROLL_PIN, INPUT_PULLUP);
  pinMode(STOP_BUTTON_PIN, INPUT_PULLUP);  // NEW

  // Setup MPU6050
  Wire.begin(SDA_PIN_MPU, SCL_PIN_MPU);
  Wire.setClock(400000);
  
  setupMPU();

  // Calibration
  Serial.println("Calibrating... Keep still!");
  calibrateMPU();
  Serial.println("Calibration Done!");
  Serial.print("Offsets - X:");
  Serial.print(offsetGx);
  Serial.print(" Y:");
  Serial.print(offsetGy);
  Serial.print(" Z:");
  Serial.println(offsetGz);

  // Start BLE
  bleMouse.begin();
  Serial.println("BLE Mouse started. Waiting for connection...");
}

void loop() {
  if (bleMouse.isConnected()) {
    
    // --- READ SENSOR ---
    readMPU();

    // --- DEADZONE (eliminates drift) ---
    if (abs(gx) < GYRO_DEADZONE) gx = 0;
    if (abs(gy) < GYRO_DEADZONE) gy = 0;
    if (abs(gz) < GYRO_DEADZONE) gz = 0;

    // --- DYNAMIC SMOOTHING ---
    // Fast movement = responsive (alpha 0.85)
    // Slow movement = smooth (alpha 0.2)
    float speed = sqrt(gz*gz + gy*gy);
    float alpha = (speed > 15) ? 0.85 : 0.3;

    smoothZ = (alpha * gz) + ((1.0 - alpha) * smoothZ);
    smoothY = (alpha * gy) + ((1.0 - alpha) * smoothY);

    // --- MOVEMENT (when touch active AND stop button NOT held) ---
    if (touchRead(TOUCH_ENABLE) > 30 && mouseMovementEnabled) { 
      
      float moveX = -(smoothZ) * SENSITIVITY_X;
      float moveY = (smoothY) * SENSITIVITY_Y;

      // Add residue for sub-pixel precision
      moveX += residueX;
      moveY += residueY;

      int intMoveX = (int)moveX;
      int intMoveY = (int)moveY;

      residueX = moveX - intMoveX;
      residueY = moveY - intMoveY;

      if (intMoveX != 0 || intMoveY != 0) {
        bleMouse.move(intMoveX, intMoveY, 0);
      }
    }

    // --- SCROLLING ---
    if (digitalRead(SCROLL_PIN) == LOW) {
      if (abs(smoothY) > 3) {
         int scrollAmount = (int)(smoothY * SCROLL_SPEED);
         if (scrollAmount != 0) {
           bleMouse.move(0, 0, scrollAmount);
         }
      }
      delay(5); // Prevent scroll spam
    }

    // --- CLICK HANDLING ---
    handleStopButton();  // NEW: Handle stop/double-click button
    handleClicks();
  
    // Minimal delay for I2C stability
    delayMicroseconds(1000); // 1ms
  }
}

// --- HELPERS ---

void setupMPU() {
  // Wake up MPU
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(MPU_PWR_MGMT);
  Wire.write(0x00); 
  Wire.endTransmission();

  // Set gyro range to ±500 dps
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(MPU_GYRO_CONFIG);
  Wire.write(0x08);
  Wire.endTransmission();
  
  delay(100);
}

void calibrateMPU() {
  long sumX = 0, sumY = 0, sumZ = 0;
  int samples = 500; // Reduced from 1000
  
  for (int i = 0; i < samples; i++) {
    readRawMPU();
    sumX += gyroX;
    sumY += gyroY;
    sumZ += gyroZ;
    delay(3); // MUCH faster - was 100ms!
    
    // Progress indicator
    if (i % 100 == 0) {
      Serial.print(".");
    }
  }
  Serial.println();
  
  offsetGx = (sumX / (float)samples) / 65.5;
  offsetGy = (sumY / (float)samples) / 65.5;
  offsetGz = (sumZ / (float)samples) / 65.5;
}

void readRawMPU() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x43); // Gyro registers start
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 6, true);
  
  gyroX = Wire.read() << 8 | Wire.read();
  gyroY = Wire.read() << 8 | Wire.read();
  gyroZ = Wire.read() << 8 | Wire.read();
}

void readMPU() {
  readRawMPU();
  gx = (gyroX / 65.5) - offsetGx;
  gy = (gyroY / 65.5) - offsetGy;
  gz = (gyroZ / 65.5) - offsetGz;
}

void handleClicks() {
  static bool leftState = false;
  static bool rightState = false;

  bool currentLeft = (digitalRead(LEFT_CLICK_PIN) == LOW);
  bool currentRight = (digitalRead(RIGHT_CLICK_PIN) == LOW);

  // Left click
  if (currentLeft != leftState) {
    if (currentLeft) {
      bleMouse.press(MOUSE_LEFT);
    } else {
      bleMouse.release(MOUSE_LEFT);
    }
    leftState = currentLeft;
  }

  // Right click
  if (currentRight != rightState) {
    if (currentRight) {
      bleMouse.press(MOUSE_RIGHT);
    } else {
      bleMouse.release(MOUSE_RIGHT);
    }
    rightState = currentRight;
  }
}

void handleStopButton() {
  static bool lastStopState = HIGH;
  bool currentStopState = digitalRead(STOP_BUTTON_PIN);
  unsigned long currentTime = millis();
  
  // Button pressed (LOW because INPUT_PULLUP)
  if (currentStopState == LOW && lastStopState == HIGH) {
    // Record press time
    lastStopPress = currentTime;
    
    // Disable mouse movement while held
    mouseMovementEnabled = false;
    
    Serial.println("Stop button pressed - movement disabled");
  }
  
  // Button released
  else if (currentStopState == HIGH && lastStopState == LOW) {
    // Record release time
    lastStopRelease = currentTime;
    
    // Re-enable mouse movement
    mouseMovementEnabled = true;
    
    // Check for double-click
    if (stopClickCount == 0) {
      // First click
      stopClickCount = 1;
      Serial.println("Stop button - first click");
    } else if (stopClickCount == 1 && (currentTime - lastStopRelease < DOUBLE_CLICK_TIME)) {
      // Second click within time window - trigger left click
      bleMouse.click(MOUSE_LEFT);
      stopClickCount = 0;
      Serial.println("Stop button - DOUBLE CLICK! Left click triggered");
    }
  }
  
  // Reset click count if too much time has passed
  if (stopClickCount > 0 && (currentTime - lastStopRelease > DOUBLE_CLICK_TIME)) {
    stopClickCount = 0;
    Serial.println("Stop button - timeout, reset");
  }
  
  lastStopState = currentStopState;
}