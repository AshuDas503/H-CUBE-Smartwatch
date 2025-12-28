#include <Wire.h>
#include <RTClib.h>
#include <LedDisplay.h>
#include "SparkFun_BMI270_Arduino_Library.h"
#include <TinyGPSPlus.h>
#include <Preferences.h>
#include <SparkFun_MAX1704x_Fuel_Gauge_Arduino_Library.h>

// -------------------------
// HCMS-2971 connections
// -------------------------
#define dataPin        14
#define registerSelect 13
#define clockPin       12
#define enable         11
#define reset          10
#define displayLength  8
LedDisplay myDisplay = LedDisplay(dataPin, registerSelect, clockPin, enable, reset, displayLength);

// -------------------------
// RTC & IMU setup
// -------------------------
RTC_DS3231 rtc;
BMI270 imu;
uint8_t bmiAddress = BMI2_I2C_SEC_ADDR; // 0x69

// -------------------------
// GPS setup
// -------------------------
TinyGPSPlus gps;
#define GPS_RX 16
#define GPS_TX 17
#define gpsSerial Serial2

// -------------------------
// RGB LED (PWM outputs)
// -------------------------
#define LED_R 38
#define LED_G 39
#define LED_B 40
#define LEDC_FREQ 1000
#define LEDC_RES 8

int rgbR = 255, rgbG = 70, rgbB = 0; // default amber

// -------------------------
// Buzzer (Alarm output)
// -------------------------
#define BUZZER_PIN 41

// -------------------------
// Buttons
// -------------------------
#define BTN_UP     5
#define BTN_DOWN   6
#define BTN_CENTER 4

// -------------------------
// Preferences (Flash storage)
// -------------------------
Preferences prefs;

// -------------------------
// Battery Fuel Gauge (MAX17048)
// -------------------------
SFE_MAX1704X lipo(MAX1704X_MAX17048);
float lastVoltage = 0;
float lastSOC = 0;
bool isCharging = false;
unsigned long lastBatteryCheck = 0;
bool showBatteryMode = false;
bool chargingDisplayActive = false;
unsigned long lastChargeCheck = 0;

// -------------------------
// State variables
// -------------------------
bool inMenu = false;
bool brightnessSettingMode = false;
bool showDateMode = false;
bool showStepsMode = false;
bool showGPSMode = false;
bool showTempMode = false;
bool rgbMode = false;
bool stopwatchMode = false;
bool alarmMode = false;
bool alarmActive = false;
bool justEnteredMenu = false;

int menuIndex = 0;
const int totalMenus = 11; // includes Battery + Timer

// Timing
unsigned long lastInteraction = 0;
unsigned long pressStartTime = 0;
bool longPressTriggered = false;

const unsigned long holdTime = 5000;
const unsigned long sleepTimeout = 10000;

// Stopwatch
unsigned long stopwatchStart = 0;
unsigned long stopwatchElapsed = 0;
bool stopwatchRunning = false;

// Alarm
int alarmHour = 7;
int alarmMinute = 0;
bool alarmSet = false;
int alarmField = 0;
unsigned long lastBeep = 0;
bool beepState = false;

// Brightness
int currentBrightness = 15;

// Splash flag
bool showSplashNext = false;

// -------------------------
// Timer variables (NEW)
// -------------------------
bool timerMode = false;
bool timerRunning = false;
bool timerFinished = false;
unsigned long timerStartMillis = 0;
unsigned long timerDurationMillis = 0; // total countdown in ms (remaining when paused)
int timerSetMinutes = 0;
int timerSetSeconds = 30;
int timerSelectField = 0; // 0 = minutes, 1 = seconds
unsigned long timerLastUpdate = 0;

// -------------------------
// Helper
// -------------------------
void printDisplay(const char* str) {
  char buf[9];
  snprintf(buf, 9, "%-8s", str);
  myDisplay.home();
  myDisplay.print(buf);
}

void timerSavePrefs() {
  prefs.begin("watchprefs", false);
  prefs.putInt("timerMin", timerSetMinutes);
  prefs.putInt("timerSec", timerSetSeconds);
  prefs.end();
}

