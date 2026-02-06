# WifiConfig Library

A robust, modern WiFi configuration manager for ESP32. It features a modern mobile-friendly UI, background scanning for instant page loads, and continuous reconnection logic.

## 🚀 Key Features

*   **Instant Portal**: The configuration page loads immediately (no waiting for scans).
*   **Background Scanning**: Networks are scanned asynchronously and populate the list via AJAX/JSON.
*   **Modern UI**: Beautiful, clean interface with modern aesthetics (plain text password field).
*   **Continuous Reconnection**: Automatically retries saved networks if connection is lost.
*   **Connection Priorities**: Choose between LIFO (Last Saved), Last Connected, or Strongest Signal.
*   **Blocking Connect API**: Simple `connect()` method instead of manual polling.
*   **Event Callbacks**: Get notified when connection succeeds with `onConnected()`.
*   **Configurable Retries**: Control max retries and delay between attempts.
*   **Offline Support**: Can initialize without blocking the main loop or forcing an AP.

## 📦 Installation

Add the repository URL to your `platformio.ini` dependencies:

```ini
[env:esp32]
platform = espressif32
board = esp32doit-devkit-v1
framework = arduino
lib_deps =
    https://github.com/nainaiurk/WifiConfig.git
```

## ⚡ Quick Start

Here is a complete example of how to use the library in your `main.cpp`. 

This setup will:
1.  Try to connect silently on boot (without blocking).
2.  If connection fails, it keeps trying in the background (no AP).
3.  If you press the button, it forces the Portal (AP Mode) to open.

```cpp
#include <Arduino.h>
#include <WifiConfig.h>

WifiConfig wifiConfig;
#define BUTTON_PIN 12  // Button to force Config Portal

void setup() {
    Serial.begin(115200);
    pinMode(BUTTON_PIN, INPUT_PULLDOWN);

    // Optional: Configure settings before begin
    wifiConfig.setAutoFallbackToAP(false); // Don't start AP automatically on boot failure
    wifiConfig.setConnectTimeout(5000);    // Wait 5s per network attempt
    
    // Optional: Set Connection Priority (v1.1.0+)
    // wifiConfig.prioritizeLastConnected(); // Try the last successfully connected network first
    wifiConfig.prioritizeStrongestSignal(); // Scan and connect to the strongest signal found

    // Initialize the library
    // Arguments: "AP Name", "AP Password" (NULL for open), AutoConnect (true/false)
    wifiConfig.begin("Smart Device", NULL, false); 
}

void loop() {
    // 1. Essential: Handle network tasks
    wifiConfig.run();

    // 2. Manual Override: Start Portal on button press
    if (digitalRead(BUTTON_PIN) == HIGH) {
        Serial.println("Starting Portal...");
        wifiConfig.startPortal(); 
        // Portal is now active at 192.168.4.1 (or connected IP)
        // Access Point Name: "Smart Device"
    }
    
    // 3. Check Status
    if (wifiConfig.isConnected()) {
        // You are connected!
        // Serial.println(WiFi.localIP());
    }
}
```

### Simplified Example with Blocking Connect (v1.2.6+)

If you want a simpler, synchronous approach:

```cpp
#include <Arduino.h>
#include <WifiConfig.h>

WifiConfig wifiConfig;

void setup() {
    Serial.begin(115200);
    wifiConfig.begin("Smart Device", NULL, false);
    
    // Set callback for when connected
    wifiConfig.onConnected([](String ssid) {
        Serial.println("Connected to: " + ssid);
        Serial.println("IP: " + WiFi.localIP().toString());
    });
    
    // Wait for connection (with 40 second timeout)
    if (wifiConfig.connect()) {
        Serial.println("WiFi Connected!");
    } else {
        Serial.println("Connection failed, starting portal...");
        wifiConfig.startPortal();
    }
}

void loop() {
    // Just call run() in the loop
    wifiConfig.run();
}
```

## 📖 API Reference

### Initialization

#### `void begin(const char *apSSID, const char *apPass, bool autoConnect)`
Initializes the library.
*   `apSSID`: The name of the Access Point (when portal is active).
*   `apPass`: The password for the AP (pass `NULL` for open network).
*   `autoConnect`: 
    *   `true` (Default): Tries to connect to saved networks immediately. If it fails (and `CurrentFallback` is true), it starts the AP.
    *   `false`: Initializes memory but does **nothing** else. Useful if you want to control when to connect manually or use your own retry loop.

### Configuration

#### `void setAutoFallbackToAP(bool enable)`
*   `true` (Default): If `tryConnectSaved()` fails, the Access Point automatically starts.
*   `false`: If connection fails, it stays disconnected (silent mode). Good for devices that function offline.

#### `void setConnectTimeout(unsigned long ms)`
Sets how long to wait for a WiFi connection per attempt. Default is 10000ms (10s).

#### `void setMaxSavedNetworks(int max)`
Sets the maximum number of networks to remember. Default is 3.

### Connection Priorities (v1.1.0+)

#### `void prioritizeLastSaved()`
**Default Behavior**. The library tries to connect to networks in the reverse order they were added (LIFO). 

#### `void prioritizeLastConnected()`
Tries to connect to the **last successfully connected** network first.
*   **Why use this?** If you have a Mobile Hotspot and Home WiFi saved, and you were last using the Hotspot, it will reconnect to the Hotspot instantly instead of trying Home WiFi first.

