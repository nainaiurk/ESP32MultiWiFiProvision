#include "ESP32MultiWiFiProvision.h"
#include "portal_html.h"

ESP32MultiWiFiProvision::ESP32MultiWiFiProvision()
    : _server(80), _portalActive(false), _maxNetworks(3),
      _connectTimeout(10000), _switchPending(false),
      _maxRetries(3),
      _priority(CONNECT_PRIORITY_LAST_SAVED) {}

void ESP32MultiWiFiProvision::setMaxSavedNetworks(int maxNetworks) {
  if (maxNetworks > 0) {
    _maxNetworks = maxNetworks;
  }
}

void ESP32MultiWiFiProvision::setConnectTimeout(unsigned long ms) { _connectTimeout = ms; }

void ESP32MultiWiFiProvision::setStatusCallback(StatusCallback cb) { _statusCallback = cb; }

void ESP32MultiWiFiProvision::onConnected(OnConnectedCallback cb) { _onConnectedCallback = cb; }

void ESP32MultiWiFiProvision::setAutoReconnect(bool enable) { _autoReconnect = enable; }

void ESP32MultiWiFiProvision::setReconnectInterval(unsigned long ms) {
  _reconnectInterval = ms;
}

void ESP32MultiWiFiProvision::setMaxRetries(int maxRetries) { _maxRetries = maxRetries; }

void ESP32MultiWiFiProvision::setRetryDelay(unsigned long ms) { _retryDelay = ms; }

void ESP32MultiWiFiProvision::setAutoFallbackToAP(bool enable) { _autoFallbackAP = enable; }

void ESP32MultiWiFiProvision::setPortalAutoConnect(bool enable) { _portalAutoConnect = enable; }

void ESP32MultiWiFiProvision::setConnectPriority(ConnectPriority priority) {
  _priority = priority;
}

void ESP32MultiWiFiProvision::prioritizeLastSaved() {
  setConnectPriority(CONNECT_PRIORITY_LAST_SAVED);
}

void ESP32MultiWiFiProvision::prioritizeLastConnected() {
  setConnectPriority(CONNECT_PRIORITY_LAST_CONNECTED);
}

void ESP32MultiWiFiProvision::prioritizeStrongestSignal() {
  setConnectPriority(CONNECT_PRIORITY_STRONGEST);
}

void ESP32MultiWiFiProvision::setLastConnectedSSID(String ssid) {
  if (ssid.length() > 0 && !ssid.equals(_lastConnectedSSID)) {
    _lastConnectedSSID = ssid;
    _prefs.begin("wificfg", false);
    _prefs.putString("last_conn_ssid", _lastConnectedSSID.c_str());
    _prefs.end();
  }
}

void ESP32MultiWiFiProvision::begin(const char *apSSID, const char *apPass,
                       bool autoConnect) {
  _apSSID = String(apSSID);
  if (apPass)
    _apPass = String(apPass);

  _prefs.begin("wificfg", false);
  _lastConnectedSSID = _prefs.getString("last_conn_ssid", "");
  _prefs.end();

  if (autoConnect) {
    // Try to connect to saved networks
    bool connected = tryConnectSaved();

    if (!connected && _autoFallbackAP) {
      _startAP();
    }
  }
}

bool ESP32MultiWiFiProvision::_tryConnectSaved() {
  // Start the state machine
  int count = getSavedNetworkCount();
  if (count > 0) {
    // Check priority mode
    if (_priority == CONNECT_PRIORITY_LAST_CONNECTED &&
        _lastConnectedSSID.length() > 0) {
      // Verify if last connected is still in saved list
      bool found = false;
      for (int i = 0; i < count; i++) {
        if (getSavedSSID(i).equals(_lastConnectedSSID)) {
          found = true;
          break;
        }
      }
      if (found) {
        _connectState = STATE_TRYING_LAST_CONNECTED;
        _connectStartTime = 0;
        return true;
      }
    } else if (_priority == CONNECT_PRIORITY_STRONGEST) {
      if (_statusCallback)
        _statusCallback("Scanning for strongest signal...");
      WiFi.scanNetworks(true); // Async scan
      _connectState = STATE_SCANNING;
      _connectStartTime = millis(); // timeout safety
      return true;
    }

    // Default or Fallback
    _connectState = STATE_TRYING_SAVED;
    _connectIndex = 0;
    _connectStartTime = 0; // Will trigger immediate attempt in run()
    return true;           // Started
  }
  return false;
}