// -------------------------
// Setup
// -------------------------
void setup() {
  Serial.begin(115200);
  Wire.begin(8, 9);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_CENTER, INPUT_PULLUP);

  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC!");
    while (1);
  }

  //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
 
  myDisplay.begin();
  myDisplay.setBrightness(currentBrightness);
  myDisplay.clear();

  while (imu.beginI2C(bmiAddress) != BMI2_OK) {
    Serial.println("BMI270 not connected!");
    delay(1000);
  }
  imu.enableFeature(BMI2_STEP_COUNTER);

  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);

  // --- Battery gauge init ---
  if (lipo.begin()) {
    lipo.quickStart();
    lastVoltage = lipo.getVoltage();
    lastSOC = lipo.getSOC();
    lastBatteryCheck = millis();
    Serial.println("MAX17048 OK");
  } else {
    Serial.println("MAX17048 not detected!");
  }

  ledcAttach(LED_R, LEDC_FREQ, LEDC_RES);
  ledcAttach(LED_G, LEDC_FREQ, LEDC_RES);
  ledcAttach(LED_B, LEDC_FREQ, LEDC_RES);

  prefs.begin("watchprefs", false);
  rgbR = prefs.getInt("rgbR", 255);
  rgbG = prefs.getInt("rgbG", 70);
  rgbB = prefs.getInt("rgbB", 0);
  alarmHour = prefs.getInt("alarmH", 7);
  alarmMinute = prefs.getInt("alarmM", 0);
  alarmSet = prefs.getBool("alarmOn", false);
  // load timer prefs
  timerSetMinutes = prefs.getInt("timerMin", 0);
  timerSetSeconds = prefs.getInt("timerSec", 30);
  prefs.end();

  timerDurationMillis = (unsigned long)timerSetMinutes * 60000UL + (unsigned long)timerSetSeconds * 1000UL;

  ledcWrite(LED_R, rgbR);
  ledcWrite(LED_G, rgbG);
  ledcWrite(LED_B, rgbB);

  Serial.println("System Ready");
}

// -------------------------
// Loop
// -------------------------
void loop() {
  unsigned long now = millis();
  handleButtons(now);

  while (gpsSerial.available() > 0)
    gps.encode(gpsSerial.read());

  // Alarm trigger
  if (alarmSet && !alarmActive) {
    DateTime nowt = rtc.now();
    if (nowt.hour() == alarmHour && nowt.minute() == alarmMinute && nowt.second() == 0) {
      alarmActive = true;
      lastBeep = millis();
    }
  }
  if (alarmActive) handleAlarmBeep();

// Timer finish buzzer handling (if timer finished and still in timerMode)
if (timerFinished) {
  int notes[] = {523, 587, 659, 698, 784, 880, 988, 1047};
  int count = sizeof(notes) / sizeof(notes[0]);
  bool stoppedByButton = false;

  for (int i = 0; i < count; i++) {
    tone(BUZZER_PIN, notes[i], 500);
    delay(650);
    noTone(BUZZER_PIN);

    // If user presses center, stop melody and consume the press
    if (digitalRead(BTN_CENTER) == LOW) {
      noTone(BUZZER_PIN);
      timerFinished = false;
      timerRunning = false;
      timerMode = false;           // <-- important: exit timer mode so it won't restart
      digitalWrite(BUZZER_PIN, LOW);
      myDisplay.clear();
      lastInteraction = millis();  // keep UI awake briefly
      stoppedByButton = true;
      // small debounce delay to avoid immediate re-detection by button polling
      delay(120);
      break;
    }
  }

  // If melody ended without button, still keep timerMode=false so user must re-enter timer menu
  if (!stoppedByButton) {
    timerMode = false;           // require user to explicitly re-enter timer to restart
    lastInteraction = millis();
  }

  printDisplay("00:00:00");
}


  // Modes
  if (alarmMode) { handleAlarmMode(); return; }
  if (rgbMode) { handleRGBMode(); return; }
  if (stopwatchMode) { showStopwatch(); return; }
  if (timerMode) { showTimer(); return; }   // Timer display & logic
  if (showBatteryMode) { showBattery(); return; }

  if (showTempMode) {
    static unsigned long lastTempUpdate = 0;
    if (millis() - lastTempUpdate > 1000) {
      showTemp();
      lastTempUpdate = millis();
    }
    if (digitalRead(BTN_CENTER) == LOW) {
      delay(150);
      showTempMode = false;
      myDisplay.clear();
    }
    return;
  }

  if (showDateMode) { showDate(); if (now - lastInteraction > 5000) { showDateMode = false; myDisplay.clear(); } return; }
  if (showStepsMode) { showSteps(); if (now - lastInteraction > 5000) { showStepsMode = false; myDisplay.clear(); } return; }
  if (showGPSMode) { showGPS(); if (now - lastInteraction > 5000) { showGPSMode = false; myDisplay.clear(); } return; }

  if (!inMenu && !brightnessSettingMode) {
    if (millis() - lastInteraction < sleepTimeout) showTime();
    else myDisplay.clear();
  } else showMenu();
}

