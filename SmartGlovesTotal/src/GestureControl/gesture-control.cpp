// ===========================================
// GESTURE CONTROL MODULE
// Handles: Swipe detection and arrow key presses
// ===========================================

#include "gesture-control.h"
#include "MouseControl/mouse-control.h"
#include <BleCombo.h>
#include <Arduino.h>

// ===========================================
// GESTURE SETTINGS
// ===========================================
float SWIPE_THRESHOLD = 450;
unsigned long SWIPE_COOLDOWN = 800; 
unsigned long lastSwipeTime = 0;
bool swipeInProgress = false;

enum SwipeDirection { 
  NO_SWIPE, 
  SWIPE_LEFT, 
  SWIPE_RIGHT 
};

// ===========================================
// SWIPE DETECTION
// ===========================================
SwipeDirection detectSwipe(float raw_z, float offset_z) {
  unsigned long currentTime = millis();
  
  // Check cooldown
  if (currentTime - lastSwipeTime < SWIPE_COOLDOWN) {
    return NO_SWIPE;
  }
  
  float corrected_z = raw_z - offset_z;
  
  // Detect strong movement
  if (abs(corrected_z) > SWIPE_THRESHOLD) {
    if (!swipeInProgress) {
      swipeInProgress = true;
      lastSwipeTime = currentTime;
      
      if (corrected_z > SWIPE_THRESHOLD) {
        return SWIPE_RIGHT;
      } else if (corrected_z < -SWIPE_THRESHOLD) {
        return SWIPE_LEFT;
      }
    }
  } else {
    // Reset swipe flag when movement is small
    if (abs(corrected_z) < SWIPE_THRESHOLD / 2) {
      swipeInProgress = false;
    }
  }
  
  return NO_SWIPE;
}

// ===========================================
// GESTURE HANDLERS
// ===========================================
void handleSwipeGestures(float raw_z, float offset_z) {
  // Only process gestures when mouse movement is disabled
  if (!isMovementEnabled()) {
    SwipeDirection swipe = detectSwipe(raw_z, offset_z);
    
    if (swipe == SWIPE_RIGHT) {
      Serial.println("[Gesture] SWIPE RIGHT → Arrow Right");
      Keyboard.press(KEY_RIGHT_ARROW);
      Keyboard.releaseAll();
    } 
    else if (swipe == SWIPE_LEFT) {
      Serial.println("[Gesture] SWIPE LEFT → Arrow Left");
      Keyboard.press(KEY_LEFT_ARROW);
      Keyboard.releaseAll();
    }
  }
}

// ===========================================
// SETUP
// ===========================================
void setupGestures() {
  Keyboard.begin();
  
  Serial.println("\n╔═══════════════════════════════════════════╗");
  Serial.println("║     Accessibility Glove - Gesture Module  ║");
  Serial.println("╚═══════════════════════════════════════════╝");
  Serial.println("║                                           ║");
  Serial.println("║  GESTURE MODE (Button Released):          ║");
  Serial.println("║  • Swipe RIGHT     → Right Arrow Key      ║");
  Serial.println("║  • Swipe LEFT      → Left Arrow Key       ║");
  Serial.println("║                                           ║");
  Serial.println("╚═══════════════════════════════════════════╝\n");
}

// ===========================================
// MAIN LOOP
// ===========================================
void loopGestures() {
  // Get raw gyro Z value from mouse control module
  float raw_z = getRawGyroZ();
  
  // We need the offset, but it's private in mouse-control
  // For now, we'll use 0 and let detectSwipe handle it
  // Alternatively, expose offset through mouse-control.h
  static float offset_z = 0;
  
  handleSwipeGestures(raw_z, offset_z);
}

// ===========================================
// CONFIGURATION
// ===========================================
void setSwipeThreshold(float threshold) {
  SWIPE_THRESHOLD = threshold;
  Serial.print("→ Swipe threshold: "); 
  Serial.println(SWIPE_THRESHOLD);
}

void setSwipeCooldown(unsigned long cooldown) {
  SWIPE_COOLDOWN = cooldown;
  Serial.print("→ Swipe cooldown: "); 
  Serial.println(SWIPE_COOLDOWN);
}

void printGestureSettings() {
  Serial.println("\n═══ GESTURE SETTINGS ═══");
  Serial.print("Swipe Threshold: "); 
  Serial.println(SWIPE_THRESHOLD);
  Serial.print("Swipe Cooldown: "); 
  Serial.println(SWIPE_COOLDOWN);
  Serial.println("════════════════════════\n");
}