#### `void prioritizeStrongestSignal()`
Performs a background scan to find all available networks, then connects to the saved network with the **strongest RSSI (Signal Strength)**.
*   **Why use this?** Best for moving devices or environments with multiple saved networks available simultaneously.
*   Note: Adds ~2-3 seconds to startup time due to scanning.

### Operation

#### `void run()`
**CRITICAL**: You must call this in your `loop()`. It handles:
*   Web Server requests (serving the portal).
*   DNS requests (captive portal redirection).
*   Timeouts and state transitions.

#### `void startPortal()`
Manually forces the Configuration Portal (AP Mode) to start.
*   Disconnects current WiFi.
*   Starts the AP with the name configured in `begin()`.
*   Users can connect to `192.168.4.1` to configure.

#### `bool connect(unsigned long timeout = 40000)` (v1.2.6+)
**Simpler Alternative to `run()` Loop**
Blocking method that handles the entire connection process internally.

```cpp
// OLD WAY (still works):
wifiConfig.tryConnectSaved();
while (!wifiConfig.isConnected() && timeout) {
    wifiConfig.run();
    delay(100);
}

// NEW WAY (much simpler):
if (wifiConfig.connect()) {
    Serial.println("Connected!");
} else {
    Serial.println("Connection failed");
}
```

*   Handles `tryConnectSaved()` + `run()` loop internally.
*   Returns `true` if connected within timeout, `false` otherwise.
*   Default timeout: 40 seconds (customizable).

#### `void onConnected(OnConnectedCallback callback)` (v1.2.7+)
Event callback that fires **once** when connection succeeds.

```cpp
wifiConfig.onConnected([](String ssid) {
    Serial.println("Connected to: " + ssid);
    // Do network stuff here (HTTP requests, etc)
});
```

*   Eliminates the need to poll `isConnected()`.
*   Fires only once per successful connection.
*   Resets when disconnected/reconnected.

#### `void setMaxRetries(int maxRetries)` (v1.2.7+)
Control how many networks to attempt before giving up.

```cpp
wifiConfig.setMaxRetries(5);  // Try up to 5 networks
```

*   Default: 3 (same as max saved networks).
*   Useful for devices with many saved credentials.

#### `void setRetryDelay(unsigned long ms)` (v1.2.7+)
Add delay between retry attempts to prevent aggressive reconnection.

```cpp
wifiConfig.setRetryDelay(200);  // 200ms delay between retries
```

*   Default: 100ms.
*   Good for unstable networks or low-power scenarios.

#### `String getLastConnectedSSID()` (v1.2.6+)
Returns the SSID of the last successful connection (for debugging).

```cpp
String lastSSID = wifiConfig.getLastConnectedSSID();
Serial.println("Will try first: " + lastSSID);
```

#### `ConnectionStatus getStatus()` (v1.2.6+)
Get detailed connection status:

```cpp
WifiConfig::ConnectionStatus status = wifiConfig.getStatus();
// OPTIONS:
// - STATUS_DISCONNECTED
// - STATUS_CONNECTING
// - STATUS_CONNECTED
// - STATUS_TIMEOUT
// - STATUS_WRONG_PASSWORD
// - STATUS_NO_SAVED_NETWORKS
```

#### `String getStatusMessage()` (v1.2.6+)
Get human-readable status message:

```cpp
Serial.println(wifiConfig.getStatusMessage());
// Output examples:
// "Connected to HomeWiFi"
// "Connecting to OfficeWiFi..."
// "No saved networks"
// "Connection timeout"
```

#### `bool tryConnectSaved()`
Cycles through all saved networks and attempts to connect (lower level).
*   Returns `true` if connected successfully.
*   Returns `false` if all attempts failed.
*   You still need to call `run()` in your loop.
*   Note: Use `connect()` instead for simpler API (v1.2.6+).

### Status

#### `bool isConnected()`
Returns `true` if valid WiFi connection exists.

#### `bool isPortalActive()`
Returns `true` if the Access Point / Web Server is currently running.

#### `String getConnectedSSID()`
Returns the SSID of the currently connected network.

#### `int getSavedNetworkCount()`
Returns the number of credentials currently stored in flash memory.

#### `String getSavedSSID(int index)`
Returns the SSID at a specific index (0 to max-1).

## 🧠 How It Works

1.  **Storage**: Credentials are saved permanently in NVS (Non-Volatile Storage).
2.  **Portal**: The HTML is compressed directly in the code.
3.  **Fast Loading**: When a user opens the page, it serves the HTML instantly. The ESP32 then scans for networks in the background and sends the results via JSON ("AJAX"), so the user never sees a loading spinner blocking the page.

## 📋 Version History

### v1.2.7 (Latest)
- ✨ Add `onConnected()` callback for event-driven connection detection
- ✨ Add `setMaxRetries()` and `setRetryDelay()` configuration
- 🐛 Auto-save last connected SSID in `isConnected()` for reliability

### v1.2.6
- ✨ Add `connect()` blocking method for simpler API
- ✨ Add `getLastConnectedSSID()` for debugging
- ✨ Add `getStatus()` and `getStatusMessage()` for detailed connection info
- ✨ Add `ConnectionStatus` enum for better error handling

### v1.2.5
- 🐛 Fix portal interference with connection state machine (early return)
- 🐛 Improve `getSavedNetworkCount()` to handle contiguous entries only

### v1.2.4
- 🐛 Fix credential saving - missing Preferences open/close
- ✨ Remove password masking - show plain text in portal
- ✨ Improve oldest credential replacement logic

### v1.1.0+
- ✨ Add connection priority modes (Last Saved, Last Connected, Strongest Signal)

### v1.0.0
- 🎉 Initial release
