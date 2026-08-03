#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>

#define BTN1 2
#define BTN2 3
#define BTN3 4
#define BUZZER 5
#define EEPROM_ADDR 0x57
#define EEPROM_SIZE 4096
#define FILE_TABLE_START 0
#define MAX_FILES 16
#define NAME_LEN 8
#define EXT_LEN 3
#define FULLNAME_LEN 13
#define BLOCK_SIZE 8

#define FILE_TABLE_SIZE (MAX_FILES * (FULLNAME_LEN + 5))
#define DATA_START_BLOCK ((FILE_TABLE_SIZE + BLOCK_SIZE - 1) / BLOCK_SIZE)

LiquidCrystal_I2C lcd(0x27, 16, 2);
RTC_DS3231 rtc;

enum State { MAIN, DISK, EDIT, VIEWER, PROGRAMS, SETTINGS, PLAYER, RENAME_ST, RUN_OEF, CONTEXT_MENU, INFO_SCREEN };
State currentState = MAIN;
State prevState = MAIN;

const unsigned long LONG_MS = 800;
const unsigned long VERY_LONG_MS = 1500;
const unsigned long EXIT_MS = 2000;
const unsigned long DEBOUNCE_MS = 30;
bool soundEnabled = true;

enum Extension { EXT_TXT, EXT_OEF, EXT_OMF, EXT_UNKNOWN };

struct FileEntry {
  char name[FULLNAME_LEN + 1];
  uint16_t startBlock;
  uint8_t sizeBlocks;
  uint8_t flags;
};

FileEntry files[MAX_FILES];
uint8_t fileCount = 0;
uint8_t selectedFile = 0;

uint8_t block[BLOCK_SIZE];
uint16_t currentBlock = 0;
uint8_t currentByte = 0;
uint8_t currentFileIdx = 255;

bool displayNeedsFullRedraw = true;
uint8_t currentCharIndex = 0;

int8_t vars[32];
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

void handleShort(uint8_t mask);
void handleVeryLongAction(uint8_t mask);
void handleExit(uint8_t mask);
void playToneForMask(uint8_t mask);
void redrawFullScreen();
void drawMainScreenFull();
void updateClockDisplay();
void drawDiskFull();
void drawEditScreenFull();
void drawSettingsFull();
void drawProgramsFull();
void drawViewerFull();
void drawPlayerFull();
void drawRenameFull();
void drawRunOefFull();
void drawContextMenuFull();
void drawInfoScreenFull();
void openFile();
void editFile();
void fileInfo();
void createFile();
void deleteFile();
void buildProgram();
void eraseDisk();
void viewerPrevPage();
void viewerNextPage();
void viewerPrevLine();
void viewerNextLine();
void startOEF();
void executeOneInstruction();
void playOMF();
int noteToFreq(uint8_t note);
char getCharAt(uint16_t index);
void changeCurrentChar(int dir);
Extension getExtension(const char* fullname);
Extension getCurrentExtension();
char* getFileName(uint8_t idx);
uint16_t findFreeBlock();
void loadFileTable();
void saveFileTable();
void createDefaultFile();
void createHelloOef();
void createHiOef();
void loadBlock(uint16_t blockIdx);
void saveBlock(uint16_t blockIdx);
void writeEEPROM(uint16_t addr, uint8_t data);
uint8_t readEEPROM(uint16_t addr);
char nextValidChar(char c, uint8_t pos);
char prevValidChar(char c, uint8_t pos);
bool isValidChar(char c, uint8_t pos);
void chooseExtensionDialog(char ext[4]);
uint8_t getButtonMask();
void renameBackspace();
void renameDelete();
void renameInsert();
void cyclicMove(uint8_t &pos, uint8_t max, bool forward);

