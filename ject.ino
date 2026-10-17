#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "DHT.h"

// ===== ใส่ค่าของตัวเอง 3 บรรทัดนี้ =====
#define WIFI_SSID      "Junphut"
#define WIFI_PASSWORD  "porunza2537
#define DEVICE_API_KEY "AbJydhTikdzKPwWAPvypOrRbMLTPAb-s7nGb7aOiDHSFZc_ApnZ_LQ"
// =======================================
#define DEVICE_ID      "esp32-1"
#define PI_BASE        "https://reflector-neatness-faster.ngrok-free.dev"

// ใช้รีเลย์ 2 ช่อง:
// ช่อง 4 (GPIO19) = START : ต่อ COM/NO ขนานกับปุ่ม START (3-4) -> กระตุก 0.5 วิ เพื่อเปิดปั๊ม (ตู้ค้างเองด้วย 13-14)
// ช่อง 3 (GPIO16) = STOP  : ต่อ COM/NC อนุกรมกับปุ่ม STOP     -> กระตุก 0.5 วิ เพื่อตัดปั๊ม
// ช่อง 1-2 (GPIO17,18) ไม่ได้ใช้
const int START_PIN = 19;   // รีเลย์ช่อง 4
const int STOP_PIN  = 16;   // รีเลย์ช่อง 3
const int UNUSED_PINS[2] = {17, 18};
// 2026-10-16: เสียบ ESP32 แล้วรีเลย์ติดค้างทุกช่อง = บอร์ดนี้ทำงานแบบ active LOW
const bool RELAY_ACTIVE_LOW = true;
const unsigned long PULSE_MS = 500;           // เวลาจำลองการกดปุ่ม (มิลลิวินาที)

// ปุ่มกดเล็กต่อกับ ESP32 (ถ้ามี): ต่อปุ่มระหว่างขา GPIO กับ GND
// ถ้าไม่ได้ต่อปุ่ม ก็ไม่ต้องแก้อะไร โค้ดจะไม่ทำอะไร
const int BTN_START_PIN = 14;
const int BTN_STOP_PIN  = 15;

// สถานะที่ Pi สั่งมา 4 ช่อง (เก็บไว้ตอบกลับ Pi ให้ตรงกัน)
// ถ้า Pi สั่งเปิดช่องไหนก็ตาม = ปั๊มทำงาน
bool relayState[4] = {false, false, false, false};
bool pumpOn = false;

// DHT22
#define DHTPIN 23
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

const unsigned long SEND_INTERVAL_MS = 60000;
const unsigned long POLL_INTERVAL_MS = 500;
const int LONG_POLL_SEC = 20;
const unsigned long FAILSAFE_MS = 120000;   // ติดต่อ Pi ไม่ได้เกิน 2 นาที -> ปิดปั๊ม

unsigned long lastSend = 0;
unsigned long lastPoll = 0;
unsigned long lastPiOk = 0;
bool failsafeOn = false;

void writeRelay(int pin, bool on) {
  bool level = RELAY_ACTIVE_LOW ? !on : on;
  digitalWrite(pin, level ? HIGH : LOW);
}

SemaphoreHandle_t relayLock;   // กันโค้ด 2 ส่วนกดรีเลย์พร้อมกัน

// กดปุ่มด้วยรีเลย์ครู่หนึ่ง (PULSE_MS) แล้วปล่อย
// อินเตอร์ล็อก: ปล่อยอีกปุ่มก่อนเสมอ START กับ STOP จะไม่ทำงานพร้อมกัน
void pressButton(int pin) {
  xSemaphoreTake(relayLock, portMAX_DELAY);
  int other = (pin == START_PIN) ? STOP_PIN : START_PIN;
  writeRelay(other, false);
  delay(100);
  writeRelay(pin, true);
  delay(PULSE_MS);
  writeRelay(pin, false);
  xSemaphoreGive(relayLock);
}

void writePump(bool on) {
  if (pumpOn == on) return;
  pumpOn = on;
  if (on) {
    pressButton(START_PIN);
    Serial.println("ปั๊ม -> ON (กด START)");
  } else {
    pressButton(STOP_PIN);
    Serial.println("ปั๊ม -> OFF (กด STOP)");
  }
}

bool pressed(int pin) {
  if (digitalRead(pin) == LOW) {
    delay(30);                       // กันปุ่มเด้ง
    return digitalRead(pin) == LOW;
  }
  return false;
}

void waitRelease(int pin) {
  while (digitalRead(pin) == LOW) delay(10);
}

// เช็คปุ่มกดที่ ESP32: STOP สำคัญกว่า เช็คก่อน
void checkButtons() {
  if (pressed(BTN_STOP_PIN)) {
    Serial.println("กดปุ่ม STOP");
    pressButton(STOP_PIN);
    pumpOn = false;
    waitRelease(BTN_STOP_PIN);
  } else if (pressed(BTN_START_PIN)) {
    Serial.println("กดปุ่ม START");
    pressButton(START_PIN);
    pumpOn = true;
    waitRelease(BTN_START_PIN);
  }
}

