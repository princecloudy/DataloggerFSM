#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>
#include <Preferences.h>

// ================= CONFIG =================
#define DHTPIN 4
#define DHTTYPE DHT11

#define SD_CS 5
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCK 18

#define BTN_UP    12
#define BTN_DOWN  14
#define BTN_SAVE  27

#define SDA_PIN 21
#define SCL_PIN 22

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

#define CSV_HEADER "timestamp_ms,datetime,temp_C,humidity_pct"

// ================= OBJECT =================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// RTC
RTC_DS3231 rtc;

// DHT
DHT dht(DHTPIN, DHTTYPE);
float Temp = NAN;
float Hum = NAN;

// Preferences (non-volatile)
Preferences prefs;
const char *PREF_NAMESPACE = "datalogger";
const char *PREF_KEY_INTERVAL = "interval_s";

// ================= FSM =================
enum State {
  STATE_LOGGING,
  STATE_DISPLAY_SETTING,
  STATE_SAVE_SETTING,
  STATE_EJECT
};

State currentState = STATE_LOGGING;

// ================= GLOBAL =================
unsigned long logInterval = 10;
unsigned long lastLogMillis = 0; 
unsigned long lastLogTimeMs = 0; //for delta_s

// Interval clamp
const unsigned long MIN_INTERVAL = 1;
const unsigned long MAX_INTERVAL = 3600;

// Button debounce
unsigned long lastBtnMillis = 0;
const unsigned long BTN_DEBOUNCE = 150;

// Menu timeout
const unsigned long MENU_TIMEOUT = 5000;
unsigned long lastMenuActivity = 0;

// Accelerated button
unsigned long btnHoldStartUp = 0;
unsigned long btnHoldStartDown = 0;
bool btnUpHolding = false;
bool btnDownHolding = false;

// Eject SD Card
unsigned long btnSaveHoldStart = 0;
bool btnSaveHolding = false;

// SD filename helper
char logFilename[32];

// Flag SD Card
volatile bool sdBusy = false;

// Reset Timer ESP32
unsigned long intervalStartMillis = 0;

// ================= UTIL =================
String twoDigits(int v) {
  return (v < 10) ? "0" + String(v) : String(v);
}

String formatDateTime(DateTime dt) {
  // "YYYY-MM-DD HH:MM:SS"
  return String(dt.year()) + "-" + twoDigits(dt.month()) + "-" +
         twoDigits(dt.day()) + " " + twoDigits(dt.hour()) + ":" + 
         twoDigits(dt.minute()) + ":" + twoDigits(dt.second());
}

void makeLogFilename(DateTime dt) {
  // namefile "LOG_YYYYMMDD.csv"
  snprintf(logFilename, sizeof(logFilename),
           "/LOG_%04d%02d%02d.csv", dt.year(), dt.month(), dt.day());
}

bool buttonPressed(int pin) {
  if (millis() - lastBtnMillis < BTN_DEBOUNCE) return false;
  if (digitalRead(pin) == LOW) {
    lastBtnMillis = millis();
    return true;
  }
  return false;
}

unsigned long getStep(unsigned long heldMs) {
  if (heldMs < 500)       return 1;
  else if (heldMs < 2000) return 10;
  else if (heldMs < 4000) return 100;
  else                    return 500;
}

// ================= DISPLAY =================
void showSettingScreen() {
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("SET SAMPLING TIME");

  display.setCursor(0,16);
  display.print("Interval: ");
  display.print(logInterval);
  display.println(" s");

  display.setCursor(0,32);
  display.println("Tap:1 Hold:10/100/500");

  display.setCursor(0,48);
  display.println("MENU = Save");
  display.display();
}

void showSavedScreen() {
  display.clearDisplay();
  display.setCursor(0,24);
  display.println("SETTING SAVED");
  display.display();
}

void ejectScreen(const char* msg) {
  display.clearDisplay();
  display.setCursor(0, 20);
  display.println(msg);
  display.display();
}

