// ===========================================
// WIFI TCP MODULE
// Connects ESP32 to hotspot and streams
// sensor data to Python over TCP.
//
// Flow:
//   1. ESP32 connects to WiFi (SSID/password set here)
//   2. On letter button press, ESP32 opens TCP socket to PC_IP:PORT
//   3. Sends "LETTER_MODE_START\n", CSV rows, "LETTER_MODE_END\n"
//   4. Closes socket — Python handles inference + action
//
// BLE HID stays completely independent (mouse/keyboard).
// ===========================================

#ifndef WIFI_TCP_H
#define WIFI_TCP_H

#include <Arduino.h>

// ── USER CONFIG — edit these before flashing ─────────────────────────────────
#define WIFI_SSID     "lmaos"       // hotspot SSID
#define WIFI_PASSWORD "shortasscutie"   // hotspot password
#define PC_IP         "10.98.174.12"           // your PC's IP on the hotspot
#define PC_PORT       9000                    // must match Python server port
// ─────────────────────────────────────────────────────────────────────────────

// Call once in setup() — blocks until WiFi is connected
void setupWiFiTcp();

// Returns true if WiFi is currently connected
bool wifiIsConnected();

// Open a fresh TCP connection to the PC.
// Returns true on success. Call before a recording session.
bool tcpConnect();

// Send a null-terminated string over the open TCP socket.
// Returns false if the socket is not connected.
bool tcpSend(const char* msg);

// Same as tcpSend but appends "\n"
bool tcpSendLn(const char* msg);

// Close the TCP socket gracefully. Call after LETTER_MODE_END is sent.
void tcpDisconnect();

#endif