/*
 * ESP32MultiWiFiProvision - Multi Network Example
 *
 * Demonstrates how to manage multiple saved WiFi networks.
 * The library can store several credentials and connect to the best one.
 *
 * This example shows:
 *   - Storing up to 5 networks
 *   - Listing all saved credentials
 *   - Deleting a specific credential
 *   - Connection priority modes (Last Connected vs Strongest Signal)
 *   - Factory reset (erase all saved networks)
 *
 * Wiring:
 *   GPIO 12 — Portal button (INPUT_PULLDOWN)
 *   GPIO 14 — Factory reset button (hold 3 seconds, INPUT_PULLDOWN)
 */

#include <ESP32MultiWiFiProvision.h>

ESP32MultiWiFiProvision wifiConfig;

#define PORTAL_BUTTON  12
#define RESET_BUTTON   14

// Helper: print all saved networks to Serial
void printSavedNetworks() {
    int count = wifiConfig.getSavedNetworkCount();
    Serial.println("\n╔══════════════════════════╗");
    Serial.println("║    Saved Networks        ║");
    Serial.println("╠══════════════════════════╣");
    if (count == 0) {
        Serial.println("║  (none)                  ║");
    } else {
        for (int i = 0; i < count; i++) {
            Serial.print("║  [");
            Serial.print(i);
            Serial.print("] ");
            String ssid = wifiConfig.getSavedSSID(i);
            Serial.print(ssid);
            // Pad for alignment
            for (int j = ssid.length(); j < 20; j++) Serial.print(" ");
            Serial.println("║");
        }
    }
    Serial.println("╚══════════════════════════╝");
    Serial.print("Total: ");
    Serial.print(count);
    Serial.println(" / 5");
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== Multi-Network Example ===");

    pinMode(PORTAL_BUTTON, INPUT_PULLDOWN);
    pinMode(RESET_BUTTON, INPUT_PULLDOWN);

    // Store up to 5 WiFi networks
    wifiConfig.setMaxSavedNetworks(5);

    // Choose a priority mode:
    //   prioritizeLastSaved()       — tries most recently added first (default)
    //   prioritizeLastConnected()   — tries last successful network first
    //   prioritizeStrongestSignal() — scans and picks best RSSI (adds ~2s)
    wifiConfig.prioritizeLastConnected();

    wifiConfig.setConnectTimeout(8000);   // 8 seconds per attempt
    wifiConfig.setMaxRetries(5);          // Try all 5 stored networks

    // Initialize — auto-connect on boot
    wifiConfig.begin("ESP32-MultiNet", NULL, true);

    // Show what's saved
    printSavedNetworks();

    if (wifiConfig.isConnected()) {
        Serial.print("\n✓ Connected to: ");
        Serial.println(WiFi.SSID());
        Serial.print("  Last connected: ");
        Serial.println(wifiConfig.getLastConnectedSSID());
    } else {
        Serial.println("\nNot connected. Open portal to add networks.");
    }

    Serial.println("\nCommands:");
    Serial.println("  Portal button  — add a new network");
    Serial.println("  Reset button   — hold 3s to erase all networks");
}

void loop() {
    wifiConfig.run();

    // --- Portal Button ---
    if (digitalRead(PORTAL_BUTTON) == HIGH) {
        delay(50);
        if (digitalRead(PORTAL_BUTTON) == HIGH) {
            Serial.println("\nOpening portal — add a new network...");
            wifiConfig.startPortal();
            while (digitalRead(PORTAL_BUTTON) == HIGH) delay(10);
        }
    }

    // --- Factory Reset Button (hold 3 seconds) ---
    if (digitalRead(RESET_BUTTON) == HIGH) {
        Serial.print("Hold reset button for 3 seconds...");
        unsigned long pressStart = millis();
        while (digitalRead(RESET_BUTTON) == HIGH && millis() - pressStart < 3000) {
            delay(10);
        }
        if (millis() - pressStart >= 3000) {
            Serial.println(" RESET!");
            Serial.println("Erasing all saved networks...");
            wifiConfig.resetSettings();
            Serial.println("Done. Restarting...");
            delay(500);
            ESP.restart();
        } else {
            Serial.println(" released too early.");
        }
    }

    // --- Connection monitor ---
    static bool wasConnected = false;
    if (wifiConfig.isConnected() && !wasConnected) {
        wasConnected = true;
        Serial.print("\n✓ Connected to: ");
        Serial.println(WiFi.SSID());
        printSavedNetworks();
    }
    if (!wifiConfig.isConnected() && wasConnected) {
        wasConnected = false;
        Serial.println("\n✗ Disconnected. Will auto-reconnect...");
    }
}