// ================= LOGGING =================
void logToSD(DateTime now) {
  sdBusy = true;

  float t = Temp;
  float h = Hum;

  unsigned long nowMs = millis();
  unsigned long tsRel = nowMs - intervalStartMillis;

  float delta_t = 0;
  if (lastLogTimeMs != 0) {
    delta_t = (nowMs - lastLogTimeMs) / 1000.0;
  }
  lastLogTimeMs = nowMs;

  makeLogFilename(now);

  // Create header if new file
  if (!SD.exists(logFilename)) {
    File hf = SD.open(logFilename, FILE_WRITE);
    if (hf) {
      hf.println(CSV_HEADER);
      hf.close();
    }
  }

  File f = SD.open(logFilename, FILE_APPEND);
  if (f) {
    f.print(tsRel);               // timestamp_ms
    f.print(",");
    f.print(formatDateTime(now)); // datetime
    f.print(",");

    if (isnan(t) || isnan(h)) {
      f.print("ERR,ERR");
    } else {
      f.print(t,1);               // temp_C
      f.print(",");
      f.println(h,0);               // humidity_pct
    }

    f.flush();
    f.close();
  }

  delay(50);        // Delay for FAT settle
  sdBusy = false;
}

void handleLogging(unsigned long nowMillis) {
  DateTime now = rtc.now();

  Temp = dht.readTemperature();
  Hum  = dht.readHumidity();

  display.clearDisplay();
  display.setCursor(0,0);
  display.println(formatDateTime(now));

  display.setCursor(0,14);
  if (!isnan(Temp) && !isnan(Hum)) {
    display.print("T:");
    display.print(Temp,1);
    display.print("C  H:");
    display.print(Hum,0);
    display.print("%");
  } else {
    display.print("DHT ERROR");
  }

  display.setCursor(0,32);
  display.print("Interval: ");
  display.print(logInterval);
  display.print(" s");

  display.display();


  if (nowMillis - lastLogMillis >= logInterval * 1000UL) {
    //lastLogMillis += logInterval * 1000UL;
    lastLogMillis = nowMillis;
    logToSD(now);
  }
}

// ================= EJECT =================
void handleEject() {
  ejectScreen("Stopping logging...");

  // Tunggu jika SD sedang dipakai
  unsigned long t0 = millis();
  while (sdBusy && millis() - t0 < 1000) {
    delay(10);
  }

  // Pastikan tidak ada operasi SD
  SD.end();

  ejectScreen("SD SAFE TO REMOVE");
}

// ================= INTERVAL MARKER =================
void writeIntervalMarker(unsigned long newInterval) {
  //if (!SD.begin(SD_CS)) return;
  DateTime now = rtc.now();
  makeLogFilename(now);

  File f = SD.open(logFilename, FILE_APPEND);
  if (!f) return;

  //DateTime now = rtc.now();
  f.print("# ---- INTERVAL CHANGED TO ");
  f.print(newInterval);
  f.print(" s at ");
  f.print(formatDateTime(now));
  f.println(" ----");

  f.close();
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(200);

  // Pins
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_SAVE, INPUT_PULLUP);

  // I2C
  Wire.begin(SDA_PIN, SCL_PIN);

  // OLED init
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed");
    // continue but display won't work
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0,0);
    display.println("OLED ready");
    display.display();
  }

  // RTC init
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
  } else {
    if (rtc.lostPower()) {
      Serial.println("RTC LOST POWER!");
    }
  }

  dht.begin();

  // Preferences init
  prefs.begin(PREF_NAMESPACE, false);
  logInterval = prefs.getInt(PREF_KEY_INTERVAL, 10);
  logInterval = constrain(logInterval, MIN_INTERVAL, MAX_INTERVAL);

  if (logInterval != prefs.getInt(PREF_KEY_INTERVAL, 10)) {
    prefs.putInt(PREF_KEY_INTERVAL, logInterval); // normalize
  }

  //SPI
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  // SD init
  delay(500); //delay
  if (!SD.begin(SD_CS, SPI, 1000000)) {
    Serial.println("SD init failed!");
    if (display.width() > 0) {
      display.clearDisplay();
      display.setCursor(0,0);
      display.println("SD init failed!");
      display.display();
    } // If SD fails: still run but logging won't work
  } else {
    Serial.println("SD initialized.");
  }

  DateTime now = rtc.now();
  makeLogFilename(now);

  // Ensure header exists
  if (SD.begin(SD_CS)) {
    if (!SD.exists(logFilename)) {
      File f = SD.open(logFilename, FILE_WRITE);
      if (f) {
        f.println(CSV_HEADER);
        f.close();
      }
    }
  }

  // Initial display
  if (display.width() > 0) {
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("Data Logger Ready");
    display.print("Interval: ");
    display.print(logInterval);
    display.println(" s");
    display.display();
  }

  //lastLogMillis = millis();
  intervalStartMillis = millis();
  lastLogMillis = intervalStartMillis;
  lastLogTimeMs = 0;
}

