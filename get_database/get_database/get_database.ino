#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#define ARDUINOJSON_ENABLE_STD_STRING 1

// ========== NEW FIREBASE CONFIG ==========
#define FIREBASE_HOST "save-in-a-database-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "rD8ShXsTAbQkAPrMZoAxBnZMWPUdqOMpFnrd4PRY"
#define RELAY_PIN 2
// ========================================

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

Preferences prefs;
String ssid = "", password = "";
const unsigned long WIFI_TIMEOUT = 80000;
const unsigned long SEND_INTERVAL = 10000;

bool relayState = false;
bool wifiWasConnected = false;

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  loadWiFiCredentials();
  connectWiFi();
  wifiWasConnected = (WiFi.status() == WL_CONNECTED);

  // NTP: IST (+5:30)
  configTime(19800, 0, "pool.ntp.org");
  Serial.print("Syncing time (IST)");
  while (time(nullptr) < 1000000000L) { delay(500); Serial.print("."); }
  Serial.println("\nTime synced (IST)!");

  connectFirebase();
  if (Firebase.ready()) Firebase.RTDB.setBool(&fbdo, "/esp32/control/relay", false);
}

void loop() {
  bool nowConnected = (WiFi.status() == WL_CONNECTED);

  if (wifiWasConnected && !nowConnected) {
    Serial.println(F("WiFi LOST → reset..."));
    WiFi.disconnect(true); WiFi.mode(WIFI_OFF); delay(1000); WiFi.mode(WIFI_STA);
    connectWiFi();
  }
  if (!wifiWasConnected && nowConnected) connectFirebase();
  wifiWasConnected = nowConnected;

  if (nowConnected && !Firebase.ready()) connectFirebase();

  if (nowConnected && Firebase.ready()) {
    if (Firebase.RTDB.getBool(&fbdo, "/esp32/control/relay")) {
      bool cmd = fbdo.to<bool>();
      if (cmd != relayState) {
        relayState = cmd;
        digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
        Serial.printf("App → Relay: %s\n", relayState ? "ON" : "OFF");
        logStateWithTime();
      }
    }
  }

  static unsigned long lastSend = 0;
  if (nowConnected && Firebase.ready() && millis() - lastSend > SEND_INTERVAL) {
    sendStatus(); lastSend = millis();
  }

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n'); cmd.trim(); cmd.toLowerCase();
    if (cmd == "on") setRelay(true);
    else if (cmd == "off") setRelay(false);
    else if (cmd == "resetwifi") { prefs.clear(); ESP.restart(); }
  }
  delay(500);
}

// === HELPERS ===
void loadWiFiCredentials() {
  prefs.begin("wifi", true);
  ssid = prefs.getString("ssid", ""); password = prefs.getString("pass", "");
  prefs.end();
  if (ssid == "" || password == "") promptForCredentials();
  else Serial.printf("Loaded WiFi: %s\n", ssid.c_str());
}
void promptForCredentials() {
  Serial.println(F("\n=== WiFi Setup ==="));
  Serial.print("SSID: "); ssid = readSerialLine();
  Serial.print("Password: "); password = readSerialLine();
  prefs.begin("wifi", false); prefs.putString("ssid", ssid); prefs.putString("pass", password); prefs.end();
  Serial.println(F("Saved!"));
}
String readSerialLine() {
  String line = ""; unsigned long start = millis();
  while (millis() - start < 30000) {
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') { if (line.length() > 0) { Serial.println(); line.trim(); return line; } }
      else { line += c; Serial.print(c); }
    } delay(50);
  } Serial.println(F("\nTimeout!")); return "";
}
void connectWiFi() {
  Serial.printf("Connecting to \"%s\" ... ", ssid.c_str());
  WiFi.begin(ssid.c_str(), password.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT) { delay(500); Serial.print("."); }
  Serial.println(WiFi.status() == WL_CONNECTED ? F("\nWiFi CONNECTED!") : F("\nWiFi FAILED."));
  if (WiFi.status() == WL_CONNECTED) Serial.println(WiFi.localIP());
}
void connectFirebase() {
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;  // Web API Key
  Serial.print(F("Firebase init ... "));
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  Serial.println(Firebase.ready() ? F("OK") : F("FAILED"));
}
void setRelay(bool state) {
  relayState = state; digitalWrite(RELAY_PIN, state ? HIGH : LOW);
  if (Firebase.ready()) { Firebase.RTDB.setBool(&fbdo, "/esp32/control/relay", state); logStateWithTime(); }
}
void sendStatus() {
  Firebase.RTDB.setBool(&fbdo, "/esp32/status/relay", relayState);
  Firebase.RTDB.setString(&fbdo, "/esp32/status/ip", WiFi.localIP().toString());
  Firebase.RTDB.setInt(&fbdo, "/esp32/status/rssi", WiFi.RSSI());
  Firebase.RTDB.setInt(&fbdo, "/esp32/status/uptime", millis() / 1000);
  Serial.println(F("Status sent"));
}
void logStateWithTime() {
  if (!Firebase.ready()) return;
  time_t now = time(nullptr); struct tm timeinfo;
  if (localtime_r(&now, &timeinfo)) {
    char ts[30];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S+05:30", &timeinfo);
    String path = "/esp32/log/" + String(ts);
    if (Firebase.RTDB.setString(&fbdo, path.c_str(), relayState ? "ON" : "OFF")) {
      Serial.printf("Logged: %s at %s\n", relayState ? "ON" : "OFF", ts);
    }
  }
}