// Public wrapper for backward compatibility or manual trigger
bool ESP32MultiWiFiProvision::tryConnectSaved() { return _tryConnectSaved(); }

void ESP32MultiWiFiProvision::deleteCredential(int index) { _deleteCredential(index); }

void ESP32MultiWiFiProvision::startPortal() {
  if (!_portalActive) {
    WiFi.disconnect();
    _connectedNotified = false;  // Reset callback flag
    _startAP();
  }
}

void ESP32MultiWiFiProvision::resetSettings() {
  _prefs.begin("wificfg", false);
  _prefs.clear();
  _prefs.end();
}

void ESP32MultiWiFiProvision::_startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.disconnect();

  if (_apPass.length() > 0) {
    WiFi.softAP(_apSSID.c_str(), _apPass.c_str());
  } else {
    WiFi.softAP(_apSSID.c_str());
  }

  _dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  _dnsServer.start(53, "*", WiFi.softAPIP());

  _server.on("/", [this]() { _handleRoot(); });
  _server.on("/scan-results", [this]() { _handleScanResults(); });
  _server.on("/connect", [this]() { _handleConnect(); });
  _server.on("/delete", [this]() {
    if (_server.hasArg("index")) {
      int idx = _server.arg("index").toInt();
      _deleteCredential(idx);
    }
    _server.sendHeader("Location", "/");
    _server.send(302, "text/plain", "");
  });
  // Android/Windows Captive Portal checks
  _server.on("/generate_204", [this]() { _handleRoot(); });
  _server.on("/gen_204", [this]() { _handleRoot(); });
  _server.on("/ncsi.txt", [this]() { _handleRoot(); });
  _server.on("/favicon.ico",
             [this]() { _server.send(404); }); // Silence icon errors
  _server.onNotFound([this]() { _handleRoot(); });

  _server.begin();
  _portalActive = true;
}

void ESP32MultiWiFiProvision::_stopAP() {
  _server.stop();
  _dnsServer.stop();
  WiFi.softAPdisconnect(true);
  _portalActive = false;
  _connectState = STATE_IDLE; // Reset state
}

