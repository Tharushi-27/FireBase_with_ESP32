#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#define ARDUINOJSON_ENABLE_STD_STRING 1

// ========== YOUR CONFIG ==========
#define FIREBASE_HOST "userinput-wifi-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "WgZ4LV2V5oFgWOC886CUozmQx6yNHAfen0P6pd0X"
#define RELAY_PIN 2
// =================================

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

Preferences prefs;
String ssid = "";
String password = "";

const unsigned long WIFI_TIMEOUT = 80000;
const unsigned long SEND_INTERVAL = 10000;

bool relayState = false;
bool wifiWasConnected = false;

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  Serial.println(F("\n=== ESP32 STARTING ==="));

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  loadWiFiCredentials();
  connectWiFi();
  wifiWasConnected = (WiFi.status() == WL_CONNECTED);
  connectFirebase();

  if (Firebase.ready()) {
    Firebase.RTDB.setBool(&fbdo, "/esp32/control/relay", false);
  }
}

void loop() {
  bool nowConnected = (WiFi.status() == WL_CONNECTED);

  // WiFi LOST?
  if (wifiWasConnected && !nowConnected) {
    Serial.println(F("WiFi LOST → full reset..."));
    WiFi.disconnect(true);     // FULL disconnect
    WiFi.mode(WIFI_OFF);       // Turn off
    delay(1000);
    WiFi.mode(WIFI_STA);       // Re-enable
    connectWiFi();
  }

  // WiFi GAINED?
  if (!wifiWasConnected && nowConnected) {
    Serial.println(F("WiFi RECONNECTED!"));
    connectFirebase();
  }

  wifiWasConnected = nowConnected;

  // Firebase reconnect
  if (nowConnected && !Firebase.ready()) {
    Serial.println(F("Firebase lost → reconnecting..."));
    connectFirebase();
  }

  // Read relay
  if (nowConnected && Firebase.ready()) {
    if (Firebase.RTDB.getBool(&fbdo, "/esp32/control/relay")) {
      bool cmd = fbdo.to<bool>();
      if (cmd != relayState) {
        relayState = cmd;
        digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
        Serial.printf("App → Relay: %s\n", relayState ? "ON" : "OFF");
      }
    }
  }

  // Send status
  static unsigned long lastSend = 0;
  if (nowConnected && Firebase.ready() && millis() - lastSend > SEND_INTERVAL) {
    sendStatus();
    lastSend = millis();
  }

  // Serial commands
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();
    if (cmd == "on") setRelay(true);
    else if (cmd == "off") setRelay(false);
    else if (cmd == "resetwifi") {
      prefs.clear();
      Serial.println(F("WiFi reset. Restarting..."));
      ESP.restart();
    }
  }

  delay(500);
}

// ============== HELPERS ==============

void loadWiFiCredentials() {
  prefs.begin("wifi", true);
  ssid = prefs.getString("ssid", "");
  password = prefs.getString("pass", "");
  prefs.end();
  if (ssid == "" || password == "") promptForCredentials();
  else Serial.printf("Loaded WiFi: %s\n", ssid.c_str());
}

void promptForCredentials() {
  Serial.println(F("\n=== WiFi Setup ==="));
  Serial.print("SSID: "); ssid = readSerialLine();
  Serial.print("Password: "); password = readSerialLine();

  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", password);
  prefs.end();
  Serial.println(F("Saved!"));
}

String readSerialLine() {
  String line = "";
  unsigned long start = millis();
  while (millis() - start < 30000) {
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        if (line.length() > 0) {
          Serial.println();
          line.trim();
          return line;
        }
      } else {
        line += c;
        Serial.print(c);
      }
    }
    delay(50);
  }
  Serial.println(F("\nTimeout!"));
  return "";
}

void connectWiFi() {
  Serial.printf("Connecting to \"%s\" ... ", ssid.c_str());
  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("\nWiFi CONNECTED!"));
    Serial.print(F("IP: ")); Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("\nWiFi FAILED. Check SSID/password."));
  }
}

void connectFirebase() {
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Serial.print(F("Firebase init ... "));
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  Serial.println(Firebase.ready() ? F("OK") : F("FAILED"));
}

void setRelay(bool state) {
  relayState = state;
  digitalWrite(RELAY_PIN, state ? HIGH : LOW);
  if (Firebase.ready()) {
    Firebase.RTDB.setBool(&fbdo, "/esp32/control/relay", state);
    Serial.printf("Relay → %s\n", state ? "ON" : "OFF");
  }
}

void sendStatus() {
  Firebase.RTDB.setBool(&fbdo, "/esp32/status/relay", relayState);
  Firebase.RTDB.setString(&fbdo, "/esp32/status/ip", WiFi.localIP().toString());
  Firebase.RTDB.setInt(&fbdo, "/esp32/status/rssi", WiFi.RSSI());
  Firebase.RTDB.setInt(&fbdo, "/esp32/status/uptime", millis() / 1000);
  Serial.println(F("Status sent"));
}