int strcasecmp_local(const char* a, const char* b) {
  while (*a && *b) {
    char ca = *a >= 'a' && *a <= 'z' ? *a - 32 : *a;
    char cb = *b >= 'a' && *b <= 'z' ? *b - 32 : *b;
    if (ca != cb) return (ca - cb);
    a++; b++;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

Extension getExtension(const char* fullname) {
  const char* dot = strrchr(fullname, '.');
  if (!dot) return EXT_UNKNOWN;
  if (strcasecmp_local(dot, ".TXT") == 0) return EXT_TXT;
  if (strcasecmp_local(dot, ".OEF") == 0) return EXT_OEF;
  if (strcasecmp_local(dot, ".OMF") == 0) return EXT_OMF;
  return EXT_UNKNOWN;
}

Extension getCurrentExtension() {
  if (currentFileIdx == 255) return EXT_UNKNOWN;
  return getExtension(files[currentFileIdx].name);
}

char* getFileName(uint8_t idx) { return files[idx].name; }

uint8_t getButtonMask() {
  uint8_t m = 0;
  if (digitalRead(BTN1) == LOW) m |= 1;
  if (digitalRead(BTN2) == LOW) m |= 2;
  if (digitalRead(BTN3) == LOW) m |= 4;
  return m;
}

bool isValidChar(char c, uint8_t pos) {
  if (pos == 8) return (c == '.');
  if (pos < 8) return ( (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') );
  if (pos > 8) return ( (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') );
  return false;
}

char nextValidChar(char c, uint8_t pos) {
  char orig = c;
  do {
    if (c >= '0' && c < '9') c++;
    else if (c == '9') c = 'A';
    else if (c >= 'A' && c < 'Z') c++;
    else if (c == 'Z') c = 'a';
    else if (c >= 'a' && c < 'z') c++;
    else if (c == 'z') c = '0';
    else c = '0';
    if (isValidChar(c, pos)) break;
  } while (c != orig);
  return c;
}

char prevValidChar(char c, uint8_t pos) {
  char orig = c;
  do {
    if (c == '0') c = 'z';
    else if (c >= '1' && c <= '9') c--;
    else if (c == 'a') c = 'Z';
    else if (c > 'a' && c <= 'z') c--;
    else if (c == 'A') c = '9';
    else if (c > 'A' && c <= 'Z') c--;
    else c = 'z';
    if (isValidChar(c, pos)) break;
  } while (c != orig);
  return c;
}

void chooseExtensionDialog(char ext[4]) {
  static const char* exts[] = {"TXT", "OEF", "OMF"};
  uint8_t sel = 0;
  lcd.clear(); lcd.setCursor(0,0); lcd.print("Ext: ");
  lcd.setCursor(0,1); lcd.print("<     >  OK=3");
  bool done = false;
  while (!done) {
    lcd.setCursor(5,0); lcd.print(exts[sel]);
    delay(50);
    uint8_t m = getButtonMask();
    if (m == 1) { if (sel == 0) sel = 2; else sel--; delay(150); }
    else if (m == 2) { if (sel == 2) sel = 0; else sel++; delay(150); }
    else if (m == 4) { done = true; }
    if (m == 7) { sel = 0; done = true; }
  }
  strcpy(ext, exts[sel]);
}

uint16_t findFreeBlock() {
  for (uint16_t i = DATA_START_BLOCK; i < EEPROM_SIZE / BLOCK_SIZE; i++) {
    bool used = false;
    for (uint8_t j = 0; j < fileCount; j++) {
      if (i >= files[j].startBlock && i < files[j].startBlock + files[j].sizeBlocks) { used = true; break; }
    }
    if (!used) return i;
  }
  return 0xFFFF;
}

void cyclicMove(uint8_t &pos, uint8_t max, bool forward) {
  if (forward) {
    if (pos < max - 1) pos++;
    else pos = 0;
  } else {
    if (pos > 0) pos--;
    else pos = max - 1;
  }
}

void setup() {
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  lcd.init(); lcd.backlight();
  Wire.begin();
  randomSeed(analogRead(A0));

  if (!rtc.begin()) {
    lcd.setCursor(0,0); lcd.print("RTC error"); while (1);
  }
  if (rtc.lostPower()) rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  loadFileTable();
  if (fileCount == 0) {
    createDefaultFile();
    createHelloOef();
    createHiOef();
  }
  currentState = MAIN;
  currentGreeting = random(greetCount);
  displayNeedsFullRedraw = true;
}

void loop() {
  static uint8_t peakMask = 0;
  static unsigned long pressStart = 0;
  static bool longHandled = false;
  static bool veryLongHandled = false;
  static bool exitHandled = false;
  static bool active = false;

  uint8_t mask = getButtonMask();

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

void handleShort(uint8_t mask) {
  switch (currentState) {
    case MAIN:
      if (mask == 1) { currentState = DISK; selectedFile = 0; }
      else if (mask == 2) { currentState = PROGRAMS; }
      else if (mask == 4) { currentState = CONTEXT_MENU; contextMenuIndex = 0; }
      break;
    case CONTEXT_MENU:
      if (mask == 1) { if (contextMenuIndex > 0) contextMenuIndex--; else contextMenuIndex = contextCount - 1; }
      else if (mask == 2) { if (contextMenuIndex < contextCount - 1) contextMenuIndex++; else contextMenuIndex = 0; }
      else if (mask == 4) {
        if (contextMenuIndex == 0) currentState = SETTINGS;
        else if (contextMenuIndex == 1) currentState = INFO_SCREEN;
      }
      else if (mask == 3 || mask == 7) currentState = MAIN;
      break;
    case INFO_SCREEN:
      if (mask == 3 || mask == 4 || mask == 7) currentState = MAIN;
      break;
    case DISK:
      if (mask == 1) openFile();
      else if (mask == 2) editFile();
      else if (mask == 4) fileInfo();
      else if (mask == 3) createFile();
      else if (mask == 5) { if (selectedFile > 0) selectedFile--; else selectedFile = fileCount - 1; }
      else if (mask == 6) { if (selectedFile < fileCount - 1) selectedFile++; else selectedFile = 0; }
      break;
    case VIEWER:
      if (mask == 1) viewerPrevPage(); else if (mask == 2) viewerNextPage();
      else if (mask == 4) { currentState = DISK; }
      else if (mask == 5) viewerPrevLine(); else if (mask == 6) viewerNextLine();
      break;
    case EDIT:
      if (getCurrentExtension() == EXT_TXT) {
        if (mask == 1) changeCurrentChar(1);
        else if (mask == 2) changeCurrentChar(-1);
        else if (mask == 4) { saveBlock(currentBlock); currentState = DISK; }
        else if (mask == 3) { currentCharIndex++; if (currentCharIndex >= BLOCK_SIZE * files[currentFileIdx].sizeBlocks) currentCharIndex = 0; }
      } else {
        if (mask == 1) block[currentByte]++;
        else if (mask == 2) block[currentByte]--;
        else if (mask == 4) block[currentByte] = 0;
        else if (mask == 3) block[currentByte] += 0x55;
        else if (mask == 7) buildProgram();
        else if (mask == 5) { saveBlock(currentBlock); if (currentBlock > 0) currentBlock--; else currentBlock = files[currentFileIdx].sizeBlocks - 1; loadBlock(currentBlock); }
        else if (mask == 6) {
          saveBlock(currentBlock);
          if (currentBlock < files[currentFileIdx].sizeBlocks - 1) currentBlock++;
          else {
            uint16_t nextBlock = files[currentFileIdx].startBlock + files[currentFileIdx].sizeBlocks;
            bool free = true;
            for (uint8_t j = 0; j < fileCount; j++)
              if (files[j].startBlock <= nextBlock && nextBlock < files[j].startBlock + files[j].sizeBlocks) { free = false; break; }
            if (free && nextBlock < EEPROM_SIZE/BLOCK_SIZE && files[currentFileIdx].sizeBlocks < 255) {
              for (int i = 0; i < BLOCK_SIZE; i++) writeEEPROM(nextBlock * BLOCK_SIZE + i, 0x00);
              files[currentFileIdx].sizeBlocks++;
              saveFileTable();
              currentBlock = files[currentFileIdx].sizeBlocks - 1;
            }
          }
          loadBlock(currentBlock);
        }
        // циклический переход по байтам
        if (mask == 3) { cyclicMove(currentByte, BLOCK_SIZE, true); }
        else if (mask == 6) { cyclicMove(currentByte, BLOCK_SIZE, false); }
      }
      playToneForMask(mask);
      break;
    case SETTINGS:
      if (mask == 1) soundEnabled = !soundEnabled;
      else if (mask == 2) eraseDisk();
      else if (mask == 4 || mask == 7) currentState = MAIN;
      break;
    case PROGRAMS: currentState = MAIN; break;
    case PLAYER: currentState = DISK; break;
    case RENAME_ST:
      if (mask == 7) { strcpy(files[selectedFile].name, renameBuffer); saveFileTable(); currentState = DISK; renameJustEntered = false; displayNeedsFullRedraw = true; break; }
      if (mask == 3) { currentState = DISK; renameJustEntered = false; displayNeedsFullRedraw = true; break; }
      if (renameJustEntered) { renameJustEntered = false; break; }
      if (mask == 1) {
        renameBuffer[renamePos] = prevValidChar(renameBuffer[renamePos], renamePos);
      } else if (mask == 2) {
        renameBuffer[renamePos] = nextValidChar(renameBuffer[renamePos], renamePos);
      } else if (mask == 4) {
        cyclicMove(renamePos, FULLNAME_LEN, true);
      } else if (mask == 5) {
        cyclicMove(renamePos, FULLNAME_LEN, false);
      } else if (mask == 6) {
        renameDelete();
      }
      displayNeedsFullRedraw = true;
      break;
    case RUN_OEF: break;
  }
  displayNeedsFullRedraw = true;
}

void handleVeryLongAction(uint8_t mask) {
  switch (currentState) {
    case DISK:
      if (mask == 3) deleteFile();
      else if (mask == 4) { strcpy(renameBuffer, files[selectedFile].name); renamePos = 0; renameJustEntered = true; currentState = RENAME_ST; displayNeedsFullRedraw = true; }
      break;
    case EDIT:
      if (getCurrentExtension() == EXT_TXT) {
        if (mask == 3) { currentCharIndex--; if (currentCharIndex < 0) currentCharIndex = BLOCK_SIZE * files[currentFileIdx].sizeBlocks - 1; }
      } else {
        if (mask == 3) { cyclicMove(currentByte, BLOCK_SIZE, true); }
        else if (mask == 6) { cyclicMove(currentByte, BLOCK_SIZE, false); }
        displayNeedsFullRedraw = true;
      }
      playToneForMask(mask);
      break;
    case RENAME_ST:
      if (mask == 5) { renameInsert(); displayNeedsFullRedraw = true; }
      break;
  }
}

void handleExit(uint8_t mask) {
  if (mask == 7) {
    if (currentState == EDIT) { saveBlock(currentBlock); lcd.noBlink(); }
    currentState = MAIN;
    if (soundEnabled) tone(BUZZER, 500, 300);
    displayNeedsFullRedraw = true;
  }
}

void playToneForMask(uint8_t mask) {
  if (!soundEnabled) return;
  switch (mask) {
    case 1: tone(BUZZER, 1000, 60); break;
    case 2: tone(BUZZER, 1200, 60); break;
    case 4: tone(BUZZER, 800, 60); break;
    case 3: tone(BUZZER, 1400, 60); break;
    case 5: tone(BUZZER, 1600, 60); break;
    case 6: tone(BUZZER, 1800, 60); break;
    case 7: tone(BUZZER, 2000, 60); break;
  }
  delay(70);
}

void redrawFullScreen() {
  lcd.noBlink();
  switch (currentState) {
    case MAIN: drawMainScreenFull(); break;
    case DISK: drawDiskFull(); break;
    case EDIT: drawEditScreenFull(); break;
    case VIEWER: drawViewerFull(); break;
    case PROGRAMS: drawProgramsFull(); break;
    case SETTINGS: drawSettingsFull(); break;
    case PLAYER: drawPlayerFull(); break;
    case RENAME_ST: drawRenameFull(); break;
    case RUN_OEF: drawRunOefFull(); break;
    case CONTEXT_MENU: drawContextMenuFull(); break;
    case INFO_SCREEN: drawInfoScreenFull(); break;
  }
}

void drawMainScreenFull() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(greetings[currentGreeting]);
  updateClockDisplay();
}

void updateClockDisplay() {
  DateTime now = rtc.now();
  char buf[17];
  sprintf(buf, "%02d:%02d %02d/%02d/%02d", now.hour(), now.minute(), now.month(), now.day(), now.year() % 100);
  lcd.setCursor(0,1);
  lcd.print(buf);
}

void drawDiskFull() {
  lcd.clear();
  if (fileCount == 0) { lcd.setCursor(0,0); lcd.print("No files"); lcd.setCursor(0,1); lcd.print("1+2 to create"); return; }
  lcd.setCursor(0,0); lcd.print(">"); lcd.print(getFileName(selectedFile));
  lcd.setCursor(0,1);
  if (fileCount == 1) lcd.print(" (only one)");
  else { uint8_t next = (selectedFile + 1) % fileCount; lcd.print(" "); lcd.print(getFileName(next)); }
}

void drawEditScreenFull() {
  lcd.clear();
  if (getCurrentExtension() == EXT_TXT) {
    lcd.setCursor(0,0); lcd.print("TXT Edit");
    lcd.setCursor(0,1); lcd.print("Char:"); char c = getCharAt(currentCharIndex); lcd.print(c); lcd.print(" idx:"); lcd.print(currentCharIndex);
  } else {
    lcd.setCursor(0,0); char line1[17]; sprintf(line1, "B:%03d C:%01d", currentBlock, currentByte); lcd.print(line1);
    lcd.setCursor(0,1);
    for (int i = 0; i < BLOCK_SIZE; i++) { char hex[3]; sprintf(hex, "%02X", block[i]); lcd.print(hex); }
    lcd.setCursor(currentByte * 2, 1); lcd.blink();
  }
}

void drawSettingsFull() { lcd.clear(); lcd.setCursor(0,0); lcd.print("Sound: "); lcd.print(soundEnabled ? "On" : "Off"); lcd.setCursor(0,1); lcd.print("1:tgl 2:erase 3:back"); }

void drawProgramsFull() { lcd.clear(); lcd.setCursor(0,0); lcd.print("Built-in progs"); lcd.setCursor(0,1); lcd.print("(coming soon)"); }
void drawViewerFull() { lcd.clear(); lcd.setCursor(0,0); lcd.print("View:"); lcd.print(getFileName(currentFileIdx)); lcd.setCursor(0,1); lcd.print("1/2-pg 3-exit"); }
void drawPlayerFull() { lcd.clear(); lcd.setCursor(0,0); lcd.print("Playing:"); lcd.print(getFileName(currentFileIdx)); lcd.setCursor(0,1); lcd.print("any key to stop"); }

void drawRenameFull() {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("Rename:");
  lcd.setCursor(0,1); lcd.print(renameBuffer);
  lcd.setCursor(renamePos, 1); lcd.blink();
}

void drawRunOefFull() { lcd.clear(); lcd.setCursor(0,0); lcd.print("Running:"); lcd.print(getFileName(currentFileIdx)); lcd.setCursor(0,1); lcd.print("(1+2+3 to stop)"); }

void drawContextMenuFull() {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("Context Menu:");
  lcd.setCursor(0,1); lcd.print(contextItems[contextMenuIndex]);
}

void drawInfoScreenFull() {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("OrangeOS v1.00");
  uint16_t usedBlocks = 0;
  for (uint8_t i = 0; i < fileCount; i++) usedBlocks += files[i].sizeBlocks;
  uint16_t freeBytes = (EEPROM_SIZE / BLOCK_SIZE - DATA_START_BLOCK - usedBlocks) * BLOCK_SIZE;
  char buf[17];
  sprintf(buf, "Free: %d B", freeBytes);
  lcd.setCursor(0,1);
  lcd.print(buf);
}

void openFile() {
  if (fileCount == 0) return;
  currentFileIdx = selectedFile;
  Extension ext = getCurrentExtension();
  if (ext == EXT_OEF) startOEF();
  else if (ext == EXT_OMF) playOMF();
  else currentState = VIEWER;
  displayNeedsFullRedraw = true;
}
void editFile() {
  if (fileCount == 0) return;
  currentFileIdx = selectedFile;
  Extension ext = getCurrentExtension();
  if (ext == EXT_TXT) { currentBlock = 0; loadBlock(currentBlock); currentCharIndex = 0; }
  else { currentBlock = 0; loadBlock(currentBlock); currentByte = 0; }
  currentState = EDIT;
  displayNeedsFullRedraw = true;
}

void fileInfo() {
  if (fileCount == 0) return;
  lcd.clear(); lcd.setCursor(0,0); lcd.print(files[selectedFile].name);
  lcd.setCursor(0,1); char buf[9];
  uint16_t fileBytes = files[selectedFile].sizeBlocks * BLOCK_SIZE;
  sprintf(buf, "Sz:%d B", fileBytes);
  lcd.print(buf); delay(1500); displayNeedsFullRedraw = true;
}

void createFile() {
  if (fileCount >= MAX_FILES) { lcd.clear(); lcd.setCursor(0,0); lcd.print("Max files"); delay(1000); return; }
  char ext[4]; chooseExtensionDialog(ext);
  char newName[FULLNAME_LEN+1]; snprintf(newName, FULLNAME_LEN+1, "NEWFILE.%s", ext);
  uint16_t start = findFreeBlock();
  if (start == 0xFFFF) { lcd.clear(); lcd.setCursor(0,0); lcd.print("Disk full!"); delay(1000); return; }
  files[fileCount].startBlock = start;
  files[fileCount].sizeBlocks = 1;
  files[fileCount].flags = (strcmp(ext, "OEF") == 0) ? 0x01 : 0x00;
  strcpy(files[fileCount].name, newName);
  fileCount++;
  for (int i = 0; i < BLOCK_SIZE; i++) writeEEPROM(start * BLOCK_SIZE + i, 0x00);
  saveFileTable();
  selectedFile = fileCount - 1;
  lcd.clear(); lcd.setCursor(0,0); lcd.print("Created"); delay(500);
  displayNeedsFullRedraw = true;
}

void deleteFile() {
  if (fileCount == 0) return;
  for (int i = selectedFile; i < fileCount - 1; i++) files[i] = files[i + 1];
  fileCount--;
  saveFileTable();
  if (selectedFile >= fileCount) selectedFile = fileCount - 1;
  lcd.clear(); lcd.setCursor(0,0); lcd.print("Deleted"); delay(500);
  displayNeedsFullRedraw = true;
}

void buildProgram() {
  bool ok = true;
  uint8_t size = files[currentFileIdx].sizeBlocks;
  for (int i = 0; i < size; i++) {
    for (int j = 0; j < BLOCK_SIZE; j++) {
      uint8_t b = readEEPROM((files[currentFileIdx].startBlock + i) * BLOCK_SIZE + j);
      if (b == 0xFF) break;
      if (b >= 0x40 && b <= 0x5F) {
        if (b == 0x40 && j + 1 < BLOCK_SIZE) {
          uint8_t next = readEEPROM((files[currentFileIdx].startBlock + i) * BLOCK_SIZE + j + 1);
          if (next != 0x29) { ok = false; break; }
        }
      }
    }
    if (!ok) break;
  }
  if (ok) { files[currentFileIdx].flags |= 0x01; saveFileTable(); lcd.clear(); lcd.setCursor(0,0); lcd.print("Build OK!"); }
  else { files[currentFileIdx].flags &= ~0x01; saveFileTable(); lcd.clear(); lcd.setCursor(0,0); lcd.print("Build ERR!"); }
  delay(1000); displayNeedsFullRedraw = true;
}

void eraseDisk() {
  for (int i = 0; i < EEPROM_SIZE; i++) writeEEPROM(i, 0xFF);
  fileCount = 0; saveFileTable();
  lcd.clear(); lcd.setCursor(0,0); lcd.print("Erased!"); delay(1000);
  currentState = MAIN; displayNeedsFullRedraw = true;
}

void viewerPrevPage() {} void viewerNextPage() {} void viewerPrevLine() {} void viewerNextLine() {}

void startOEF() {
  pc = 0; memset(vars, 0, sizeof(vars));
  oefRunning = true; oefPaused = false; oefDelayUntil = 0;
  currentState = RUN_OEF; displayNeedsFullRedraw = true;
}

void executeOneInstruction() {
  uint16_t addr = files[currentFileIdx].startBlock * BLOCK_SIZE;
  uint8_t op = readEEPROM(addr + pc);
  if (op == 0x00) { oefRunning = false; currentState = MAIN; displayNeedsFullRedraw = true; return; }
  pc++;
  switch (op) {
    case 0x40:
      if (readEEPROM(addr + pc) == 0x29) {
        pc++;
        lcd.clear();
        lcd.setCursor(0, 0);
        while (true) {
          uint8_t ch = readEEPROM(addr + pc);
          if (ch == 0x00) break;
          if (ch >= 0x60 && ch <= 0x7F) {
            int8_t val = vars[ch - 0x60];
            char buf[5];
            sprintf(buf, "%d", val);
            lcd.print(buf);
          } else {
            lcd.print((char)ch);
          }
          pc++;
        }
        pc++;
        oefDelayUntil = millis() + 1000;
      }
      break;
    case 0x41: oefRunning = false; currentState = MAIN; displayNeedsFullRedraw = true; return;
    case 0x46: lcd.clear(); break;
    case 0x44: { uint8_t d = readEEPROM(addr + pc); pc++; oefDelayUntil = millis() + d * 100; break; }
    case 0x47: oefPaused = true; break;
    case 0x42: pc = readEEPROM(addr + pc); break;
    case 0x43: { uint8_t var = readEEPROM(addr + pc)-0x60; pc++; int8_t val = (int8_t)readEEPROM(addr + pc); pc++; if(var<32) vars[var]=val; break; }
    case 0x48: { uint8_t var = readEEPROM(addr + pc)-0x60; pc++; if(var<32) vars[var]++; break; }
    case 0x49: { uint8_t var = readEEPROM(addr + pc)-0x60; pc++; if(var<32) vars[var]--; break; }
    case 0x4A: { uint8_t var = readEEPROM(addr+pc)-0x60; pc++; uint8_t cond = readEEPROM(addr+pc); pc++; int8_t val = (int8_t)readEEPROM(addr+pc); pc++;
      uint16_t jumpAddr = readEEPROM(addr+pc) | (readEEPROM(addr+pc+1)<<8); pc+=2;
      if (var>=32) break; bool res=false; int8_t v=vars[var];
      if (cond==0x3A) res=(v==val); else if(cond==0x3B) res=(v!=val); else if(cond==0x3C) res=(v<val); else if(cond==0x3D) res=(v>val); else if(cond==0x3E) res=(v<=val); else if(cond==0x3F) res=(v>=val);
      if (res) pc=jumpAddr; break; }
    case 0x4B: { uint8_t var = readEEPROM(addr+pc)-0x60; pc++; if(var<32) { while(true) { uint8_t m=getButtonMask(); if(m!=0) { while(getButtonMask()!=0) delay(10); vars[var]=m; break; } delay(10); } } break; }
    case 0x45: { uint16_t freq = readEEPROM(addr+pc) | (readEEPROM(addr+pc+1)<<8); pc+=2; uint8_t dur = readEEPROM(addr+pc); pc++; if(soundEnabled) tone(BUZZER, freq, dur*10); oefDelayUntil=millis()+dur*10; break; }
    case 0x4C: { uint8_t var = readEEPROM(addr+pc)-0x60; pc++; uint8_t max = readEEPROM(addr+pc); pc++; if(var<32 && max>0) vars[var] = random(max); break; }
    case 0x4D: { uint8_t col = readEEPROM(addr+pc); pc++; uint8_t row = readEEPROM(addr+pc); pc++; lcd.setCursor(col & 0x0F, row & 0x01); break; }
    case 0x4E: { uint8_t note = readEEPROM(addr+pc); pc++; uint8_t dur = readEEPROM(addr+pc); pc++; if (soundEnabled) tone(BUZZER, noteToFreq(note), dur*50); oefDelayUntil = millis() + dur*50; break; }
    case 0x4F: { uint8_t var1 = readEEPROM(addr+pc)-0x60; pc++; uint8_t var2 = readEEPROM(addr+pc); pc++; if(var1<32) { if(var2>=0x60 && var2<0x80) vars[var1]+=vars[var2-0x60]; else vars[var1]+=(int8_t)var2; } break; }
    case 0x50: { uint8_t var1 = readEEPROM(addr+pc)-0x60; pc++; uint8_t var2 = readEEPROM(addr+pc); pc++; if(var1<32) { if(var2>=0x60 && var2<0x80) vars[var1]-=vars[var2-0x60]; else vars[var1]-=(int8_t)var2; } break; }
    case 0x51: { uint8_t var1 = readEEPROM(addr+pc)-0x60; pc++; uint8_t var2 = readEEPROM(addr+pc); pc++; if(var1<32) { int8_t v2=(var2>=0x60 && var2<0x80)?vars[var2-0x60]:(int8_t)var2; vars[var1]=vars[var1]*v2; } break; }
    case 0x52: { uint8_t var1 = readEEPROM(addr+pc)-0x60; pc++; uint8_t var2 = readEEPROM(addr+pc); pc++; if(var1<32) { int8_t v2=(var2>=0x60 && var2<0x80)?vars[var2-0x60]:(int8_t)var2; if(v2!=0) vars[var1]=vars[var1]/v2; } break; }
    case 0x53: { uint8_t var = readEEPROM(addr+pc)-0x60; pc++; if(var<32) vars[var] = getButtonMask(); break; }
  }
}
void playOMF() {
  currentFileIdx = selectedFile; currentState = PLAYER; displayNeedsFullRedraw = true;
  uint16_t addr = files[currentFileIdx].startBlock * BLOCK_SIZE;
  for (uint16_t i=0; i<files[currentFileIdx].sizeBlocks*BLOCK_SIZE; i++) {
    uint8_t note = readEEPROM(addr+i); if (note==0x00) break;
    if (note>=0x80 && note<=0xB6) { i++; if (i>=files[currentFileIdx].sizeBlocks*BLOCK_SIZE) break;
      uint8_t dur = readEEPROM(addr+i); if (soundEnabled) { tone(BUZZER, noteToFreq(note), dur*50); delay(dur*50); } }
  }
  currentState = DISK; displayNeedsFullRedraw = true;
}
int noteToFreq(uint8_t note) { int midi = note - 0x80 + 60; return (int)(440.0 * pow(2.0, (midi - 69) / 12.0)); }
char getCharAt(uint16_t index) { return (char)readEEPROM(files[currentFileIdx].startBlock * BLOCK_SIZE + index); }
void changeCurrentChar(int dir) { uint16_t addr = files[currentFileIdx].startBlock * BLOCK_SIZE + currentCharIndex; uint8_t cur = readEEPROM(addr); if(dir>0) cur++; else cur--; writeEEPROM(addr, cur); }

void renameBackspace() {
  if (renamePos == 0) return;
  for (uint8_t i = renamePos - 1; i < FULLNAME_LEN - 1; i++) {
    renameBuffer[i] = renameBuffer[i + 1];
  }
  renameBuffer[FULLNAME_LEN - 1] = ' ';
  renamePos--;
}

void renameDelete() {
  if (renamePos >= FULLNAME_LEN) return;
  for (uint8_t i = renamePos; i < FULLNAME_LEN - 1; i++) {
    renameBuffer[i] = renameBuffer[i + 1];
  }
  renameBuffer[FULLNAME_LEN - 1] = ' ';
}

void renameInsert() {
  if (renameBuffer[FULLNAME_LEN - 1] != ' ') return; // нет места
  for (uint8_t i = FULLNAME_LEN - 1; i > renamePos; i--) {
    renameBuffer[i] = renameBuffer[i - 1];
  }
  renameBuffer[renamePos] = ' ';
}

void createHelloOef() {
  if (fileCount >= MAX_FILES) return;
  uint16_t start = findFreeBlock(); if (start == 0xFFFF) return;
  strcpy(files[fileCount].name, "HELLO.OEF");
  files[fileCount].startBlock = start; files[fileCount].sizeBlocks = 1; files[fileCount].flags = 0x01;
  uint8_t prog[] = {0x40, 0x29, 'H','E','L','L','O', 0x00};
  for (int i=0; i<sizeof(prog); i++) writeEEPROM(start * BLOCK_SIZE + i, prog[i]);
  for (int i=sizeof(prog); i<BLOCK_SIZE; i++) writeEEPROM(start * BLOCK_SIZE + i, 0x00);
  fileCount++; saveFileTable();
}
void createHiOef() {
  if (fileCount >= MAX_FILES) return;
  uint16_t start = findFreeBlock(); if (start == 0xFFFF) return;
  strcpy(files[fileCount].name, "HI.OEF");
  files[fileCount].startBlock = start; files[fileCount].sizeBlocks = 1; files[fileCount].flags = 0x01;
  uint8_t prog[] = {0x40, 0x29, 'H','I', 0x00, 0x47, 0x41};
  for (int i=0; i<sizeof(prog); i++) writeEEPROM(start * BLOCK_SIZE + i, prog[i]);
  for (int i=sizeof(prog); i<BLOCK_SIZE; i++) writeEEPROM(start * BLOCK_SIZE + i, 0x00);
  fileCount++; saveFileTable();
}

void loadFileTable() {
  fileCount = 0;
  for (int i = 0; i < MAX_FILES; i++) {
    uint16_t addr = FILE_TABLE_START + i * (FULLNAME_LEN + 5);
    uint8_t first = readEEPROM(addr);
    if (first == 0xFF) break;
    for (int j = 0; j < FULLNAME_LEN; j++) files[fileCount].name[j] = readEEPROM(addr + j);
    files[fileCount].name[FULLNAME_LEN] = 0;
    files[fileCount].startBlock = readEEPROM(addr + FULLNAME_LEN) | (readEEPROM(addr + FULLNAME_LEN + 1) << 8);
    files[fileCount].sizeBlocks = readEEPROM(addr + FULLNAME_LEN + 2);
    files[fileCount].flags = readEEPROM(addr + FULLNAME_LEN + 3);
    fileCount++;
  }
}
void saveFileTable() {
  for (int i = 0; i < MAX_FILES; i++) {
    uint16_t addr = FILE_TABLE_START + i * (FULLNAME_LEN + 5);
    if (i < fileCount) {
      for (int j = 0; j < FULLNAME_LEN; j++) writeEEPROM(addr + j, files[i].name[j]);
      writeEEPROM(addr + FULLNAME_LEN, files[i].startBlock & 0xFF);
      writeEEPROM(addr + FULLNAME_LEN + 1, files[i].startBlock >> 8);
      writeEEPROM(addr + FULLNAME_LEN + 2, files[i].sizeBlocks);
      writeEEPROM(addr + FULLNAME_LEN + 3, files[i].flags);
      writeEEPROM(addr + FULLNAME_LEN + 4, 0x00);
    } else {
      for (int j = 0; j < FULLNAME_LEN + 5; j++) writeEEPROM(addr + j, 0xFF);
    }
  }
}
void createDefaultFile() {
  uint16_t start = findFreeBlock(); if (start == 0xFFFF) return;
  strcpy(files[0].name, "HELLO.TXT");
  files[0].startBlock = start; files[0].sizeBlocks = 1; files[0].flags = 0;
  for (int i = 0; i < BLOCK_SIZE; i++) writeEEPROM(start * BLOCK_SIZE + i, 0x00);
  fileCount = 1; saveFileTable();
}
void loadBlock(uint16_t blockIdx) { uint16_t offset = (files[currentFileIdx].startBlock + blockIdx) * BLOCK_SIZE; for (int i=0; i<BLOCK_SIZE; i++) block[i] = readEEPROM(offset + i); }
void saveBlock(uint16_t blockIdx) { uint16_t offset = (files[currentFileIdx].startBlock + blockIdx) * BLOCK_SIZE; for (int i=0; i<BLOCK_SIZE; i++) writeEEPROM(offset + i, block[i]); }
void writeEEPROM(uint16_t addr, uint8_t data) { Wire.beginTransmission(EEPROM_ADDR); Wire.write(addr>>8); Wire.write(addr&0xFF); Wire.write(data); Wire.endTransmission(); delay(5); }
uint8_t readEEPROM(uint16_t addr) { Wire.beginTransmission(EEPROM_ADDR); Wire.write(addr>>8); Wire.write(addr&0xFF); Wire.endTransmission(); Wire.requestFrom(EEPROM_ADDR,1); if(Wire.available()) return Wire.read(); return 0xFF; }
