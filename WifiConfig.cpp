#include "WifiConfig.h"
#include "portal_html.h"

WifiConfig::WifiConfig()
    : _server(80), _portalActive(false), _maxNetworks(3),
      _connectTimeout(10000), _switchPending(false),
      _priority(CONNECT_PRIORITY_LAST_SAVED) {}

void WifiConfig::setMaxSavedNetworks(int maxNetworks) {
  if (maxNetworks > 0) {
    _maxNetworks = maxNetworks;
  }
}

void WifiConfig::setConnectTimeout(unsigned long ms) { _connectTimeout = ms; }

void WifiConfig::setStatusCallback(StatusCallback cb) { _statusCallback = cb; }

void WifiConfig::setAutoReconnect(bool enable) { _autoReconnect = enable; }

void WifiConfig::setReconnectInterval(unsigned long ms) {
  _reconnectInterval = ms;
}

void WifiConfig::setAutoFallbackToAP(bool enable) { _autoFallbackAP = enable; }

void WifiConfig::setConnectPriority(ConnectPriority priority) {
  _priority = priority;
}

void WifiConfig::prioritizeLastSaved() {
  setConnectPriority(CONNECT_PRIORITY_LAST_SAVED);
}

void WifiConfig::prioritizeLastConnected() {
  setConnectPriority(CONNECT_PRIORITY_LAST_CONNECTED);
}

void WifiConfig::prioritizeStrongestSignal() {
  setConnectPriority(CONNECT_PRIORITY_STRONGEST);
}

void WifiConfig::begin(const char *apSSID, const char *apPass,
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

bool WifiConfig::_tryConnectSaved() {
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
bool WifiConfig::tryConnectSaved() { return _tryConnectSaved(); }

void WifiConfig::deleteCredential(int index) { _deleteCredential(index); }

void WifiConfig::startPortal() {
  if (!_portalActive) {
    WiFi.disconnect();
    _startAP();
  }
}

void WifiConfig::resetSettings() {
  _prefs.begin("wificfg", false);
  _prefs.clear();
  _prefs.end();
}

void WifiConfig::_startAP() {
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

void WifiConfig::_stopAP() {
  _server.stop();
  _dnsServer.stop();
  WiFi.softAPdisconnect(true);
  _portalActive = false;
  _connectState = STATE_IDLE; // Reset state
}

void WifiConfig::run() {
  // 1. Handle Portal
  if (_portalActive) {
    _dnsServer.processNextRequest();
    _server.handleClient();
  }

  // 2. Handle Connection State Machine
  // 2. Handle Connection State Machine
  if (_connectState == STATE_IDLE) {
    // Check if we need to auto-reconnect
    if (_autoReconnect && !isConnected() && _lastConnectedSSID.length() > 0 &&
        ((!_portalActive) || (_portalActive && !_autoFallbackAP)) &&
        (millis() - _lastReconnectAttempt > _reconnectInterval)) {
      _lastReconnectAttempt = millis();
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
    if (now - _connectStartTime > _connectTimeout) {
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
    if (now - _connectStartTime > _connectTimeout) {
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
    // Check if we need to start a new connection attempt
    if (now - _connectStartTime > _connectTimeout) {
      // Timeout or first run
      if (_connectIndex < getSavedNetworkCount()) {
        // Try next network
        String ssid = getSavedSSID(_connectIndex);

        // SKIP if already tried in LAST_CONNECTED or STRONGEST state
        if ((_priority == CONNECT_PRIORITY_LAST_CONNECTED &&
             ssid.equals(_lastConnectedSSID)) ||
            (_priority == CONNECT_PRIORITY_STRONGEST &&
             _bestNetworkIndex == _connectIndex)) {
          _connectIndex++;
          _connectStartTime = 0; // Immediately check next
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

bool WifiConfig::isConnected() { return WiFi.status() == WL_CONNECTED; }

bool WifiConfig::isPortalActive() { return _portalActive; }

String WifiConfig::getConnectedSSID() {
  if (isConnected()) {
    return WiFi.SSID();
  }
  return "";
}

int WifiConfig::getSavedNetworkCount() {
  int count = 0;
  _prefs.begin("wificfg", false); // Ensure opened
  for (int i = 0; i < _maxNetworks; i++) {
    String key = "ssid" + String(i);
    if (_prefs.isKey(key.c_str())) {
      count++;
    }
  }
  _prefs.end();
  return count;
}

String WifiConfig::getSavedSSID(int index) {
  _prefs.begin("wificfg", false);
  String ssid = "";
  if (index >= 0 && index < _maxNetworks) {
    String key = "ssid" + String(index);
    ssid = _prefs.getString(key.c_str(), "");
  }
  _prefs.end();
  return ssid;
}

String WifiConfig::getSavedPassword(int index) {
  _prefs.begin("wificfg", true); // Read-only is fine/better
  String pass = "";
  if (index >= 0 && index < _maxNetworks) {
    String key = "pass" + String(index);
    pass = _prefs.getString(key.c_str(), "");
  }
  _prefs.end();
  return pass;
}

void WifiConfig::_deleteCredential(int index) {
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

void WifiConfig::_saveCredential(String newSSID, String newPass) {
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

void WifiConfig::_handleRoot() {
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

void WifiConfig::_handleScanResults() {
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

void WifiConfig::_handleConnect() {
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
    html += "<p>Connecting to <strong>" + newSSID + "</strong>...</p>";
    html += "<p style='color:#666;font-size:13px;margin-top:20px'>Please check "
            "your device status.</p>";
    html += "</div></body></html>";

    _server.send(200, "text/html", html);

    // Schedule switch
    _switchPending = true;
    _switchTime = millis();

  } else {
    _sendError("Missing credentials");
  }
}

void WifiConfig::_sendError(String msg) {
  _server.send(400, "text/plain", "Error: " + msg);
}
