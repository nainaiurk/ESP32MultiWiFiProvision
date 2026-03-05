/*
 * ESP32MultiWiFiProvision - Basic Provision
 *
 * The simplest way to get started. This example:
 *   1. Tries to connect to any previously saved WiFi network.
 *   2. If no saved network is found (first boot), opens a captive portal.
 *   3. User connects to the AP, picks a network, and enters the password.
 *   4. Credentials are saved and the device connects automatically.
 *
 * Wiring: Connect a button between GPIO 12 and 3.3V (with INPUT_PULLDOWN).
 *         Press this button at any time to re-open the portal.
 */

#include <ESP32MultiWiFiProvision.h>

ESP32MultiWiFiProvision wifiConfig;

#define PORTAL_BUTTON  12

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== ESP32 WiFi Provision ===");

    pinMode(PORTAL_BUTTON, INPUT_PULLDOWN);

    // Initialize and auto-connect to any saved network.
    // If no saved network exists, the captive portal starts automatically.
    //   "ESP32-Setup"  = Name of the AP that appears in your phone's WiFi list
    //   NULL           = No password for the AP (open network)
    //   true           = Auto-connect on boot
    wifiConfig.begin("ESP32-Setup", NULL, true);

    // Check result
    if (wifiConfig.isConnected()) {
        Serial.print("✓ Connected to: ");
        Serial.println(WiFi.SSID());
        Serial.print("  IP Address:   ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("Portal is open!");
        Serial.println("  1. Connect your phone to 'ESP32-Setup' WiFi");
        Serial.println("  2. A setup page will open automatically");
        Serial.println("  3. Pick your WiFi network and enter the password");
    }
}

void loop() {
    // REQUIRED — must be called every loop
    wifiConfig.run();

    // Press button to re-open the portal at any time
    if (digitalRead(PORTAL_BUTTON) == HIGH) {
        delay(50);  // debounce
        if (digitalRead(PORTAL_BUTTON) == HIGH) {
            Serial.println("\nButton pressed — opening WiFi portal...");
            wifiConfig.startPortal();
            while (digitalRead(PORTAL_BUTTON) == HIGH) delay(10);
        }
    }

    // Simple connection monitor
    static bool wasConnected = false;
    if (wifiConfig.isConnected() && !wasConnected) {
        wasConnected = true;
        Serial.println("\n✓ WiFi Connected!");
        Serial.print("  SSID: ");
        Serial.println(WiFi.SSID());
        Serial.print("  IP:   ");
        Serial.println(WiFi.localIP());
    }
    if (!wifiConfig.isConnected() && wasConnected) {
        wasConnected = false;
        Serial.println("\n✗ WiFi Disconnected. Auto-reconnecting...");
    }
}