void ESP32MultiWiFiProvision::run() {
  // 1. Handle Portal
  if (_portalActive) {
    _dnsServer.processNextRequest();
    _server.handleClient();
    return;  // Exit early - don't process connection states when portal is active
  }

  // 2. Handle Connection State Machine
  if (_connectState == STATE_IDLE) {
    // Check if we need to auto-reconnect
    if (_autoReconnect && !isConnected() && _lastConnectedSSID.length() > 0 &&
        (millis() - _lastReconnectAttempt > _reconnectInterval)) {
      _lastReconnectAttempt = millis();
      _connectedNotified = false;  // Reset callback flag for reconnection
      if (_statusCallback)
        _statusCallback("Auto-reconnecting...");
      _tryConnectSaved();
    }
  }

  // Handle Scan State (for Strongest Priority)
  if (_connectState == STATE_SCANNING) {
    int n = WiFi.scanComplete();
    if (n >= 0) {
      // Scan done
      // Find best match
      int bestIndex = -1;
      int bestRSSI = -1000;

      for (int i = 0; i < n; i++) {
        String scanSSID = WiFi.SSID(i);
        // Check if this scanned SSID is in our saved list
        int count = getSavedNetworkCount();
        for (int j = 0; j < count; j++) {
          if (getSavedSSID(j).equals(scanSSID)) {
            // Match found! Check RSSI
            if (WiFi.RSSI(i) > bestRSSI) {
              bestRSSI = WiFi.RSSI(i);
              bestIndex = j;
            }
            break; // Stop checking saved list for this scan result
          }
        }
      }

      WiFi.scanDelete(); // Clean up

      if (bestIndex >= 0) {
        _connectState = STATE_TRYING_STRONGEST;
        _bestNetworkIndex = bestIndex;
        _connectStartTime = 0;
        if (_statusCallback) {
          String msg = "Strongest: " + getSavedSSID(bestIndex) + " (" +
                       String(bestRSSI) + "dBm)";
          _statusCallback(msg.c_str());
        }
      } else {
        // No match found, fallback to normal saved order
        _connectState = STATE_TRYING_SAVED;
        _connectIndex = 0;
        _connectStartTime = 0;
      }

    } else if (n == -1) {
      // Still scanning, check timeout
      if (millis() - _connectStartTime > 10000) {
        // Scan stuck? Fallback
        _connectState = STATE_TRYING_SAVED;
        _connectIndex = 0;
        _connectStartTime = 0;
      }
    }
  } else if (_connectState == STATE_TRYING_STRONGEST) {
    if (WiFi.status() == WL_CONNECTED) {
      _connectState = STATE_IDLE;
      // Save valid connection (optional, acts as Last Connected too)
      String currentSSID = WiFi.SSID();
      if (!currentSSID.equals(_lastConnectedSSID)) {
        _lastConnectedSSID = currentSSID;
        _prefs.begin("wificfg", false);
        _prefs.putString("last_conn_ssid", _lastConnectedSSID.c_str());
        _prefs.end();
      }
      if (_statusCallback)
        _statusCallback("Connected!");
      return;
    }

    unsigned long now = millis();
    if (_connectStartTime == 0 || now - _connectStartTime >= _connectTimeout) {
      if (_connectStartTime == 0) { // First run for this state
        String ssid = getSavedSSID(_bestNetworkIndex);
        String pass = getSavedPassword(_bestNetworkIndex);
        if (_statusCallback) {
          String msg = "Connecting to (Strongest): " + ssid + "...";
          _statusCallback(msg.c_str());
        }
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), pass.c_str());
        _connectStartTime = now;
      } else {
        // Timeout
        _connectState = STATE_TRYING_SAVED;
        _connectIndex = 0;
        _connectStartTime = 0;
      }
    }
  } else if (_connectState == STATE_TRYING_LAST_CONNECTED) {
    if (WiFi.status() == WL_CONNECTED) {
      _connectState = STATE_IDLE;
      // Save valid connection
      String currentSSID = WiFi.SSID();
      if (!currentSSID.equals(_lastConnectedSSID)) {
        _lastConnectedSSID = currentSSID;
        _prefs.begin("wificfg", false);
        _prefs.putString("last_conn_ssid", _lastConnectedSSID.c_str());
        _prefs.end();
      }
      if (_statusCallback)
        _statusCallback("Connected!");
      return;
    }

    unsigned long now = millis();
    if (_connectStartTime == 0 || now - _connectStartTime >= _connectTimeout) {
      if (_connectStartTime == 0) { // First run for this state
        // Find password for _lastConnectedSSID
        String pass = "";
        int count = getSavedNetworkCount();
        for (int i = 0; i < count; i++) {
          if (getSavedSSID(i).equals(_lastConnectedSSID)) {
            pass = getSavedPassword(i);
            break;
          }
        }
        if (_statusCallback) {
          String msg = "Connecting to (Last): " + _lastConnectedSSID + "...";
          _statusCallback(msg.c_str());
        }
        WiFi.mode(WIFI_STA);
        WiFi.begin(_lastConnectedSSID.c_str(), pass.c_str());
        _connectStartTime = now;
      } else {
        // Timeout -> Fallback to normal saved list
        _connectState = STATE_TRYING_SAVED;
        _connectIndex = 0;
        _connectStartTime = 0;
      }
    }
  } else if (_connectState == STATE_TRYING_SAVED) {
    if (WiFi.status() == WL_CONNECTED) {
      _connectState = STATE_IDLE;
      // Save valid connection
      String currentSSID = WiFi.SSID();
      if (!currentSSID.equals(_lastConnectedSSID)) {
        _lastConnectedSSID = currentSSID;
        _prefs.begin("wificfg", false);
        _prefs.putString("last_conn_ssid", _lastConnectedSSID.c_str());
        _prefs.end();
      }
      if (_statusCallback)
        _statusCallback("Connected!");
      return;
    }

    unsigned long now = millis();
    // Check if we need to start a new connection attempt
    if (_connectStartTime == 0 || now - _connectStartTime >= _connectTimeout) {
      // Timeout or first run
      if (_connectIndex < getSavedNetworkCount() && _connectIndex < _maxRetries) {
        // Try next network
        String ssid = getSavedSSID(_connectIndex);

        // SKIP if already tried in LAST_CONNECTED or STRONGEST state
        if ((_priority == CONNECT_PRIORITY_LAST_CONNECTED &&
             ssid.equals(_lastConnectedSSID)) ||
            (_priority == CONNECT_PRIORITY_STRONGEST &&
             _bestNetworkIndex == _connectIndex)) {
          _connectIndex++;
          _connectStartTime = 0;  // Trigger immediate retry for the next network
          return;
        }

        String pass = getSavedPassword(_connectIndex);

        if (ssid.length() > 0) {
          if (_statusCallback) {
            String msg = "Connecting to: " + ssid + "...";
            _statusCallback(msg.c_str());
          }
          WiFi.mode(WIFI_STA);
          WiFi.begin(ssid.c_str(), pass.c_str());
          _connectStartTime = now;
        }
        _connectIndex++;
      } else {
        // All Failed (Index reached limit)
        _connectState = STATE_IDLE;
        if (_autoFallbackAP) {
          _startAP();
          _connectState = STATE_PORTAL;
        }
      }
    }
  }
  // Handle deferred switch
  if (_switchPending && millis() - _switchTime > 2000) {
    _switchPending = false;
    _stopAP();
    _tryConnectSaved();
  }
}

