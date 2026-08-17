#include "orangeos.h"

// ===================== MAIN / GLOBALS =====================
LiquidCrystal_I2C lcd(0x27, 16, 2);
RTC_DS3231 rtc;

State currentState = MAIN;
State prevState = MAIN;
bool soundEnabled = true;

uint8_t selectedFile = 0;
uint8_t currentDisk = 0;
uint8_t currentFileIdx = 255;
uint8_t renameTarget = 255;
uint8_t taskmanIndex = 0;
uint16_t currentBlock = 0;
uint8_t currentByte = 0;
uint8_t block[BLOCK_SIZE];
bool displayNeedsFullRedraw = true;
uint8_t currentCharIndex = 0;

int8_t vars8[32];
uint16_t vars16[8];
uint32_t vars32[16];
uint16_t pc;
bool oefRunning = false;
bool oefPaused = false;
unsigned long oefDelayUntil = 0;

char renameBuffer[FULLNAME_LEN + 1];
uint8_t renamePos = 0;
bool renameJustEntered = false;

uint8_t contextMenuIndex = 0;
const char* contextItems[] = {"Settings", "Info"};
const uint8_t contextCount = 2;

const char* greetings[] = {"Welcome!", "Home Sweet Home", "Hello User", "OrangeOS Ready", "Good Day!"};
const uint8_t greetCount = 5;
uint8_t currentGreeting = 0;

const char* settingsItems[] = {"Sound", "Set Time", "Erase Data", "System Info", "Back"};
uint8_t settingsIndex = 0;

uint8_t currentPrivilege = 0;
uint16_t viewerOffset = 0;

bool sleeping = false;
unsigned long lastActivity = 0;
bool pcConnected = false;
uint8_t execFileIdx = 0;

void setup() {
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  Wire.begin();
  Serial.begin(CONN_BAUD);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Booting..."));
  lcd.display();
  delay(300);
  randomSeed(analogRead(A0));

  if (!rtc.begin()) {
    lcd.setCursor(0,0); lcd.print(F("RTC error")); while (1);
  }
  if (rtc.lostPower()) {
    DateTime now = rtc.now();
    if (now.year() < 2020) rtc.adjust(DateTime(2024, 1, 1, 0, 0, 0));
  }

  currentDisk = 0;
  loadSettings();
  if (getFileCount() == 0) {
    createBuiltinFiles();
  }
  currentDisk = 1;
  if (getFileCount() == 0) initDiskLayout();

  currentState = MAIN;
  currentGreeting = random(greetCount);
  lastActivity = millis();
  displayNeedsFullRedraw = true;
}

void loop() {
  static uint8_t peakMask = 0;
  static unsigned long pressStart = 0;
  static bool longHandled = false;
  static bool veryLongHandled = false;
  static bool exitHandled = false;
  static bool active = false;
  static bool wakeConsume = false;

  uint8_t mask = getButtonMask();

  connectionPoll();

  if (sleeping) {
    if (mask != 0) { wakeFromSleep(); wakeConsume = true; }
    return;
  }
  if (wakeConsume) {
    if (mask == 0) { wakeConsume = false; active = false; peakMask = 0; }
    return;
  }
  if (mask != 0) lastActivity = millis();
  if (!oefRunning && !pcConnected && (millis() - lastActivity > SLEEP_TIMEOUT_MS)) enterSleep();
  if (pcConnected && (millis() - lastActivity > 10000)) pcConnected = false;

  if (oefRunning) {
    if (mask == 7) {
      oefRunning = false; oefPaused = false;
      currentState = MAIN; displayNeedsFullRedraw = true;
      active = false; return;
    }
    if (oefPaused) {
      static bool pausedActive = false;
      static unsigned long pausedStart = 0;
      if (!pausedActive && mask != 0) { pausedActive = true; pausedStart = millis(); }
      if (pausedActive && mask == 0) {
        if (millis() - pausedStart > DEBOUNCE_MS) {
          oefPaused = false; oefDelayUntil = millis();
        }
        pausedActive = false;
      }
      return;
    }
    if (millis() >= oefDelayUntil) executeOneInstruction();
    return;
  }

  if (!active && mask != 0) {
    active = true; pressStart = millis(); peakMask = mask;
    longHandled = false; veryLongHandled = false; exitHandled = false;
  }
  if (active && mask != 0) {
    if (mask > peakMask) peakMask = mask;
    unsigned long dur = millis() - pressStart;
    if (dur > DEBOUNCE_MS) {
      if (!exitHandled && dur >= EXIT_MS) { exitHandled = true; handleExit(peakMask); }
      else if (!veryLongHandled && dur >= VERY_LONG_MS) { veryLongHandled = true; handleVeryLongAction(peakMask); }
    }
  }
  if (active && mask == 0) {
    if (millis() - pressStart > DEBOUNCE_MS) {
      if (!exitHandled && !veryLongHandled) handleShort(peakMask);
    }
    active = false; peakMask = 0; longHandled = false; veryLongHandled = false; exitHandled = false;
    renameJustEntered = false;
  }

  if (displayNeedsFullRedraw) { redrawFullScreen(); displayNeedsFullRedraw = false; prevState = currentState; }
  else if (currentState != prevState) { redrawFullScreen(); prevState = currentState; }

  if (currentState == MAIN || currentState == CONTEXT_MENU || currentState == INFO_SCREEN) {
    static unsigned long lastTimeUpdate = 0;
    if (millis() - lastTimeUpdate >= 1000) {
      if (currentState == MAIN) updateClockDisplay();
      lastTimeUpdate = millis();
    }
  }
}
