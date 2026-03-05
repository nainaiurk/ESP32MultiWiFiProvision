# ESP32MultiWiFiProvision

A robust, non-blocking WiFi configuration library for ESP32. Provides a beautiful captive portal, multi-network credential storage, automatic reconnection, and flexible connection priority modes — all with a simple API.

---

## ✨ Features

| Feature | Description |
|---|---|
| 📡 **Captive Portal** | Beautiful web UI served instantly; networks load in background via AJAX |
| 💾 **Multi-Network Storage** | Saves up to N credentials in NVS (Non-Volatile Storage) |
| 🔄 **Auto Reconnection** | Automatically retries saved networks if connection drops |
| 🎯 **Connection Priorities** | Last Saved (LIFO), Last Connected, or Strongest Signal |
| ⏱️ **Blocking & Non-Blocking** | Choose `connect()` (blocking) or `run()` loop (non-blocking) |
| 📢 **Event Callbacks** | `onConnected()` fires once when WiFi connects — no polling needed |
| ⚙️ **Highly Configurable** | Timeouts, retries, retry delays, auto-fallback, and more |
| 🔌 **Offline-Friendly** | Can initialize without blocking or forcing AP mode |

---

## 📦 Installation

### PlatformIO

Add to your `platformio.ini`:

```ini
lib_deps =
    https://github.com/nainaiurk/ESP32MultiWiFiProvision.git
```

### Arduino IDE

1. Download this repository as a ZIP (Code → Download ZIP).
2. In Arduino IDE: **Sketch → Include Library → Add .ZIP Library…** → select the ZIP.
3. Select your ESP32 board under **Tools → Board**.

---

## 🚀 Quick Start

### Non-Blocking (Recommended)

```cpp
#include <Arduino.h>
#include <ESP32MultiWiFiProvision.h>

ESP32MultiWiFiProvision wifiConfig;
#define BUTTON_PIN 12

void setup() {
    Serial.begin(115200);
    pinMode(BUTTON_PIN, INPUT_PULLDOWN);

    wifiConfig.setAutoFallbackToAP(false);
    wifiConfig.setConnectTimeout(5000);
    wifiConfig.prioritizeStrongestSignal();

    wifiConfig.begin("Smart Device", NULL, false);
}

void loop() {
    wifiConfig.run();  // Must be called every loop

    if (digitalRead(BUTTON_PIN) == HIGH) {
        wifiConfig.startPortal();  // Opens AP at 192.168.4.1
    }

    if (wifiConfig.isConnected()) {
        // Do your connected work here
    }
}
```

### Blocking (Simpler)

```cpp
#include <Arduino.h>
#include <ESP32MultiWiFiProvision.h>

ESP32MultiWiFiProvision wifiConfig;

void setup() {
    Serial.begin(115200);
    wifiConfig.begin("Smart Device", NULL, false);

    wifiConfig.onConnected([](String ssid) {
        Serial.println("Connected to: " + ssid);
    });

    if (!wifiConfig.connect()) {       // Blocks for up to 40s
        wifiConfig.startPortal();      // Fallback to portal
    }
}

void loop() {
    wifiConfig.run();
}
```

---

## 📖 API Reference

### Setup & Lifecycle

| Method | Description |
|---|---|
| `begin(apSSID, apPass, autoConnect)` | Initialize the library. `autoConnect=true` connects immediately; `false` waits for manual trigger. |
| `run()` | **Call every `loop()` iteration.** Handles portal, DNS, state machine, and reconnection. |
| `connect(timeout)` | Blocking connect — tries saved networks, returns `true`/`false`. Default timeout: 40s. |
| `startPortal()` | Manually opens the captive portal (AP mode at 192.168.4.1). |
| `resetSettings()` | Erases all saved credentials from NVS. |

---

### Configuration (Call Before `begin()`)

| Method | Default | Description |
|---|---|---|
| `setMaxSavedNetworks(n)` | 3 | Max credentials to store |
| `setConnectTimeout(ms)` | 10000 | Timeout per connection attempt |
| `setAutoFallbackToAP(bool)` | `true` | Start AP automatically if all connections fail |
| `setAutoReconnect(bool)` | `true` | Auto-retry if connection drops |
| `setReconnectInterval(ms)` | 10000 | Delay between reconnect attempts |
| `setMaxRetries(n)` | 3 | How many networks to try before giving up |
| `setRetryDelay(ms)` | 100 | Delay between retry attempts |
| `setPortalAutoConnect(bool)` | `true` | Auto-connect after saving via portal. Set `false` for on-demand WiFi patterns. |

