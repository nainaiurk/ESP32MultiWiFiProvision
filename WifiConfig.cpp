#include "WifiConfig.h"
#include "portal_html.h"

WifiConfig::WifiConfig()
    : _server(80), _portalActive(false), _maxNetworks(3),
      _connectTimeout(10000), _switchPending(false) {}

void WifiConfig::setMaxSavedNetworks(int maxNetworks) {
  if (maxNetworks > 0) {
    _maxNetworks = maxNetworks;
  }
}

void WifiConfig::setConnectTimeout(unsigned long ms) { _connectTimeout = ms; }

void WifiConfig::setStatusCallback(StatusCallback cb) { _statusCallback = cb; }

void WifiConfig::setAutoFallbackToAP(bool enable) { _autoFallbackAP = enable; }

void WifiConfig::begin(const char *apSSID, const char *apPass,
                       bool autoConnect) {
  _apSSID = String(apSSID);
  if (apPass)
    _apPass = String(apPass);

  _prefs.begin("wificfg", false);

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
    _connectState = STATE_TRYING_SAVED;
    _connectIndex = 0;
    _connectStartTime = 0; // Will trigger immediate attempt in run()
    return true;           // Started
  }
  return false;
}

// Public wrapper for backward compatibility or manual trigger
bool WifiConfig::tryConnectSaved() { return _tryConnectSaved(); }

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
  if (_connectState == STATE_TRYING_SAVED) {
    if (WiFi.status() == WL_CONNECTED) {
      _connectState = STATE_IDLE;
      if (_statusCallback)
        _statusCallback("Connected!");
      return;
    }

    unsigned long now = millis();
    // Check if we need to start a new connection attempt
    if (now - _connectStartTime > _connectTimeout) {
      // Timeout or first run
      if (_connectIndex < getSavedNetworkCount()) {
        // Try next network
        String ssid = getSavedSSID(_connectIndex);
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
        // All Failed
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
  return count;
}

String WifiConfig::getSavedSSID(int index) {
  if (index >= 0 && index < _maxNetworks) {
    String key = "ssid" + String(index);
    return _prefs.getString(key.c_str(), "");
  }
  return "";
}

String WifiConfig::getSavedPassword(int index) {
  if (index >= 0 && index < _maxNetworks) {
    String key = "pass" + String(index);
    return _prefs.getString(key.c_str(), "");
  }
  return "";
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
  struct Cred {
    String s;
    String p;
  };
  Cred *creds = new Cred[_maxNetworks];
  int count = 0;

  // Read existing
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

  _prefs.clear();

  // Write new
  _prefs.putString("ssid0", newSSID);
  _prefs.putString("pass0", newPass);

  // Write old
  int savedCount = 1;
  for (int i = 0; i < count && savedCount < _maxNetworks; i++) {
    String kS = "ssid" + String(savedCount);
    String kP = "pass" + String(savedCount);
    _prefs.putString(kS.c_str(), creds[i].s);
    _prefs.putString(kP.c_str(), creds[i].p);
    savedCount++;
  }

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
