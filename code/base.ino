#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal.h>

RTC_DS3231 rtc;
// RS, E, D4, D5, D6, D7
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

// ==== USER SETTINGS ====
int  ALARM_HOUR_24 = 7; // default alarm hour
int  ALARM_MINUTE  = 0; // default alarm minute
const int ALARM_WINDOW_SECONDS = 30; // how long to show "WAKE UP!"
// ========================

// Alarm runtime state
uint32_t lastTriggerMinute = 0xFFFFFFFF; // allow re-trigger after minute changes
bool alarmActive      = false;
bool prevAlarmActive  = false;
unsigned long alarmStartMs = 0;

// --- Encoder pins ---
const uint8_t ENC_CLK = 7;
const uint8_t ENC_DT  = 8;
const uint8_t ENC_SW  = 9;

// Encoder runtime state
bool    settingMode     = false;  // false=normal, true=setting hours/minutes
bool    settingMinutes  = false;  // false=hours, true=minutes
unsigned long lastBtnChangeMs = 0;
bool    lastBtnState    = HIGH;   // INPUT_PULLUP; HIGH=not pressed
const unsigned long BTN_DEBOUNCE_MS = 50;

// --- RF TRANSMITTER ---
const int TX_GO_PIN = 10; // TX module pin wired to D10
bool txBurstActive = false;
unsigned long txGoUntil = 0;
const unsigned long TX_BURST_MS = 4000; // hold LOW for ~4 s

// --- helper: draw first line (16 chars) ---
void showTitle(bool alarmIsOn) {
  lcd.setCursor(0, 0);
  if (alarmIsOn) {
    lcd.print("    WAKE UP!    ");
  } else if (settingMode) {
    lcd.print("   SET  ALARM   ");
  } else {
    lcd.print("      TIME      ");
  }
}

// --- encoder reading (ben buxton style, threshold 2) ---
int8_t readEncoderStep() {
  static uint8_t prev = 0;    // previous 2-bit state: [CLK<<1 | DT]
  static int8_t  acc  = 0;    // accumulate sub-steps

  uint8_t s = (digitalRead(ENC_CLK) << 1) | digitalRead(ENC_DT);
  static const int8_t lut[16] = {
    0,-1,+1,0,  +1,0,0,-1,  -1,0,0,+1,  0,+1,-1,0
  };
  acc += lut[(prev << 2) | s];
  prev = s;

  int8_t step = 0;
  if (acc >= 2)   { step = +1; acc = 0; }
  else if (acc <= -2) { step = -1; acc = 0; }
  return step;
}

// --- button with debounce & edge detection ---
bool buttonPressed() {
  bool reading = digitalRead(ENC_SW); // HIGH=idle, LOW=pressed
  unsigned long now = millis();
  if (reading != lastBtnState && (now - lastBtnChangeMs) > BTN_DEBOUNCE_MS) {
    lastBtnChangeMs = now;
    lastBtnState = reading;
    if (reading == LOW) return true; // just pressed
  }
  return false;
}

// --- print 12h time on line 2 ---
void printTime12h(const DateTime& now) {
  int h24 = now.hour(), h12 = h24;
  const char* ampm = "AM";
  if (h12 == 0) { h12 = 12; ampm = "AM"; }
  else if (h12 == 12) { ampm = "PM"; }
  else if (h12 > 12) { h12 -= 12; ampm = "PM"; }

  lcd.setCursor(2, 1);
  if (h12 < 10) lcd.print(' ');
  lcd.print(h12); lcd.print(':');
  if (now.minute() < 10) lcd.print('0');
  lcd.print(now.minute()); lcd.print(':');
  if (now.second() < 10) lcd.print('0');
  lcd.print(now.second()); lcd.print(' ');
  lcd.print(ampm);
}

void setup() {
  lcd.begin(16, 2);
  Wire.begin();

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT,  INPUT_PULLUP);
  pinMode(ENC_SW,  INPUT_PULLUP);
  lastBtnState = digitalRead(ENC_SW);

  pinMode(TX_GO_PIN, OUTPUT);
  digitalWrite(TX_GO_PIN, HIGH);

  if (!rtc.begin()) {
    lcd.print("RTC not found");
    while (1);
  }
  
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  showTitle(false);
}

void loop() {
  DateTime now = rtc.now();

  // ===== Encoder UI =====
  if (buttonPressed()) {
    if (!settingMode) {
      settingMode = true; settingMinutes = false; showTitle(false);
    } else if (!settingMinutes) {
      settingMinutes = true;
    } else {
      settingMode = false; settingMinutes = false;
      lastTriggerMinute = 0xFFFFFFFF;    // re-arm
      showTitle(false);
    }
  }

  if (settingMode) {
    int8_t step = readEncoderStep();
    if (step) {
      if (!settingMinutes) ALARM_HOUR_24 = (ALARM_HOUR_24 + step + 24) % 24;
      else                 ALARM_MINUTE  = (ALARM_MINUTE  + step + 60) % 60;
    }
  } else {
    // ===== Alarm logic =====
    uint32_t thisMinute = now.unixtime() / 60;
    if (!alarmActive &&
        now.hour()   == ALARM_HOUR_24 &&
        now.minute() == ALARM_MINUTE  &&
        now.second() == 0 &&
        lastTriggerMinute != thisMinute) {

      alarmActive = true;
      alarmStartMs = millis();
      lastTriggerMinute = thisMinute;

      // Start TX burst
      txBurstActive = true;
      txGoUntil = millis() + TX_BURST_MS;
      digitalWrite(TX_GO_PIN, LOW);
    }

    // end LCD alarm window
    if (alarmActive && (millis() - alarmStartMs >= (unsigned long)ALARM_WINDOW_SECONDS * 1000UL)) {
      alarmActive = false;
    }
  }

  // ===== Maintain TX burst =====
  if (txBurstActive && millis() >= txGoUntil) {
    digitalWrite(TX_GO_PIN, HIGH);
    txBurstActive = false;
  }

  // Update title on state change
  static bool prevSettingMode = false;
  if (alarmActive != prevAlarmActive || settingMode != prevSettingMode) {
    showTitle(alarmActive);
    prevAlarmActive = alarmActive;
    prevSettingMode = settingMode;
  }

  // ===== Render second line =====
  lcd.setCursor(0, 1);
  lcd.print("                "); // clear line
  if (settingMode) {
    lcd.setCursor(3, 1);
    if (ALARM_HOUR_24 < 10) lcd.print('0'); lcd.print(ALARM_HOUR_24);
    lcd.print(':');
    if (ALARM_MINUTE  < 10) lcd.print('0'); lcd.print(ALARM_MINUTE);
  } else {
    printTime12h(now);
  }

  delay(50);
}