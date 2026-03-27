// ===========================================
// WIFI TCP MODULE — Implementation
// ESP32-WROOM-32E: WiFi and BLE share one
// radio but coexist fine at low BLE duty cycle.
// ===========================================

#include "wifi-tcp.h"
#include <WiFi.h>

static WiFiClient _client;

// ── Setup ─────────────────────────────────────────────────────────────────────
void setupWiFiTcp() {
  Serial.println("\n[WiFi] Connecting to: " WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
    // Timeout after 15 s — don't block BLE setup forever
    if (millis() - start > 15000) {
      Serial.println("\n[WiFi] ⚠️  Timeout — continuing without WiFi.");
      Serial.println("[WiFi]    Letter recognition will be unavailable.");
      return;
    }
  }

  Serial.println();
  Serial.println("[WiFi] ✓ Connected!");
  Serial.print("[WiFi] IP: "); Serial.println(WiFi.localIP());
  Serial.print("[WiFi] PC target: " PC_IP ":"); Serial.println(PC_PORT);
}

bool wifiIsConnected() {
  return WiFi.status() == WL_CONNECTED;
}

// ── TCP connect ───────────────────────────────────────────────────────────────
bool tcpConnect() {
  if (!wifiIsConnected()) {
    Serial.println("[TCP] No WiFi — skipping connection.");
    return false;
  }

  if (_client.connected()) _client.stop();   // close stale socket

  Serial.println("[TCP] Connecting to Python server...");
  if (!_client.connect(PC_IP, PC_PORT)) {
    Serial.println("[TCP] ❌ Connection failed. Is Python server running?");
    return false;
  }

  Serial.println("[TCP] ✓ Connected to Python server.");
  return true;
}

// ── Send ──────────────────────────────────────────────────────────────────────
bool tcpSend(const char* msg) {
  if (!_client.connected()) return false;
  _client.print(msg);
  return true;
}

bool tcpSendLn(const char* msg) {
  if (!_client.connected()) return false;
  _client.println(msg);   // println appends \r\n — Python strips it
  return true;
}

// ── Disconnect ────────────────────────────────────────────────────────────────
void tcpDisconnect() {
  if (_client.connected()) {
    _client.stop();
    Serial.println("[TCP] Socket closed.");
  }
}