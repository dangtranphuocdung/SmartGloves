// ===========================================
// MAIN PROGRAM
// Orchestrates mouse and gesture control modules
// ===========================================
// SCL: 21, SDA:22

#include <Wire.h>
#include <BleCombo.h>
#include <Arduino.h>
#include "MouseControl/mouse-control.h"
#include "GestureControl/gesture-control.h"

void setup() {
  Serial.begin(115200);
  
  Serial.println("\n╔═══════════════════════════════════════════╗");
  Serial.println("║    ACCESSIBILITY GLOVE - INITIALIZING     ║");
  Serial.println("╚═══════════════════════════════════════════╝\n");
  
  // Initialize mouse control (includes MPU6050 setup)
  setupMouse();
  
  // Initialize gesture control
  setupGestures();
  
  Serial.println("\n╔═══════════════════════════════════════════╗");
  Serial.println("║         SYSTEM READY - USAGE GUIDE        ║");
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
  Serial.println("║  SERIAL COMMANDS:                         ║");
  Serial.println("║  • 'x' + number    → Set X sensitivity    ║");
  Serial.println("║  • 'y' + number    → Set Y sensitivity    ║");
  Serial.println("║  • 'd' + number    → Set dead zone        ║");
  Serial.println("║  • 'g' + number    → Set swipe threshold  ║");
  Serial.println("║  • 'r'             → Recalibrate sensor   ║");
  Serial.println("║  • 's'             → Show all settings    ║");
  Serial.println("║                                           ║");
  Serial.println("╚═══════════════════════════════════════════╝\n");
  
  Serial.println("Waiting for Bluetooth connection...\n");
}

void loop() {
  // Handle mouse movement and button detection
  loopMouse();
  
  // Handle swipe gestures
  loopGestures();
  
  // Check BLE connection status
  static unsigned long lastStatusCheck = 0;
  if (millis() - lastStatusCheck > 5000) {
    if (!Keyboard.isConnected()) {
      Serial.println("[BLE] Waiting for connection...");
    }
    lastStatusCheck = millis();
  }
}