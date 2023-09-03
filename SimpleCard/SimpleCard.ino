#include <SPI.h>
#include <MFRC522.h>
#include <WebServer.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <FastLED.h>

#define SS_PIN    21  // Define the SS pin (Slave Select) for the RFID module.
#define RST_PIN   22 // Define the RST pin for the RFID module.
#define RELAY_PIN 13 // Define the pin connected to the relay.
#define BLOCK_PIN 14 // Define the pin connected to the LDR for Coin Blocking from cab
#define LED_PIN   12 // Define the pin connected to the WS2812 LED strip.
#define NUM_LEDS  1  // Define the number of LEDs in the strip.

CRGB leds[NUM_LEDS]; // Create an array of CRGB colors for the LEDs.
MFRC522 mfrc522(SS_PIN, RST_PIN); // Create an MFRC522 instance.
const char *ssid = "Radio Noise AX";
const char *password = "Radio Noise AX";
WebServer server(80);
const char *apiUrl = "http://card-services.nyti.ne.jp:1777/";
int enableState = 0;
int blockState = 0;
int blockOverride = 0;

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Initially, set the relay to OFF.

  SPI.begin(); // Initialize SPI communication.
  mfrc522.PCD_Init(); // Initialize the RFID module.
  mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_48dB);
  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS); // Initialize the LED strip.

  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::Black;
  }
  FastLED.show();
  checkWiFiConnection();

  server.on("/block_overide", [=]() {
    if (blockOverride == 0) {
      blockOverride = 1;
    } else {
      blockOverride = 0;
    }
    enableState = 1;
    server.send(200, "text/plain", ((blockOverride == 1) ? "Overided" : "Normal"));
  });
  server.on("/credit", [=]() {
    Serial.println("Card OK, Dispense Credit");
      digitalWrite(RELAY_PIN, HIGH);
      delay(100);
      digitalWrite(RELAY_PIN, LOW);
      blinkLEDs(CRGB::Green, 2000);
      blinkLEDs(CRGB::DarkGray, 3000);
    server.send(200, "text/plain", "OK");
  });
  server.on("/enable", [=]() {
    if (blockState = 0) {
      for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB::Yellow;
      }
      FastLED.show();
    }
    enableState = 1;
    server.send(200, "text/plain", "OK");
  });
  server.on("/disable", [=]() {
    for (int i = 0; i < NUM_LEDS; i++) {
      leds[i] = CRGB::Black;
    }
    FastLED.show();
    enableState = 0;
    server.send(200, "text/plain", "OK");
  });
  server.on("/status", [=]() {
    String assembledOutput = "";
    assembledOutput += ((enableState == 0) ? "Disabled" : ((blockState == 1) ? "Blocked" : "Enabled"));
    server.send(200, "text/plain", assembledOutput);
  });
  
  server.begin();

  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::Yellow;
  }
  FastLED.show();
  enableState = 1;
  Serial.println("Ready to scan RFID cards...");
}

void loop() {
  if (digitalRead(BLOCK_PIN) == HIGH && blockOverride == 0) {
    if (blockState == 0) {
      blockState = 1;
      for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB::Black;
      }
      FastLED.show();
    }
  } else if (blockState == 1) {
    blockState = 0;
    enableState = 1;
    for (int i = 0; i < NUM_LEDS; i++) {
      leds[i] = CRGB::Yellow;
    }
    FastLED.show();
  }

  if (enableState == 1 && blockState == 0 && mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    // A new card is detected, read its UID.
    String uid = getUID();
    for (int i = 0; i < NUM_LEDS; i++) {
      leds[i] = CRGB::Black;
    }
    int response = sendRequest(uid);
    FastLED.show();
    if (response >= 200 && response < 300) {
      Serial.println("Card OK, Dispense Credit");
      digitalWrite(RELAY_PIN, HIGH);
      delay(100);
      digitalWrite(RELAY_PIN, LOW);
      if (response == 200) {
        blinkLEDs(CRGB::Green, 2000);
      } else {
        blinkLEDs(CRGB::Green, 1000);
        blinkLEDs(CRGB::Orange, 500);
        blinkLEDs(CRGB::Green, 1000);
        blinkLEDs(CRGB::Orange, 500);
        blinkLEDs(CRGB::Green, 1000);
      }
    } else {
      Serial.println("Access denied! " + uid);
      blinkLEDs(CRGB::Red, 1500);
    }
    blinkLEDs(CRGB::DarkGray, 3000);
  }
  checkWiFiConnection();
  server.handleClient();
}

String getUID() {
  String uid = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}
void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
      for (int i = 0; i < NUM_LEDS; i++) {
      leds[i] = CRGB::Magenta;
    }
    FastLED.show();
    Serial.println("WiFi not connected. Attempting to reconnect...");
    WiFi.hostname("SimpleCard");
    WiFi.disconnect(true);
    WiFi.begin(ssid, password);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    int tryCount = 0;
    while (WiFi.status() != WL_CONNECTED) {
      if (tryCount > 30) {
        ESP.restart();
      }
      delay(1000);
      Serial.print(".");
      tryCount++;
    }
    Serial.println("\nConnected to WiFi");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  }
}
bool sendRequest(String cardUID) {
  HTTPClient http;
  String url = String(apiUrl) + "dispense/" + cardUID;
  Serial.println("Sending GET request to: " + url);
  http.begin(url);
  int httpCode = http.GET();
  http.end();
  Serial.println("HTTP Response code: " + String(httpCode));
  return httpCode;
}
void blinkLEDs(CRGB color, int duration) {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = color;
  }
  FastLED.show();
  delay(duration);
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::Yellow;
  }
  FastLED.show();
}