bool ESP32MultiWiFiProvision::isConnected() {
  bool connected = (WiFi.status() == WL_CONNECTED);
  
  // Auto-save last connected SSID when connection is first detected
  if (connected && _connectState != STATE_IDLE) {
    String currentSSID = WiFi.SSID();
    if (!currentSSID.equals(_lastConnectedSSID)) {
      _lastConnectedSSID = currentSSID;
      _prefs.begin("wificfg", false);
      _prefs.putString("last_conn_ssid", _lastConnectedSSID.c_str());
      _prefs.end();
    }
    _connectState = STATE_IDLE;
    _connectedNotified = false;  // Reset for next disconnection
  }
  
  // Trigger onConnected callback (once per connection)
  if (connected && !_connectedNotified && _onConnectedCallback) {
    _connectedNotified = true;
    _onConnectedCallback(WiFi.SSID());
  }
  
  return connected;
}

bool ESP32MultiWiFiProvision::isPortalActive() { return _portalActive; }

String ESP32MultiWiFiProvision::getConnectedSSID() {
  if (isConnected()) {
    return WiFi.SSID();
  }
  return "";
}

int ESP32MultiWiFiProvision::getSavedNetworkCount() {
  int count = 0;
  _prefs.begin("wificfg", true); // Read-only mode
  // Count contiguous networks from index 0
  for (int i = 0; i < _maxNetworks; i++) {
    String key = "ssid" + String(i);
    if (_prefs.isKey(key.c_str())) {
      count++;
    } else {
      // Stop at first gap (networks should be contiguous)
      break;
    }
  }
  _prefs.end();
  return count;
}

String ESP32MultiWiFiProvision::getSavedSSID(int index) {
  _prefs.begin("wificfg", false);
  String ssid = "";
  if (index >= 0 && index < _maxNetworks) {
    String key = "ssid" + String(index);
    ssid = _prefs.getString(key.c_str(), "");
  }
  _prefs.end();
  return ssid;
}

String ESP32MultiWiFiProvision::getSavedPassword(int index) {
  _prefs.begin("wificfg", true); // Read-only is fine/better
  String pass = "";
  if (index >= 0 && index < _maxNetworks) {
    String key = "pass" + String(index);
    pass = _prefs.getString(key.c_str(), "");
  }
  _prefs.end();
  return pass;
}