// -------------------------
// Displays
// -------------------------
void showTime() {
  if (showSplashNext) {
    myDisplay.clear();
    delay(50);
    myDisplay.home();
    myDisplay.print(" H-CUBE ");
    delay(1200);
    myDisplay.clear();
    showSplashNext = false;
  }

  DateTime now = rtc.now();
  char buffer[9];
  snprintf(buffer, 9, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
  printDisplay(buffer);
}
void showDate() {
  DateTime now = rtc.now();
  char buffer[9];

  // Step 1: Show Date
  snprintf(buffer, 9, "%02d/%02d/%02d", now.day(), now.month(), now.year() % 100);
  printDisplay(buffer);
  delay(2000);

  // Step 2: Show Day
  const char* daysOfWeek[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  int dayIndex = now.dayOfTheWeek(); // from RTClib
  snprintf(buffer, 9, "%-8s", daysOfWeek[dayIndex]);
  printDisplay(buffer);
  delay(2000);

  // Step 3: Clear & exit
  showDateMode = false;
  myDisplay.clear();
}

void showSteps() {
  uint32_t stepCount = 0;
  imu.getStepCount(&stepCount);
  char buffer[9];
  snprintf(buffer, 9, "%8lu", stepCount);
  printDisplay(buffer);
}
void showTemp() {
  float tempC = rtc.getTemperature() - 1.0;
  char buf[9];
  snprintf(buf, 9, "TMP:%02.1fC", tempC);
  printDisplay(buf);
}
void showGPS() {
  if (gps.location.isValid()) {
    char latStr[9], lonStr[9];
    snprintf(latStr, 9, "LT:%05.2f", gps.location.lat());
    snprintf(lonStr, 9, "LO:%05.2f", gps.location.lng());
    printDisplay(latStr);
    delay(2000);
    printDisplay(lonStr);
    delay(2000);
  } else {
    printDisplay("NO GPS");
    delay(2000);
  }
  showGPSMode = false;
}

// -------------------------
// Timer display & logic
// -------------------------
void showTimer() {
  unsigned long now = millis();

  // If finished -> show 00:00 (handled in loop buzzer)
  if (timerFinished) {
    printDisplay("00:00:00");
    return;
  }

  if (!timerRunning) {
    // Setting mode display: show "SETMM:SS", selected field blinks OFF
    static unsigned long blinkT = 0;
    bool blinkOn = ((now / 400) & 1) == 0; // blink 400ms

    char buf[9];
    int displayMin = timerSetMinutes;
    int displaySec = timerSetSeconds;

    if (timerSelectField == 0 && !blinkOn) displayMin = -1; // use -1 to indicate blank
    if (timerSelectField == 1 && !blinkOn) displaySec = -1;

    if (displayMin == -1) {
      // blank minutes during blink OFF
      snprintf(buf, 9, "SET  :%02d", displaySec);
    } else if (displaySec == -1) {
      snprintf(buf, 9, "SET%02d:  ", displayMin);
    } else {
      snprintf(buf, 9, "SET%02d:%02d", displayMin, displaySec);
    }
    printDisplay(buf);
  } else {
    // timer running -> show remaining time (update fast enough)
    unsigned long elapsed = millis() - timerStartMillis;
    if (elapsed >= timerDurationMillis) {
      // finished
      timerRunning = false;
      timerFinished = true;
      printDisplay("00:00:00");
      return;
    } else {
      unsigned long remaining = timerDurationMillis - elapsed;
      unsigned int mins = (remaining / 60000UL) % 60;
      unsigned int secs = (remaining / 1000UL) % 60;
      unsigned int hund = (remaining % 1000UL) / 10;
      char buf[9];
      snprintf(buf, 9, "%02d:%02d:%02d", mins, secs, hund);
      printDisplay(buf);
    }
  }
}

// -------------------------
// Stopwatch
// -------------------------
void showStopwatch() {
  static unsigned long lastUpdate = 0;
  unsigned long now = millis();
  if (now - lastUpdate >= 100) {
    unsigned long elapsed = stopwatchRunning ? millis() - stopwatchStart + stopwatchElapsed : stopwatchElapsed;
    unsigned int minutes = (elapsed / 60000) % 60;
    unsigned int seconds = (elapsed / 1000) % 60;
    unsigned int hundredths = (elapsed % 1000) / 10;
    char buf[9];
    snprintf(buf, 9, "%02d:%02d:%02d", minutes, seconds, hundredths);
    printDisplay(buf);
    lastUpdate = now;
  }
  static bool centerPrev = HIGH;
  bool center = digitalRead(BTN_CENTER);
  if (centerPrev == HIGH && center == LOW) {
    unsigned long pressStart = millis();
    while (digitalRead(BTN_CENTER) == LOW) delay(10);
    unsigned long pressDuration = millis() - pressStart;
    if (pressDuration < 800) {
      if (!stopwatchRunning) { stopwatchRunning = true; stopwatchStart = millis(); }
      else { stopwatchRunning = false; stopwatchElapsed += millis() - stopwatchStart; }
    } else {
      stopwatchRunning = false;
      stopwatchElapsed = 0;
      stopwatchMode = false;
      myDisplay.clear();
    }
  }
  centerPrev = center;
}

// -------------------------
//  Battery Menu
// -------------------------
void showBattery() {
  float voltage = lipo.getVoltage();
  float soc = lipo.getSOC();
  char buf[9];
  snprintf(buf, 9, "BAT:%.2f", voltage);
  printDisplay(buf);
  delay(2000);
  snprintf(buf, 9, "SOC:%02d%%", (int)soc);
  printDisplay(buf);
  delay(2000);
  showBatteryMode = false;
  myDisplay.clear();
}

// -------------------------
// RGB Mode
// -------------------------
void handleRGBMode() {
  static int colorIndex = 0;
  int *color = (colorIndex == 0 ? &rgbR : colorIndex == 1 ? &rgbG : &rgbB);
  const char *label = (colorIndex == 0 ? "RED" : colorIndex == 1 ? "GRN" : "BLU");
  static bool upPrev = HIGH, downPrev = HIGH, centerPrev = HIGH;
  static unsigned long lastChangeTime = 0;
  static unsigned long holdStartUp = 0, holdStartDown = 0;
  const unsigned long slowDelay = 150, fastDelay = 40;
  bool up = digitalRead(BTN_UP), down = digitalRead(BTN_DOWN), center = digitalRead(BTN_CENTER);
  unsigned long now = millis();
  if (up == LOW) {
    if (holdStartUp == 0) holdStartUp = now;
    unsigned long delayVal = map(constrain(now - holdStartUp, 0, 3000), 0, 3000, slowDelay, fastDelay);
    if (now - lastChangeTime > delayVal) { *color = min(255, *color + 1); lastChangeTime = now; }
  } else holdStartUp = 0;
  if (down == LOW) {
    if (holdStartDown == 0) holdStartDown = now;
    unsigned long delayVal = map(constrain(now - holdStartDown, 0, 3000), 0, 3000, slowDelay, fastDelay);
    if (now - lastChangeTime > delayVal) { *color = max(0, *color - 1); lastChangeTime = now; }
  } else holdStartDown = 0;
  ledcWrite(LED_R, rgbR);
  ledcWrite(LED_G, rgbG);
  ledcWrite(LED_B, rgbB);
  char buf[9]; snprintf(buf, 9, "%s:%03d", label, *color); printDisplay(buf);
  if (centerPrev == HIGH && center == LOW) {
    colorIndex++;
    if (colorIndex > 2) {
      prefs.begin("watchprefs", false);
      prefs.putInt("rgbR", rgbR);
      prefs.putInt("rgbG", rgbG);
      prefs.putInt("rgbB", rgbB);
      prefs.end();
      rgbMode = false;
      colorIndex = 0;
      myDisplay.clear();
      return;
    }
    delay(200);
  }
  upPrev = up; downPrev = down; centerPrev = center;
}

// -------------------------
// Alarm Logic
// -------------------------
void handleAlarmMode() {
  static bool upPrev = HIGH, downPrev = HIGH, centerPrev = HIGH;
  bool up = digitalRead(BTN_UP), down = digitalRead(BTN_DOWN), center = digitalRead(BTN_CENTER);
  if (upPrev == HIGH && up == LOW) {
    if (alarmField == 0) alarmHour = (alarmHour + 1) % 24;
    else alarmMinute = (alarmMinute + 1) % 60;
  }
  if (downPrev == HIGH && down == LOW) {
    if (alarmField == 0) alarmHour = (alarmHour + 23) % 24;
    else alarmMinute = (alarmMinute + 59) % 60;
  }
  char buf[9]; snprintf(buf, 9, "%02d:%02d", alarmHour, alarmMinute); printDisplay(buf);
  if (centerPrev == HIGH && center == LOW) {
    alarmField++;
    if (alarmField > 1) {
      alarmField = 0;
      alarmMode = false;
      alarmSet = true;
      prefs.begin("watchprefs", false);
      prefs.putInt("alarmH", alarmHour);
      prefs.putInt("alarmM", alarmMinute);
      prefs.putBool("alarmOn", true);
      prefs.end();
      myDisplay.clear();
    }
  }
  upPrev = up; downPrev = down; centerPrev = center;
}

void handleAlarmBeep() {
  // melody notes (example scale)
  int notes[] = {523, 587, 659, 698, 784, 880, 988, 1047};
  int count = sizeof(notes) / sizeof(notes[0]);

  for (int i = 0; i < count; i++) {
    tone(BUZZER_PIN, notes[i], 500);
    delay(650);
    noTone(BUZZER_PIN);

    // allow stopping alarm by button press
    if (digitalRead(BTN_UP) == LOW ||
        digitalRead(BTN_DOWN) == LOW ||
        digitalRead(BTN_CENTER) == LOW) {
      noTone(BUZZER_PIN);
      alarmActive = false;
      return;
    }
  }
}


// -------------------------
// Buttons + Menu (includes Timer handling)
// -------------------------
void handleButtons(unsigned long now) {
  // Timer mode override: handle timer-specific button logic first
  if (timerMode) {
    static bool upPrev = HIGH, downPrev = HIGH, centerPrev = HIGH;
    bool up = digitalRead(BTN_UP), down = digitalRead(BTN_DOWN), center = digitalRead(BTN_CENTER);

    // UP/DOWN adjust selected field when not running and not finished
    if (!timerRunning && !timerFinished) {
      if (upPrev == HIGH && up == LOW) {
        if (timerSelectField == 0) timerSetMinutes = min(59, timerSetMinutes + 1);
        else timerSetSeconds = min(59, timerSetSeconds + 1);
        timerDurationMillis = (unsigned long)timerSetMinutes * 60000UL + (unsigned long)timerSetSeconds * 1000UL;
        timerSavePrefs();
      }
      if (downPrev == HIGH && down == LOW) {
        if (timerSelectField == 0) timerSetMinutes = max(0, timerSetMinutes - 1);
        else timerSetSeconds = max(0, timerSetSeconds - 1);
        timerDurationMillis = (unsigned long)timerSetMinutes * 60000UL + (unsigned long)timerSetSeconds * 1000UL;
        timerSavePrefs();
      }
    }

    // CENTER: short press toggles start/pause; long press (>=1000ms) stops & exit; while setting, center cycles field on quick release
    static unsigned long centerDownAt = 0;
    if (centerPrev == HIGH && center == LOW) centerDownAt = now;

    if (centerPrev == LOW && center == HIGH) {
      unsigned long held = now - centerDownAt;
      if (held < 1000) { // short
        if (!timerFinished) {
          if (!timerRunning) {
            // start: set the start time reference and keep timerDurationMillis as total duration
            timerRunning = true;
            timerStartMillis = millis();
          } else {
            // pause: compute remaining duration and store it in timerDurationMillis
            unsigned long elapsed = millis() - timerStartMillis;
            if (elapsed < timerDurationMillis) timerDurationMillis -= elapsed;
            else timerDurationMillis = 0;
            timerRunning = false;
          }
        } else {
          // finished: stop buzzer, clear done, exit timer
          timerFinished = false;
          timerMode = false;
          digitalWrite(BUZZER_PIN, LOW);
          myDisplay.clear();
        }
      } else { // long press -> reset & exit
        timerRunning = false;
        timerFinished = false;
        timerMode = false;
        // reload saved values
        prefs.begin("watchprefs", false);
        timerSetMinutes = prefs.getInt("timerMin", 0);
        timerSetSeconds = prefs.getInt("timerSec", 30);
        prefs.end();
        timerDurationMillis = (unsigned long)timerSetMinutes * 60000UL + (unsigned long)timerSetSeconds * 1000UL;
        myDisplay.clear();
      }
    }

    // cycle selected field on short tap of center while not running (we detect quick tap by checking rising edge and held < 300ms)
    // To avoid interfering with start, we instead detect a quick press-release where we didn't start the timer.
    static unsigned long lastCenterReleased = 0;
    if (centerPrev == LOW && center == HIGH) {
      unsigned long held = now - centerDownAt;
      if (held < 300 && !timerRunning && !timerFinished) {
        // cycle field
        timerSelectField = (timerSelectField + 1) % 2;
      }
      lastCenterReleased = now;
    }

    upPrev = up;
    downPrev = down;
    centerPrev = center;
    if (up == LOW || down == LOW || center == LOW) lastInteraction = now;
    return;
  }

  // Non-timer path: keep original behavior but retain timer check earlier
  if (stopwatchMode) {
    if (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW) lastInteraction = now;
    return;
  }

  static bool upPrev = HIGH, downPrev = HIGH, centerPrev = HIGH;
  bool up = digitalRead(BTN_UP), down = digitalRead(BTN_DOWN), center = digitalRead(BTN_CENTER);
  if (centerPrev == HIGH && center == LOW) { pressStartTime = now; longPressTriggered = false; }
  if (centerPrev == LOW && center == LOW && (now - pressStartTime) > holdTime && !longPressTriggered && !inMenu) {
    inMenu = true; menuIndex = 0; longPressTriggered = true; justEnteredMenu = true; myDisplay.clear(); delay(200);
  }
  if (centerPrev == LOW && center == HIGH) {
    if (justEnteredMenu) justEnteredMenu = false;
    else if (brightnessSettingMode) brightnessSettingMode = false, inMenu = false;
    else if (inMenu && !longPressTriggered) { selectMenu(); showSplashNext = true; }
  }
  if (brightnessSettingMode) {
    if (upPrev == HIGH && up == LOW && currentBrightness < 15) myDisplay.setBrightness(++currentBrightness);
    if (downPrev == HIGH && down == LOW && currentBrightness > 0) myDisplay.setBrightness(--currentBrightness);
  } else if (inMenu) {
    if (upPrev == HIGH && up == LOW) menuIndex = (menuIndex - 1 + totalMenus) % totalMenus;
    if (downPrev == HIGH && down == LOW) menuIndex = (menuIndex + 1) % totalMenus;
  }
  upPrev = up; downPrev = down; centerPrev = center;
  if (up == LOW || down == LOW || center == LOW) lastInteraction = now;
}

// -------------------------
// Menu
// -------------------------
void showMenu() {
  const char *menus[11] = {"Date","Steps","Bright","GPS","RGB","StopWtch","Alarm","Temp","Battery","Timer","Back"};
  if (brightnessSettingMode) { char buf[9]; snprintf(buf, 9, "BR:%02d", currentBrightness); printDisplay(buf); }
  else printDisplay(menus[menuIndex]);
}

void selectMenu() {
  switch (menuIndex) {
    case 0: inMenu=false; showDateMode=true; break;
    case 1: inMenu=false; showStepsMode=true; break;
    case 2: brightnessSettingMode=true; break;
    case 3: inMenu=false; showGPSMode=true; break;
    case 4: inMenu=false; rgbMode=true; break;
    case 5: inMenu=false; stopwatchMode=true; stopwatchRunning=false; stopwatchElapsed=0; break;
    case 6: inMenu=false; alarmMode=true; break;
    case 7: inMenu=false; showTempMode=true; break;
    case 8: inMenu=false; showBatteryMode=true; break;
    case 9: inMenu=false; timerMode=true; timerRunning=false; timerFinished=false; timerSelectField=0; timerDurationMillis = (unsigned long)timerSetMinutes * 60000UL + (unsigned long)timerSetSeconds * 1000UL; break;
    case 10: inMenu=false; break;
  }
  myDisplay.clear();
}