// ================= LOOP =================
void loop() {
  unsigned long nowMillis = millis();

  switch (currentState) {

    // -------- LOGGING --------
    case STATE_LOGGING: {
      handleLogging(nowMillis);

      if (digitalRead(BTN_SAVE) == LOW) {
        if (!btnSaveHolding) {
          btnSaveHolding = true;
          btnSaveHoldStart = nowMillis;
        }

        // Long press > 3 detik → EJECT
        if (nowMillis - btnSaveHoldStart > 5000) {
          currentState = STATE_EJECT;
          handleEject();
        }
      } else {
        if (btnSaveHolding && (nowMillis - btnSaveHoldStart < 5000)) {
          // Short press → menu setting
          currentState = STATE_DISPLAY_SETTING;
          lastMenuActivity = nowMillis;
          showSettingScreen();
        }
        btnSaveHolding = false;
      }
    } break;

    // -------- MENU / SETTING --------
    case STATE_DISPLAY_SETTING: {

      unsigned long now = millis();

      // ---- UP ----
      if (digitalRead(BTN_UP) == LOW) {
        if (!btnUpHolding) {
          btnUpHolding = true;
          btnHoldStartUp = now;
        }
        logInterval += getStep(now - btnHoldStartUp);
        logInterval = constrain(logInterval, MIN_INTERVAL, MAX_INTERVAL);

        lastMenuActivity = now;
        showSettingScreen();
        delay(150);
      } else {
        btnUpHolding = false;
      }

      // ---- DOWN ----
      if (digitalRead(BTN_DOWN) == LOW) {
        if (!btnDownHolding) {
          btnDownHolding = true;
          btnHoldStartDown = now;
        }
        unsigned long step = getStep(now - btnHoldStartDown);
        if (logInterval > step) logInterval -= step;
        else logInterval = MIN_INTERVAL;

        logInterval = constrain(logInterval, MIN_INTERVAL, MAX_INTERVAL);

        lastMenuActivity = now;
        showSettingScreen();
        delay(150);
      } else {
        btnDownHolding = false;
      }

      // ---- SAVE ----
      if (buttonPressed(BTN_SAVE)) {
        currentState = STATE_SAVE_SETTING;
      }

      // ---- TIMEOUT ----
      if (now - lastMenuActivity > MENU_TIMEOUT) {
        showSavedScreen();
        delay(800);
        currentState = STATE_LOGGING;
      }
    } break;

    // -------- SAVE --------
    case STATE_SAVE_SETTING:{
      prefs.putInt("interval_s", logInterval);
      
      // ---- MARKER + RESET BASIS WAKTU ----
      DateTime now = rtc.now();
      makeLogFilename(now);
      writeIntervalMarker(logInterval);

      intervalStartMillis = millis();
      lastLogMillis = intervalStartMillis;
      lastLogTimeMs = 0;   // reset delta_t supaya bersih
      
      showSavedScreen();
      delay(800);
      currentState = STATE_LOGGING;
    } break;

    case STATE_EJECT:
      // Stay here until power off
      delay(100);
      break;
  }
}