void ESP32MultiWiFiProvision::_deleteCredential(int index) {
  _prefs.begin("wificfg", false);

  // 1. Calculate actual count of saved networks (contiguous 0..N)
  int count = 0;
  while (count < _maxNetworks) {
    String k = "ssid" + String(count);
    if (!_prefs.isKey(k.c_str()))
      break;
    count++;
  }

  Serial.print("[Delete] Request to delete index: ");
  Serial.println(index);
  Serial.print("[Delete] Current saved count: ");
  Serial.println(count);

  if (index < 0 || index >= count) {
    Serial.println("[Delete] Invalid index!");
    _prefs.end();
    return;
  }

  // 2. Shift everything down: Move (i+1) -> i
  for (int i = index; i < count - 1; i++) {
    String nextS = _prefs.getString(("ssid" + String(i + 1)).c_str(), "");
    String nextP = _prefs.getString(("pass" + String(i + 1)).c_str(), "");

    _prefs.putString(("ssid" + String(i)).c_str(), nextS);
    _prefs.putString(("pass" + String(i)).c_str(), nextP);
  }

  // 3. Delete the last tail item (which is now duplicated at count-2)
  String lastKeyS = "ssid" + String(count - 1);
  String lastKeyP = "pass" + String(count - 1);

  _prefs.remove(lastKeyS.c_str());
  _prefs.remove(lastKeyP.c_str());

  Serial.println("[Delete] Deletion complete.");
  _prefs.end();
}

void ESP32MultiWiFiProvision::_saveCredential(String newSSID, String newPass) {
  _prefs.begin("wificfg", false); // Open preferences for read/write
  
  struct Cred {
    String s;
    String p;
  };
  Cred *creds = new Cred[_maxNetworks];
  int count = 0;

  // Read existing credentials (excluding duplicate of new SSID)
  for (int i = 0; i < _maxNetworks; i++) {
    String kS = "ssid" + String(i);
    String kP = "pass" + String(i);
    if (_prefs.isKey(kS.c_str())) {
      String s = _prefs.getString(kS.c_str(), "");
      String p = _prefs.getString(kP.c_str(), "");
      if (s.length() > 0 && !s.equals(newSSID)) {
        creds[count].s = s;
        creds[count].p = p;
        count++;
      }
    }
  }

  // If we've reached max capacity, remove the oldest (last) credential
  if (count >= _maxNetworks) {
    Serial.println("[Save] Max networks reached. Replacing oldest credential.");
    count = _maxNetworks - 1; // Keep only the most recent (maxNetworks - 1) credentials
  }

  // Read existing last connected SSID before clearing
  String savedLastConn = _prefs.getString("last_conn_ssid", "");

  _prefs.clear();

  // Restore last connected SSID (if valid)
  if (savedLastConn.length() > 0) {
    _prefs.putString("last_conn_ssid", savedLastConn.c_str());
  }

  // Write new credential at index 0 (most recent)
  _prefs.putString("ssid0", newSSID);
  _prefs.putString("pass0", newPass);
  Serial.print("[Save] Saved new credential at index 0: ");
  Serial.println(newSSID);

  // Write old credentials starting from index 1
  int savedCount = 1;
  for (int i = 0; i < count; i++) {
    String kS = "ssid" + String(savedCount);
    String kP = "pass" + String(savedCount);
    _prefs.putString(kS.c_str(), creds[i].s);
    _prefs.putString(kP.c_str(), creds[i].p);
    savedCount++;
  }

  Serial.print("[Save] Total networks saved: ");
  Serial.println(savedCount);

  _prefs.end(); // Close preferences
  delete[] creds;
}

void ESP32MultiWiFiProvision::_handleRoot() {
  _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  _server.sendHeader("Pragma", "no-cache");
  _server.sendHeader("Expires", "-1");

  String html = FPSTR(PORTAL_HTML);
  html.replace("%CSS%", FPSTR(PORTAL_CSS));
  html.replace("%AP_NAME%", _apSSID);

  // Generate Saved Networks HTML
  String savedHtml = "";
  int count = getSavedNetworkCount();
  if (count > 0) {
    savedHtml += "<div class='saved-list'>";
    savedHtml += "<div class='saved-title'>💾 SAVED NETWORKS (" +
                 String(count) + "/" + String(_maxNetworks) + ")</div>";

    for (int i = 0; i < count; i++) {
      String s = getSavedSSID(i);
      String sEscaped = s;
      sEscaped.replace("\"", "&quot;");
      sEscaped.replace("'", "\\'");

      savedHtml += "<div class='saved-row'>";
      savedHtml += "<button class='saved-btn' onclick='s(\"" + sEscaped +
                   "\")'>" + s + "</button>";
      savedHtml += "<button class='delete-btn' "
                   "onclick='window.location.href=\"/delete?index=" +
                   String(i) + "\"'>🗑</button>";
      savedHtml += "</div>";
    }
    savedHtml += "</div>";
  }
  html.replace("%SAVED_LIST%", savedHtml);

  _server.send(200, "text/html", html);
}