// งานแยก: เช็คปุ่มตลอดเวลา ไม่ต้องรอ Pi ตอบ
void buttonTask(void *param) {
  for (;;) {
    checkButtons();
    delay(10);
  }
}

void setRelay(int i, bool on) {
  relayState[i] = on;
  bool any = false;
  for (int k = 0; k < 4; k++) any = any || relayState[k];
  writePump(any);
}

// กันน้ำค้าง: ถ้าไม่ได้ 200 จาก Pi เกิน 2 นาที ปิดปั๊ม
void failsafeCheck() {
  if (!failsafeOn && millis() - lastPiOk > FAILSAFE_MS) {
    for (int i = 0; i < 4; i++) setRelay(i, false);
    failsafeOn = true;
    Serial.println("ติดต่อ Pi ไม่ได้เกิน 2 นาที ปิดปั๊มแล้ว");
  }
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print(".");
    failsafeCheck();
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi Connected! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi ต่อไม่ได้ จะลองใหม่รอบหน้า");
  }
}

void beginHttp(HTTPClient &http, WiFiClientSecure &secureClient, const String &url, int timeoutMs) {
  if (url.startsWith("https")) {
    secureClient.setInsecure();
    http.begin(secureClient, url);
  } else {
    http.begin(url);
  }
  http.setTimeout(timeoutMs);
  http.addHeader("ngrok-skip-browser-warning", "1");
  http.addHeader("X-API-Key", DEVICE_API_KEY);
}

void sendReading() {
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("อ่านค่า DHT22 ไม่สำเร็จ");
    return;
  }
  Serial.printf("Temperature: %.1f C | Humidity: %.1f %%\n", temperature, humidity);
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  WiFiClientSecure secureClient;
  beginHttp(http, secureClient, String(PI_BASE) + "/api/reading", 10000);
  http.addHeader("Content-Type", "application/json");
  String json = "{\"device\":\"" DEVICE_ID "\",\"temperature\":" + String(temperature, 1) +
                ",\"humidity\":" + String(humidity, 1) + "}";
  int code = http.POST(json);
  Serial.print("Reading HTTP Code: ");
  Serial.println(code);
  http.end();
}

void pollRelays() {
  if (WiFi.status() != WL_CONNECTED) return;

  String known = "";
  for (int i = 0; i < 4; i++) known += relayState[i] ? "1" : "0";
  String url = String(PI_BASE) + "/api/relays?device=" DEVICE_ID "&known=" + known +
               "&wait=" + String(LONG_POLL_SEC);

  HTTPClient http;
  WiFiClientSecure secureClient;
  beginHttp(http, secureClient, url, (LONG_POLL_SEC + 10) * 1000);
  int code = http.GET();
  if (code == 200) {
    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, http.getString());
    if (!err && doc["relays"].is<JsonArray>()) {
      // นับว่าคุยกับ Pi ได้ เฉพาะตอนได้ข้อมูลรีเลย์จริง (กันหน้า error ของ ngrok ที่ตอบ 200)
      lastPiOk = millis();
      if (failsafeOn) {
        failsafeOn = false;
        Serial.println("ติดต่อ Pi ได้แล้ว กลับมาทำงานปกติ");
      }
      JsonArray relays = doc["relays"].as<JsonArray>();
      for (int i = 0; i < 4 && i < (int)relays.size(); i++) {
        setRelay(i, relays[i].as<int>() == 1);
      }
    } else {
      Serial.println("อ่าน JSON จาก Pi ไม่ได้");
    }
  } else {
    Serial.print("Poll HTTP Code: ");
    Serial.println(code);
    http.end();
    delay(3000);
    return;
  }
  http.end();
}

void setup() {
  // ตั้งเป็นปิดก่อน กันรีเลย์กระตุกตอนเปิดเครื่อง
  bool offLevel = RELAY_ACTIVE_LOW ? HIGH : LOW;
  digitalWrite(START_PIN, offLevel);
  pinMode(START_PIN, OUTPUT);
  digitalWrite(STOP_PIN, offLevel);
  pinMode(STOP_PIN, OUTPUT);
  for (int i = 0; i < 2; i++) {
    digitalWrite(UNUSED_PINS[i], offLevel);
    pinMode(UNUSED_PINS[i], OUTPUT);
  }
  pinMode(BTN_START_PIN, INPUT_PULLUP);
  pinMode(BTN_STOP_PIN, INPUT_PULLUP);
  relayLock = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(buttonTask, "buttons", 4096, NULL, 1, NULL, 0);
  Serial.begin(115200);
  dht.begin();
  lastPiOk = millis();
  connectWiFi();
  delay(2000);
  pollRelays();
  sendReading();
  lastSend = millis();
}

void loop() {
  if (millis() - lastPoll >= POLL_INTERVAL_MS) {
    lastPoll = millis();
    connectWiFi();
    pollRelays();
  }
  if (millis() - lastSend >= SEND_INTERVAL_MS) {
    lastSend = millis();
    sendReading();
  }
  failsafeCheck();
}