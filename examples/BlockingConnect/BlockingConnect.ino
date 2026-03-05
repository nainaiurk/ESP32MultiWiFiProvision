/*
 * ESP32MultiWiFiProvision - Blocking Connect Example
 * 
 * Simple blocking approach — waits for WiFi connection in setup().
 * Falls back to portal if connection fails.
 * 
 * Best for: projects that MUST have WiFi before proceeding.
 */

#include <ESP32MultiWiFiProvision.h>

ESP32MultiWiFiProvision wifiConfig;

void setup() {
    Serial.begin(115200);
    delay(1000);

    // --- Configuration ---
    wifiConfig.setConnectTimeout(8000);
    wifiConfig.prioritizeLastConnected();

    // --- Callback: fires once when connected ---
    wifiConfig.onConnected([](String ssid) {
        Serial.println("✓ Connected to: " + ssid);
        Serial.println("  IP: " + WiFi.localIP().toString());
    });

    // --- Initialize (no auto-connect, we'll use connect()) ---
    wifiConfig.begin("MyDevice-Setup", NULL, false);

    // --- Blocking connect: waits up to 30 seconds ---
    Serial.println("Connecting to saved networks...");
    if (wifiConfig.connect(30000)) {
        Serial.println("WiFi ready!");
        // Do your one-time connected setup here
    } else {
        Serial.println("Connection failed. Opening portal...");
        wifiConfig.startPortal();
        Serial.print("Connect to AP and go to: ");
        Serial.println(WiFi.softAPIP());
    }
}

void loop() {
    // REQUIRED: handles portal, reconnection, and state machine
    wifiConfig.run();
}

