// ===========================================
// GESTURE CONTROL HEADER
// Public interface for gesture functionality
// ===========================================

#ifndef GESTURE_CONTROL_H
#define GESTURE_CONTROL_H

#include <Arduino.h>

// Setup function - call once in setup()
void setupGestures();

// Loop function - call continuously in loop()
void loopGestures();

// Configuration functions
void setSwipeThreshold(float threshold);
void setSwipeCooldown(unsigned long cooldown);
void printGestureSettings();

#endif