void ESP32MultiWiFiProvision::_handleScanResults() {
  int n = WiFi.scanNetworks();

  // JSON construction
  String json = "[";
  for (int i = 0; i < n; ++i) {
    if (i)
      json += ",";
    json += "{";
    json += "\"s\":\"" + WiFi.SSID(i) + "\",";
    json += "\"r\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"e\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    json += "}";
  }
  json += "]";

  _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  _server.sendHeader("Pragma", "no-cache");
  _server.sendHeader("Expires", "-1");
  _server.send(200, "application/json", json);
}

void ESP32MultiWiFiProvision::_handleConnect() {
  if (_server.hasArg("ssid") && _server.hasArg("pass")) {
    String newSSID = _server.arg("ssid");
    String newPass = _server.arg("pass");

    _saveCredential(newSSID, newPass);

    String html =
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Saved</title>";
    html += FPSTR(PORTAL_CSS);
    html += "</head><body><div class='container' style='text-align:center'>";
    html += "<div style='font-size:64px;margin-bottom:20px'>✓</div>";
    html += "<h1>Saved!</h1>";
    
    if (_portalAutoConnect) {
      html += "<p>Connecting to <strong>" + newSSID + "</strong>...</p>";
      html += "<p style='color:#666;font-size:13px;margin-top:20px'>Please check "
              "your device status.</p>";
    } else {
      html += "<p>Credentials saved. You can close this window.</p>";
      html += "<p style='color:#666;font-size:13px;margin-top:20px'>Connect manually when ready.</p>";
    }
    
    html += "</div></body></html>";

    _server.send(200, "text/html", html);

    // Schedule switch only if auto-connect is enabled
    if (_portalAutoConnect) {
      _switchPending = true;
      _switchTime = millis();
    } else {
      _stopAP();  // Close portal immediately
    }

  } else {
    _sendError("Missing credentials");
  }
}

void ESP32MultiWiFiProvision::_sendError(String msg) {
  _server.send(400, "text/plain", "Error: " + msg);
}

// Blocking connection method - simplified API
bool ESP32MultiWiFiProvision::connect(unsigned long timeout) {
  if (isConnected()) return true;

  tryConnectSaved();

  unsigned long start = millis();
  while (!isConnected() && millis() - start < timeout) {
    run();
    delay(100);
  }

  return isConnected();
}

// Get last connected SSID
String ESP32MultiWiFiProvision::getLastConnectedSSID() {
  return _lastConnectedSSID;
}

// Get detailed connection status
ConnectionStatus ESP32MultiWiFiProvision::getStatus() {
  if (isConnected()) {
    _currentStatus = STATUS_CONNECTED;
  } else if (_connectState == STATE_IDLE && getSavedNetworkCount() == 0) {
    _currentStatus = STATUS_NO_SAVED_NETWORKS;
  } else if (_connectState == STATE_IDLE) {
    _currentStatus = STATUS_DISCONNECTED;
  } else {
    _currentStatus = STATUS_CONNECTING;
  }

  return _currentStatus;
}

// Get human-readable status message
String ESP32MultiWiFiProvision::getStatusMessage() {
  switch (getStatus()) {
    case STATUS_CONNECTED:
      return "Connected to " + getConnectedSSID();
    case STATUS_CONNECTING:
      return "Connecting...";
    case STATUS_DISCONNECTED:
      return "Disconnected";
    case STATUS_TIMEOUT:
      return "Connection timeout";
    case STATUS_WRONG_PASSWORD:
      return "Wrong password";
    case STATUS_NO_SAVED_NETWORKS:
      return "No saved networks";
    default:
      return "Unknown status";
  }
}
