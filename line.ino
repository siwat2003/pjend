#include <WiFi.h>
#include <HTTPClient.h>
#include "DHT.h"

// WiFi
const char* WIFI_SSID = "parn";
const char* WIFI_PASSWORD = "kikieiei";

// LINE
const char* CHANNEL_ACCESS_TOKEN = "n+jm/diMArUCCqihGP4KhrSJ1kLhfxSI0hHALjDWCThOuD+164kN688GcarQLpcDgVpj8bCnYSrAMTSmq2PGPen21z964iUGa0N0eWJM9NbJ/LEHTEAlLK7RKWtWZSpFlWjfWEAdi85dvnTQ4A3kdwdB04t89/1O/w1cDnyilFU=";
const char* USER_ID = "Ub59bd9b2dadb5cc0362dee59b9f0a9ce";

// DHT22
#define DHTPIN 4
#define DHTTYPE DHT22

DHT dht(DHTPIN, DHTTYPE);

void setup() {

  Serial.begin(115200);

  dht.begin();

  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi Connected!");

  delay(2000);

  // อ่าน DHT22
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("อ่านค่า DHT22 ไม่สำเร็จ");
    return;
  }

  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.println(" C");

  Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");

  // LINE
  HTTPClient http;

  http.begin("https://api.line.me/v2/bot/message/push");

  http.addHeader("Content-Type", "application/json");

  http.addHeader(
    "Authorization",
    String("Bearer ") + CHANNEL_ACCESS_TOKEN
  );

  // ข้อความต้องเป็นบรรทัดเดียว
  String message =
    "Smart Sprinkler | Temperature: " +
    String(temperature, 1) +
    " C | Humidity: " +
    String(humidity, 1) +
    " %";

  // JSON
  String json =
    "{\"to\":\"" + String(USER_ID) +
    "\",\"messages\":[{\"type\":\"text\",\"text\":\"" +
    message +
    "\"}]}";

  Serial.println();
  Serial.println("JSON ที่ส่ง:");
  Serial.println(json);

  int httpCode = http.POST(json);

  Serial.println();
  Serial.print("LINE HTTP Code: ");
  Serial.println(httpCode);

  Serial.println("LINE Response:");
  Serial.println(http.getString());

  http.end();
}

void loop() {
}