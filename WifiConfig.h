#ifndef WIFICONFIG_H
#define WIFICONFIG_H

#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

class WifiConfig {
public:
  WifiConfig();

  // Initialize the WiFi Config manager
  // apSSID: Name of the Access Point to create if no connection
  // apPass: Password for the Access Point (NULL for open)
  // autoConnect: If true, tries to connect immediately (default true)
  void begin(const char *apSSID, const char *apPass = NULL,
             bool autoConnect = true);

  // Call this in your loop()
  void run();

  // Configuration Setters (Call before begin)
  void setMaxSavedNetworks(int maxNetworks); // Default: 3
  void
  setConnectTimeout(unsigned long ms); // Time to wait per network connection
                                       // attempt. Default: 10000ms

  // Manually force the portal to start
  void startPortal();

  // Reset saved credentials
  void resetSettings();

  // Check if we are currently connected to the configured WiFi
  bool isConnected();

  // Check if portal is currently active
  bool isPortalActive();

  // Get the name of the currently connected SSID
  String getConnectedSSID();

  // Credential Accessors
  int getSavedNetworkCount();
  String getSavedSSID(int index);
  String getSavedPassword(int index);
  void deleteCredential(int index);

  // Status Callback
  typedef void (*StatusCallback)(const char *msg);
  void setStatusCallback(StatusCallback cb);

  void setAutoFallbackToAP(bool enable);
  bool tryConnectSaved();

private:
  Preferences _prefs;
  WebServer _server;
  DNSServer _dnsServer;
  StatusCallback _statusCallback = NULL;

  String _apSSID;
  String _apPass;
  bool _portalActive;
  bool _autoFallbackAP = true;

  int _maxNetworks;
  unsigned long _connectTimeout;
  bool _switchPending;
  unsigned long _switchTime;

  // Connection State Machine
  enum ConnectState { STATE_IDLE, STATE_TRYING_SAVED, STATE_PORTAL };

  void _saveCredential(String ssid, String pass);
  void _deleteCredential(int index);
  bool _tryConnectSaved();
  void _startAP();
  void _stopAP();
  void _handleRoot();
  void _handleScanResults();
  void _handleConnect();
  void _sendError(String msg);

  // State definitions
  ConnectState _connectState = STATE_IDLE;
  int _connectIndex = 0;
  unsigned long _connectStartTime = 0;
};

#endif