---

### Connection Priority Modes

Choose how the library picks which saved network to try first:

```cpp
wifiConfig.prioritizeLastSaved();       // Default — LIFO order
wifiConfig.prioritizeLastConnected();   // Try last successful network first
wifiConfig.prioritizeStrongestSignal(); // Scan & pick best RSSI (~2-3s added)
```

**`setLastConnectedSSID(ssid)`** *(v1.3.0)* — Manually update the "Last Connected" state when you connect via `WiFi.begin()` in your own code:

```cpp
if (WiFi.status() == WL_CONNECTED) {
    wifiConfig.setLastConnectedSSID(WiFi.SSID());
}
```

---

### Status & Callbacks

| Method | Returns | Description |
|---|---|---|
| `isConnected()` | `bool` | `true` if WiFi is connected |
| `isPortalActive()` | `bool` | `true` if AP/portal is running |
| `getConnectedSSID()` | `String` | Current SSID (empty if disconnected) |
| `getLastConnectedSSID()` | `String` | Last successfully connected SSID |
| `getStatus()` | `ConnectionStatus` | Enum: `STATUS_CONNECTED`, `STATUS_CONNECTING`, `STATUS_DISCONNECTED`, `STATUS_TIMEOUT`, `STATUS_WRONG_PASSWORD`, `STATUS_NO_SAVED_NETWORKS` |
| `getStatusMessage()` | `String` | Human-readable status (e.g., `"Connected to HomeWiFi"`) |

**`onConnected(callback)`** — Event-driven notification (fires once per connection):

```cpp
wifiConfig.onConnected([](String ssid) {
    Serial.println("Connected to: " + ssid);
});
```

---

### Credential Management

| Method | Description |
|---|---|
| `getSavedNetworkCount()` | Number of stored credentials |
| `getSavedSSID(index)` | SSID at index (0 to max-1) |
| `getSavedPassword(index)` | Password at index |
| `deleteCredential(index)` | Remove a specific credential |
| `tryConnectSaved()` | Low-level: cycle through saved networks (prefer `connect()`) |

---

## 🧠 How It Works

1. **Credentials** are stored in ESP32 NVS (persists across reboots).
2. **Portal** HTML is embedded in flash — the page loads instantly.
3. **Network scan** runs asynchronously in the background; results populate via JSON/AJAX, so the UI never blocks.
4. **State machine** in `run()` handles connection attempts, timeouts, fallbacks, and reconnection seamlessly.

---

## 📋 Changelog

### v1.3.0 (Latest)
- 🏷️ **Renamed** library to `ESP32MultiWiFiProvision`
- ✨ Add `setLastConnectedSSID()` for manual "Last Connected" override
- 🐛 Fix credential saving and preferences management bugs
- 🐛 Fix connection state timer bugs causing unintended 10s delays

### v1.2.8
- ✨ Add `setPortalAutoConnect()` for on-demand WiFi patterns
- 💡 Improved portal feedback when auto-connect disabled

### v1.2.7
- ✨ Add `onConnected()` callback
- ✨ Add `setMaxRetries()` and `setRetryDelay()`
- 🐛 Auto-save last connected SSID in `isConnected()`

### v1.2.6
- ✨ Add `connect()` blocking method
- ✨ Add `getLastConnectedSSID()`, `getStatus()`, `getStatusMessage()`
- ✨ Add `ConnectionStatus` enum

### v1.2.5
- 🐛 Fix portal interference with connection state machine
- 🐛 Fix `getSavedNetworkCount()` contiguous entry handling

### v1.2.4
- 🐛 Fix credential saving (missing Preferences open/close)
- ✨ Show plain text passwords in portal
- ✨ Improve oldest credential replacement logic

### v1.1.0
- ✨ Connection priority modes (Last Saved, Last Connected, Strongest Signal)

### v1.0.0
- 🎉 Initial release

---

## 👤 Author

**Nainaiu Rakhaine**

[![Website](https://img.shields.io/badge/Website-nainaiurk.me-blue?style=flat-square)](https://www.nainaiurk.me)
[![LinkedIn](https://img.shields.io/badge/LinkedIn-nainaiu--rakhaine-0A66C2?style=flat-square&logo=linkedin)](https://www.linkedin.com/in/nainaiu-rakhaine)
[![GitHub](https://img.shields.io/badge/GitHub-nainaiurk-181717?style=flat-square&logo=github)](https://github.com/nainaiurk)

---

## 📄 License

MIT — see [LICENSE](LICENSE) for details.
