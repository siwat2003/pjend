#include <TFT_eSPI.h>

TFT_eSPI tft;

const int LED1 = A0;
const int LED2 = A1;

const uint16_t BG = 0x041F;

// แถบความเร็วเต็มจอ: 10 ช่อง x 30px = 300px (x = 10..310)
const int MAX_LEVEL = 10;
const int BAR_X = 10, BAR_Y = 95, BOX_W = 30, BOX_H = 60;

int level = 5;                    // ระดับเริ่มต้น (1..MAX_LEVEL)

// ระดับ 1 = ช้าสุด 700ms, ระดับ 10 = เร็วสุด 40ms
unsigned long getDelay() {
  return map(level, 1, MAX_LEVEL, 700, 40);
}

bool ledState = false;
unsigned long lastToggle = 0;
unsigned long lastStep = 0;       // เวลาที่ปรับระดับครั้งล่าสุด (สำหรับกดค้าง)
const unsigned long REPEAT_MS = 120;

void drawBoxes() {
  for (int i = 0; i < MAX_LEVEL; i++) {
    int x = BAR_X + i * BOX_W;
    tft.fillRect(x, BAR_Y, BOX_W, BOX_H, i < level ? TFT_WHITE : BG);
    tft.drawRect(x, BAR_Y, BOX_W, BOX_H, TFT_BLACK);
  }
}

void drawScreen() {
  tft.fillScreen(BG);

  tft.fillRect(10, 10, 300, 40, TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.drawCentreString("Blinkster V.1.0", 160, 22, 2);

  tft.fillRect(10, 190, 300, 40, TFT_WHITE);
  tft.drawCentreString("by Apisake Hongwitayakorn", 160, 202, 2);

  drawBoxes();
}

void setup() {
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(WIO_5S_RIGHT, INPUT_PULLUP);
  pinMode(WIO_5S_LEFT,  INPUT_PULLUP);

  tft.begin();
  tft.setRotation(3);
  drawScreen();
}

void loop() {
  bool right = (digitalRead(WIO_5S_RIGHT) == LOW);
  bool left  = (digitalRead(WIO_5S_LEFT)  == LOW);

  // กดครั้งแรกขยับทันที กดค้างจะขยับซ้ำทุก REPEAT_MS
  if ((right || left) && millis() - lastStep >= REPEAT_MS) {
    int old = level;
    if (right && level < MAX_LEVEL) level++;   // ขวา = เร็วขึ้น
    if (left  && level > 1)         level--;   // ซ้าย = ช้าลง
    if (level != old) drawBoxes();
    lastStep = millis();
  }
  if (!right && !left) lastStep = 0;           // ปล่อยปุ่ม -> กดครั้งต่อไปขยับทันที

  // ไฟวิ่งสลับ 2 ดวง
  if (millis() - lastToggle >= getDelay()) {
    lastToggle = millis();
    ledState = !ledState;
    digitalWrite(LED1, ledState);
    digitalWrite(LED2, !ledState);
  }
}
