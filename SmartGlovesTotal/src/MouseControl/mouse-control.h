// ===========================================
// MOUSE CONTROL HEADER
// Public interface for mouse functionality
// ===========================================

#ifndef MOUSE_CONTROL_H
#define MOUSE_CONTROL_H

#include <Arduino.h>

// Setup function - call once in setup()
void setupMouse();

// Loop function - call continuously in loop()
void loopMouse();

// Query functions for other modules
bool isMovementEnabled();
void getProcessedSensorData(float &fx, float &fy, float &fz);
float getRawGyroZ();

#endif