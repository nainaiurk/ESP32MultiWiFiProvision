#ifndef WIFICONFIG_H
#define WIFICONFIG_H

#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

// Connection status codes
enum ConnectionStatus {
  STATUS_DISCONNECTED,
  STATUS_CONNECTING,
  STATUS_CONNECTED,
  STATUS_TIMEOUT,
  STATUS_WRONG_PASSWORD,
  STATUS_NO_SAVED_NETWORKS
};

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

  // Priority Configuration
  enum ConnectPriority {
    CONNECT_PRIORITY_LAST_SAVED,
    CONNECT_PRIORITY_LAST_CONNECTED,
    CONNECT_PRIORITY_STRONGEST
  };
  void setConnectPriority(ConnectPriority priority);
  void prioritizeLastSaved();       // Simplified API
  void prioritizeLastConnected();   // Simplified API
  void prioritizeStrongestSignal(); // Simplified API

  // Manually force the portal to start
  void startPortal();

  // Reset saved credentials
  void resetSettings();

  // Check if we are currently connected to the configured WiFi
  bool isConnected();

  // Blocking connection method - simpler API
  // Returns true if connected within timeout
  bool connect(unsigned long timeout = 40000);

  // Check if portal is currently active
  bool isPortalActive();

  // Get the name of the currently connected SSID
  String getConnectedSSID();

  // Get the last connected SSID (useful for debugging)
  String getLastConnectedSSID();

  // Get connection status with detailed information
  ConnectionStatus getStatus();

  // Get human-readable status message
  String getStatusMessage();

  // Credential Accessors
  int getSavedNetworkCount();
  String getSavedSSID(int index);
  String getSavedPassword(int index);
  void deleteCredential(int index);

  // Status Callback
  typedef void (*StatusCallback)(const char *msg);
  void setStatusCallback(StatusCallback cb);

  // Connected Callback - fires when successfully connected
  typedef void (*OnConnectedCallback)(String ssid);
  void onConnected(OnConnectedCallback cb);

  void setAutoFallbackToAP(bool enable);
  void setPortalAutoConnect(bool enable);  // Auto-connect after saving (default: true)
  bool tryConnectSaved();

  // Continuous Reconnection
  void setAutoReconnect(bool enable);
  void setReconnectInterval(unsigned long ms);

  // Retry Configuration
  void setMaxRetries(int maxRetries);      // How many networks to try (default: maxNetworks)
  void setRetryDelay(unsigned long ms);    // Delay between retry attempts (default: 100ms)

private:
  Preferences _prefs;
  WebServer _server;
  DNSServer _dnsServer;
  StatusCallback _statusCallback = NULL;
  OnConnectedCallback _onConnectedCallback = NULL;

  String _apSSID;
  String _apPass;
  bool _portalActive;
  bool _autoFallbackAP = true;
  bool _portalAutoConnect = true;  // Auto-connect after saving to portal

  // Reconnection Logic
  bool _autoReconnect = true;
  unsigned long _reconnectInterval = 10000;
  unsigned long _lastReconnectAttempt = 0;

  // Retry Configuration
  int _maxRetries;
  unsigned long _retryDelay = 100;

  int _maxNetworks;
  unsigned long _connectTimeout;
  bool _switchPending;
  unsigned long _switchTime;

  // Track if already notified about connection
  bool _connectedNotified = false;

  // Connection State Machine
  enum ConnectState {
    STATE_IDLE,
    STATE_SCANNING,
    STATE_TRYING_SAVED,
    STATE_TRYING_LAST_CONNECTED,
    STATE_TRYING_STRONGEST,
    STATE_PORTAL
  };

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
  int _bestNetworkIndex = -1; // For Strongest Priority

  // Priority Mode
  ConnectPriority _priority = CONNECT_PRIORITY_LAST_SAVED;
  String _lastConnectedSSID = "";

  // Status tracking
  ConnectionStatus _currentStatus = STATUS_DISCONNECTED;
  unsigned long _lastStatusCheck = 0;
};

#endif
