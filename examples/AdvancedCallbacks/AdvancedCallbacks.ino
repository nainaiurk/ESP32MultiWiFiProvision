/*
 * ESP32MultiWiFiProvision - Advanced Callbacks
 *
 * Shows how to use callbacks and status monitoring for
 * event-driven applications. No polling needed!
 *
 * This example demonstrates:
 *   - onConnected() callback — fires once when WiFi connects
 *   - setStatusCallback()   — real-time status messages
 *   - getStatus()           — detailed connection state enum
 *   - getStatusMessage()    — human-readable status string
 *   - Blocking connect()    — simple synchronous connection
 */

#include <ESP32MultiWiFiProvision.h>

ESP32MultiWiFiProvision wifiConfig;

#define PORTAL_BUTTON  12

// ─── Status Callback ─────────────────────────────────────
// Fires on every state change (connecting, scanning, etc.)
void onStatusUpdate(const char* message) {
    Serial.print("  [Status] ");
    Serial.println(message);
}

// ─── Connected Callback ──────────────────────────────────
// Fires ONCE when WiFi successfully connects.
// Use this to start your main application logic.
void onWiFiConnected(String ssid) {
    Serial.println("\n╔═══════════════════════════════╗");
    Serial.println("║      WiFi Connected!          ║");
    Serial.println("╠═══════════════════════════════╣");
    Serial.print("║  SSID: ");
    Serial.println(ssid);
    Serial.print("║  IP:   ");
    Serial.println(WiFi.localIP());
    Serial.print("║  RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    Serial.println("╚═══════════════════════════════╝");

    // ── Start your app here ──
    // For example: connect to MQTT, start HTTP server, sync time, etc.
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== Advanced Callbacks Example ===");

    pinMode(PORTAL_BUTTON, INPUT_PULLDOWN);

    // ── Configuration ──
    wifiConfig.setConnectTimeout(8000);
    wifiConfig.setAutoReconnect(true);
    wifiConfig.setReconnectInterval(15000);
    wifiConfig.prioritizeLastConnected();

    // ── Register Callbacks ──
    wifiConfig.setStatusCallback(onStatusUpdate);
    wifiConfig.onConnected(onWiFiConnected);

    // ── Initialize (no auto-connect, we'll use blocking connect) ──
    wifiConfig.begin("ESP32-Setup", NULL, false);

    // ── Blocking Connect ──
    // Waits up to 30 seconds, then falls back to portal.
    Serial.println("Connecting to saved networks...");

    if (wifiConfig.connect(30000)) {
        // onWiFiConnected callback already fired above
        Serial.println("Ready!");
    } else {
        Serial.println("Connection failed. Opening portal...");
        wifiConfig.startPortal();
        Serial.print("Connect to 'ESP32-Setup' and go to: ");
        Serial.println(WiFi.softAPIP());
    }
}

void loop() {
    wifiConfig.run();

    // Press button to re-open portal
    if (digitalRead(PORTAL_BUTTON) == HIGH) {
        delay(50);
        if (digitalRead(PORTAL_BUTTON) == HIGH) {
            Serial.println("\nOpening portal...");
            wifiConfig.startPortal();
            while (digitalRead(PORTAL_BUTTON) == HIGH) delay(10);
        }
    }

    // ── Periodic Status Report ──
    // Shows how to use getStatus() and getStatusMessage()
    static unsigned long lastReport = 0;
    if (millis() - lastReport > 30000) {
        lastReport = millis();

        Serial.print("\n[Report] ");
        Serial.println(wifiConfig.getStatusMessage());

        // You can also check the enum directly:
        ConnectionStatus status = wifiConfig.getStatus();
        switch (status) {
            case STATUS_CONNECTED:
                Serial.print("  Signal: ");
                Serial.print(WiFi.RSSI());
                Serial.println(" dBm");
                break;
            case STATUS_CONNECTING:
                Serial.println("  Still trying...");
                break;
            case STATUS_DISCONNECTED:
                Serial.println("  Waiting for auto-reconnect...");
                break;
            case STATUS_NO_SAVED_NETWORKS:
                Serial.println("  No credentials saved. Open portal to add.");
                break;
            default:
                break;
        }
    }
}
