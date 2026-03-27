// ===========================================
// MAIN PROGRAM
// Architecture:
//   BLE HID  → mouse + keyboard (direct to Windows, zero latency)
//   WiFi TCP → sensor data → Python → Modal → action
// SCL: 21  SDA: 22
// ===========================================

#include <Wire.h>
#include <BleCombo.h>
#include <Arduino.h>
#include "MouseControl/mouse-control.h"
#include "GestureControl/gesture-control.h"
#include "WiFiTcp/wifi-tcp.h"              // ← replaces BleUart

void setup() {
  Serial.begin(115200);

  Serial.println("\n╔═══════════════════════════════════════════╗");
  Serial.println("║    ACCESSIBILITY GLOVE - INITIALIZING     ║");
  Serial.println("╚═══════════════════════════════════════════╝\n");

  // 1. Mouse module — starts BleCombo HID + calibrates MPU6050
  setupMouse();

  // 2. Gesture module
  setupGestures();

  // 3. WiFi TCP — connects to hotspot for ML inference channel
  //    (runs after BleCombo so BLE is already advertising)
  setupWiFiTcp();

  Serial.println("\n╔═══════════════════════════════════════════╗");
  Serial.println("║              SYSTEM READY                 ║");
  Serial.println("╠═══════════════════════════════════════════╣");
  Serial.println("║  BLE HID  → mouse + keyboard (wireless)  ║");
  Serial.println("║  WiFi TCP → letter ML inference           ║");
  Serial.println("╠═══════════════════════════════════════════╣");
  Serial.println("║  Hold ENABLE btn  → mouse mode + swipe   ║");
  Serial.println("║  Double-click     → left mouse button     ║");
  Serial.println("║  Hold LETTER btn  → record gesture        ║");
  Serial.println("╚═══════════════════════════════════════════╝\n");
}

void loop() {
  loopMouse();
  loopGestures();

  // Periodic status log (USB serial only, for debugging)
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 8000) {
    Serial.printf("[Status] BLE=%s  WiFi=%s\n",
      Keyboard.isConnected() ? "✓" : "waiting",
      wifiIsConnected()      ? "✓" : "disconnected"
    );
    lastCheck = millis();
  }
}