#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>

const char* WIFI_SSID     = "tharu";
const char* WIFI_PASSWORD = "tharu0927";

#define LED_PIN 2

// MUST END WITH /
#define FIREBASE_DATABASE_URL "https://led-esp32-137db-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define DATABASE_SECRET       "yQ1oCrwXl0MbrroVDAahZ2P7mriSUCwqAh9jZhTu"  // FROM FIREBASE

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
int lastLedState = -1;

void applyLed(int val) {
  int state = (val != 0) ? HIGH : LOW;
  if (state != lastLedState) {
    digitalWrite(LED_PIN, state);
    lastLedState = state;
    Serial.printf("LED: %s\n", state ? "ON" : "OFF");
  }
}

void streamCallback(FirebaseStream data) {
  if (data.dataType() == "int" || data.dataType() == "string") {
    applyLed(atoi(data.stringData().c_str()));
  }
}

void streamTimeoutCallback(bool timeout) {
  if (timeout) Serial.println("Stream timeout");
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print('.'); }
  Serial.println("\nIP: " + WiFi.localIP().toString());

  // USE DATABASE SECRET (NOT API KEY!)
  config.database_url = FIREBASE_DATABASE_URL;
  config.signer.tokens.legacy_token = DATABASE_SECRET;
  config.token_status_callback = tokenStatusCallback;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.print("Signing in with secret");
  unsigned long t = millis();
  while (!Firebase.ready() && millis() - t < 15000) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println();

  if (!Firebase.ready()) {
    Serial.println("\nFAILED! Check secret & URL");
    while (1) delay(1000);
  } else {
    Serial.println("SIGNED IN! Legacy token active");
  }

  if (!Firebase.RTDB.beginStream(&fbdo, "/led")) {
    Serial.println("Stream error: " + fbdo.errorReason());
  } else {
    Serial.println("Stream started!");
    Firebase.RTDB.setStreamCallback(&fbdo, streamCallback, streamTimeoutCallback);
  }
}

void loop() {
  if (!Firebase.ready()) { delay(1000); return; }

  if (!fbdo.httpConnected()) {
    if (Firebase.RTDB.getInt(&fbdo, "/led")) {
      applyLed(fbdo.to<int>());
    }
    delay(2000);
  } else {
    delay(100);
  }
}
