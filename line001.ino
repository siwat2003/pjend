// LINE Chatbot บน ESP32: พิมพ์ถามใน LINE เช่น "อุณหภูมิเท่าไหร่" แล้วบอร์ดตอบกลับ
// ไลบรารีที่ต้องติดตั้ง: "DHT sensor library" + "Adafruit Unified Sensor" + "ArduinoJson"
//
// วิธีทำงาน:
//   LINE --(webhook HTTPS)--> ngrok --> ESP32 (HTTP port 80) --(reply HTTPS)--> LINE
// ต้องตั้ง Webhook URL ใน LINE Developers Console เป็น https://xxxx.ngrok-free.app/webhook
// (รัน `ngrok http 192.168.x.x:80` โดยใช้ IP ของ ESP32 หรือรัน ngrok บนเครื่องที่ forward มาที่ ESP32)

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

// ====== แก้ค่าตรงนี้ ======
const char* WIFI_SSID     = "guy";
const char* WIFI_PASSWORD = "gggggggg";
const char* LINE_TOKEN    = "n+jm/diMArUCCqihGP4KhrSJ1kLhfxSI0hHALjDWCThOuD+164kN688GcarQLpcDgVpj8bCnYSrAMTSmq2PGPen21z964iUGa0N0eWJM9NbJ/LEHTEAlLK7RKWtWZSpFlWjfWEAdi85dvnTQ4A3kdwdB04t89/1O/w1cDnyilFU=";  // Channel access token (long-lived)

#define DHTPIN  4
#define DHTTYPE DHT22   // ถ้าใช้ DHT11 ให้เปลี่ยนเป็น DHT11
// ==========================

DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);

bool hasAny(const String& text, std::initializer_list<const char*> keys) {
  for (auto k : keys) {
    if (text.indexOf(k) >= 0) return true;
  }
  return false;
}

String buildAnswer(String text) {
  text.toLowerCase();
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  bool bad = isnan(t) || isnan(h);

  bool askTemp  = hasAny(text, {"อุณหภูมิ", "ร้อน", "หนาว", "temp"});
  bool askHumid = hasAny(text, {"ความชื้น", "humid"});

  if (!askTemp && !askHumid && !hasAny(text, {"สถานะ", "status", "ค่า"})) {
    return "ลองพิมพ์ถามว่า \"อุณหภูมิเท่าไหร่\" หรือ \"ความชื้นเท่าไหร่\" หรือ \"สถานะ\" ได้เลยครับ";
  }
  if (bad) return "อ่านค่าจากเซนเซอร์ไม่ได้ ตรวจสอบสายเซนเซอร์อีกครั้งครับ";

  if (askTemp && !askHumid) return "อุณหภูมิตอนนี้ " + String(t, 1) + " °C";
  if (askHumid && !askTemp) return "ความชื้นตอนนี้ " + String(h, 1) + " %";
  return "อุณหภูมิ " + String(t, 1) + " °C\nความชื้น " + String(h, 1) + " %";
}

void replyLine(const String& replyToken, const String& message) {
  WiFiClientSecure client;
  client.setInsecure();  // ไม่ตรวจใบรับรอง (ง่ายต่อการใช้งาน)
  HTTPClient https;
  if (!https.begin(client, "https://api.line.me/v2/bot/message/reply")) return;
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", String("Bearer ") + LINE_TOKEN);

  JsonDocument doc;
  doc["replyToken"] = replyToken;
  JsonObject msg = doc["messages"].add<JsonObject>();
  msg["type"] = "text";
  msg["text"] = message;
  String body;
  serializeJson(doc, body);

  int code = https.POST(body);
  Serial.printf("LINE reply status: %d\n", code);
  https.end();
}

void handleWebhook() {
  // ตอบ 200 ให้ LINE ก่อนเสมอ
  String body = server.arg("plain");
  server.send(200, "text/plain", "OK");

  JsonDocument doc;
  if (deserializeJson(doc, body)) return;

  for (JsonObject ev : doc["events"].as<JsonArray>()) {
    if (strcmp(ev["type"] | "", "message") != 0) continue;
    if (strcmp(ev["message"]["type"] | "", "text") != 0) continue;

    String text  = ev["message"]["text"].as<String>();
    String token = ev["replyToken"].as<String>();
    Serial.println("ข้อความ: " + text);
    replyLine(token, buildAnswer(text));
  }
}

void setup() {
  Serial.begin(115200);
  dht.begin();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("กำลังเชื่อมต่อ WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nIP: " + WiFi.localIP().toString());

  server.on("/webhook", HTTP_POST, handleWebhook);
  server.on("/", HTTP_GET, []() { server.send(200, "text/plain", "LINE bot is running"); });
  server.begin();
}

void loop() {
  server.handleClient();
}
