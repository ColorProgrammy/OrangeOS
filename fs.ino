#include "orangeos.h"

// ===================== FILESYSTEM / STORAGE =====================

uint8_t getMaxFiles() {
  return (currentDisk == 0) ? MAX_FILES_INT : MAX_FILES_EXT;
}
uint16_t getDataStartBlock() {
  return (currentDisk == 0) ? INT_DATA_START_BLOCK : EXT_DATA_START_BLOCK;
}
uint16_t getEepromSize() {
  return (currentDisk == 0) ? INT_EEPROM_SIZE : EXT_EEPROM_SIZE;
}
void readFileEntry(uint8_t idx, FileEntry* entry) {
  uint16_t addr = FILE_TABLE_START + idx * FILE_ENTRY_SIZE;
  for (uint8_t i = 0; i < FULLNAME_LEN; i++) {
    entry->name[i] = readEEPROM(addr + i);
  }
  entry->name[FULLNAME_LEN] = 0;
  entry->startBlock = readEEPROM(addr + FULLNAME_LEN) |
                      (readEEPROM(addr + FULLNAME_LEN + 1) << 8);
  entry->sizeBlocks = readEEPROM(addr + FULLNAME_LEN + 2);
  entry->flags = readEEPROM(addr + FULLNAME_LEN + 3);
}
void writeFileEntry(uint8_t idx, const FileEntry* entry) {
  uint16_t addr = FILE_TABLE_START + idx * FILE_ENTRY_SIZE;
  for (uint8_t i = 0; i < FULLNAME_LEN; i++) {
    writeEEPROM(addr + i, entry->name[i]);
  }
  writeEEPROM(addr + FULLNAME_LEN, entry->startBlock & 0xFF);
  writeEEPROM(addr + FULLNAME_LEN + 1, entry->startBlock >> 8);
  writeEEPROM(addr + FULLNAME_LEN + 2, entry->sizeBlocks);
  writeEEPROM(addr + FULLNAME_LEN + 3, entry->flags);
  writeEEPROM(addr + FULLNAME_LEN + 4, 0x00);
}
uint8_t getFileCount() {
  uint8_t count = 0;
  uint8_t max = getMaxFiles();
  for (uint8_t i = 0; i < max; i++) {
    if (readEEPROM(FILE_TABLE_START + i * FILE_ENTRY_SIZE) == 0xFF) break;
    count++;
  }
  return count;
}
bool isFreeEntry(uint8_t idx) {
  if (idx >= getMaxFiles()) return true;
  return readEEPROM(FILE_TABLE_START + idx * FILE_ENTRY_SIZE) == 0xFF;
}
uint16_t getUsedBytes() {
  uint16_t used = 0;
  uint8_t count = getFileCount();
  for (uint8_t i = 0; i < count; i++) {
    FileEntry e;
    readFileEntry(i, &e);
    used += e.sizeBlocks * BLOCK_SIZE;
  }
  return used;
}
uint16_t getFreeBytes() {
  uint16_t total = (getEepromSize() / BLOCK_SIZE - getDataStartBlock()) * BLOCK_SIZE;
  uint16_t used = getUsedBytes();
  return (used > total) ? 0 : (total - used);
}

void initDiskLayout() {
  for (uint8_t i = 0; i < getMaxFiles(); i++) clearFileEntry(i);
  createDefaultFile();
  createHelloOef();
  createHiOef();
}

bool isSystemDisk() { return currentDisk == 0; }

void readOnlyMsg() {
  lcd.clear(); lcd.setCursor(0,0); lcd.print(F("C: read only"));
  delay(700); displayNeedsFullRedraw = true;
}
void clearFileEntry(uint8_t idx) {
  uint16_t addr = FILE_TABLE_START + idx * FILE_ENTRY_SIZE;
  for (uint8_t i = 0; i < FILE_ENTRY_SIZE; i++) writeEEPROM(addr + i, 0xFF);
}

void deleteFileEntry(uint8_t idx) {
  uint8_t count = getFileCount();
  if (idx >= count) return;
  for (uint8_t i = idx; i + 1 < count; i++) {
    FileEntry e;
    readFileEntry(i + 1, &e);
    writeFileEntry(i, &e);
  }
  clearFileEntry(count - 1);
}

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
  FileEntry e; readFileEntry(currentFileIdx, &e);
  return getExtension(e.name);
}

char* getFileName(uint8_t idx) {
  static char buf[FULLNAME_LEN+1];
  FileEntry e; readFileEntry(idx, &e);
  strcpy(buf, e.name);
  return buf;
}

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
  lcd.clear(); lcd.setCursor(0,0); lcd.print(F("Ext: "));
  lcd.setCursor(0,1); lcd.print(F("<     >  OK=3"));
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
  uint16_t maxBlocks = getEepromSize() / BLOCK_SIZE;
  uint16_t start = getDataStartBlock();
  uint8_t count = getFileCount();
  for (uint16_t i = start; i < maxBlocks; i++) {
    bool used = false;
    for (uint8_t j = 0; j < count; j++) {
      FileEntry e;
      readFileEntry(j, &e);
      if (i >= e.startBlock && i < e.startBlock + e.sizeBlocks) {
        used = true;
        break;
      }
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

void eraseInternalEeprom() {
  for (int i = 0; i < SETTINGS_EE_ADDR; i++) {
    EEPROM.update(i, 0xFF);
  }
}

void loadSettings() {
  uint8_t s = EEPROM.read(SETTINGS_EE_ADDR);
  uint8_t g = EEPROM.read(SETTINGS_EE_ADDR + 1);
  if (s == 0xFF) {
    soundEnabled = true;
    currentGreeting = random(greetCount);
    saveSettings();
  } else {
    soundEnabled = (s != 0);
    currentGreeting = (g < greetCount) ? g : 0;
  }
}

void saveSettings() {
  EEPROM.update(SETTINGS_EE_ADDR, soundEnabled ? 1 : 0);
  EEPROM.update(SETTINGS_EE_ADDR + 1, currentGreeting);
}

void enterSleep() {
  sleeping = true;
  lcd.noBacklight();
}

void wakeFromSleep() {
  sleeping = false;
  lcd.backlight();
  lastActivity = millis();
  displayNeedsFullRedraw = true;
}

bool createFileNamed(uint8_t disk, const char* name, uint8_t blocks) {
  uint8_t old = currentDisk;
  currentDisk = disk;
  if (disk == 0) { currentDisk = old; return false; }
  uint8_t idx = getFileCount();
  if (idx >= getMaxFiles()) { currentDisk = old; return false; }
  uint16_t start = findFreeBlock();
  if (start == 0xFFFF) { currentDisk = old; return false; }
  if (blocks == 0) blocks = 1;
  FileEntry e;
  memset(e.name, ' ', FULLNAME_LEN);
  strncpy(e.name, name, FULLNAME_LEN);
  e.name[FULLNAME_LEN] = 0;
  e.startBlock = start;
  e.sizeBlocks = blocks;
  e.flags = (strstr(name, ".OEF")) ? 0x01 : 0x00;
  writeFileEntry(idx, &e);
  for (uint16_t i = 0; i < (uint16_t)blocks * BLOCK_SIZE; i++) {
    writeEEPROM(start * BLOCK_SIZE + i, 0x00);
  }
  currentDisk = old;
  return true;
}

void showSystemInfo() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(F("OrangeOS v1.00"));
  uint8_t oldDisk = currentDisk;
  currentDisk = 1;
  uint8_t fileCnt = getFileCount();
  uint16_t used = getUsedBytes();
  uint16_t free = getFreeBytes();
  currentDisk = oldDisk;
  lcd.setCursor(0,1);
  lcd.print(F("F:"));
  lcd.print(fileCnt);
  lcd.print(F(" Free:"));
  lcd.print(free);
  lcd.print(F("B"));
}

void adjustTime() {
  DateTime now = rtc.now();
  uint8_t yy = now.year() % 100;
  uint8_t mo = now.month();
  uint8_t dd = now.day();
  uint8_t hr = now.hour();
  uint8_t mn = now.minute();
  uint8_t field = 0;
  bool done = false;
  const char* flds[] = {"Year", "Month", "Day", "Hour", "Min"};
  while (!done) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print(F("Set Time: "));
    lcd.print(flds[field]);
    lcd.setCursor(0,1);
    print2(yy);
    lcd.print('/');
    print2(mo);
    lcd.print('/');
    print2(dd);
    lcd.print(' ');
    print2(hr);
    lcd.print(':');
    print2(mn);
    uint8_t pos = 0;
    if (field == 1) pos = 3;
    else if (field == 2) pos = 6;
    else if (field == 3) pos = 9;
    else if (field == 4) pos = 12;
    lcd.setCursor(pos, 1);
    lcd.blink();
    uint8_t mask = getButtonMask();
    if (mask == 1) {
      if (field == 0) yy = (yy == 0) ? 99 : yy - 1;
      else if (field == 1) mo = (mo == 1) ? 12 : mo - 1;
      else if (field == 2) dd = (dd == 1) ? 31 : dd - 1;
      else if (field == 3) hr = (hr == 0) ? 23 : hr - 1;
      else mn = (mn == 0) ? 59 : mn - 1;
      delay(120);
    } else if (mask == 2) {
      if (field == 0) yy = (yy == 99) ? 0 : yy + 1;
      else if (field == 1) mo = (mo == 12) ? 1 : mo + 1;
      else if (field == 2) dd = (dd == 31) ? 1 : dd + 1;
      else if (field == 3) hr = (hr == 23) ? 0 : hr + 1;
      else mn = (mn == 59) ? 0 : mn + 1;
      delay(120);
    } else if (mask == 5) {
      field = (field == 0) ? 4 : field - 1;
      delay(150);
    } else if (mask == 6) {
      field = (field == 4) ? 0 : field + 1;
      delay(150);
    } else if (mask == 4 || mask == 7) {
      done = true;
    }
    delay(50);
  }
  lcd.noBlink();
  rtc.adjust(DateTime(2000 + yy, mo, dd, hr, mn, 0));
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(F("Time saved!"));
  delay(800);
}

bool resolveSelected(uint8_t* flat) {
  if (selectedFile >= getFileCount()) return false;
  *flat = selectedFile;
  return true;
}

void selectDisk(uint8_t disk) {
  currentDisk = disk;
  selectedFile = 0;
}

void writeEEPROM(uint16_t addr, uint8_t data) {
  if (currentDisk == 0) {
    EEPROM.update(addr, data);
  } else {
    Wire.beginTransmission(EEPROM_ADDR);
    Wire.write(addr >> 8);
    Wire.write(addr & 0xFF);
    Wire.write(data);
    Wire.endTransmission();
    delay(5);
  }
}

uint8_t readEEPROM(uint16_t addr) {
  if (currentDisk == 0) {
    return EEPROM.read(addr);
  } else {
    Wire.beginTransmission(EEPROM_ADDR);
    Wire.write(addr >> 8);
    Wire.write(addr & 0xFF);
    Wire.endTransmission();
    Wire.requestFrom(EEPROM_ADDR, 1);
    if (Wire.available()) return Wire.read();
    return 0xFF;
  }
}

uint8_t getFileSizeBlocks(uint8_t idx) {
  FileEntry e; readFileEntry(idx, &e);
  return e.sizeBlocks;
}

void loadBlock(uint16_t blockIdx) {
  FileEntry e; readFileEntry(currentFileIdx, &e);
  uint16_t offset = (e.startBlock + blockIdx) * BLOCK_SIZE;
  for (int i=0; i<BLOCK_SIZE; i++) block[i] = readEEPROM(offset + i);
}

void saveBlock(uint16_t blockIdx) {
  FileEntry e; readFileEntry(currentFileIdx, &e);
  uint16_t offset = (e.startBlock + blockIdx) * BLOCK_SIZE;
  for (int i=0; i<BLOCK_SIZE; i++) writeEEPROM(offset + i, block[i]);
}

void eraseDisk() {
  if (currentDisk == 0) {
    lcd.clear(); lcd.setCursor(0,0); lcd.print(F("Can't erase C:")); delay(1000);
    return;
  }
  lcd.clear(); lcd.setCursor(0,0); lcd.print(F("Erasing..."));
  for (int i = 0; i < EXT_EEPROM_SIZE; i++) {
    Wire.beginTransmission(EEPROM_ADDR);
    Wire.write(i >> 8);
    Wire.write(i & 0xFF);
    Wire.write(0xFF);
    Wire.endTransmission();
    delay(2);
  }
  lcd.clear(); lcd.setCursor(0,0); lcd.print(F("Erased!"));
  delay(1000);
  initDiskLayout();
  currentState = MAIN; displayNeedsFullRedraw = true;
}

void createDefaultFile() {
  if (getFileCount() >= getMaxFiles()) return;
  uint16_t start = findFreeBlock(); if (start == 0xFFFF) return;
  uint8_t idx = 0xFF;
  for (uint8_t i = 0; i < getMaxFiles(); i++) { if (isFreeEntry(i)) { idx = i; break; } }
  if (idx == 0xFF) return;
  FileEntry e;
  memset(e.name, ' ', FULLNAME_LEN);
  strcpy(e.name, "HELLO.TXT");
  e.startBlock = start;
  e.sizeBlocks = 1;
  e.flags = 0;
  writeFileEntry(idx, &e);
  for (int i = 0; i < BLOCK_SIZE; i++) writeEEPROM(start * BLOCK_SIZE + i, 0x00);
}

void createHelloOef() {
  if (getFileCount() >= getMaxFiles()) return;
  uint16_t start = findFreeBlock(); if (start == 0xFFFF) return;
  uint8_t idx = 0xFF;
  for (uint8_t i = 0; i < getMaxFiles(); i++) { if (isFreeEntry(i)) { idx = i; break; } }
  if (idx == 0xFF) return;
  FileEntry e;
  memset(e.name, ' ', FULLNAME_LEN);
  strcpy(e.name, "HELLO.OEF");
  e.startBlock = start;
  e.sizeBlocks = 1;
  e.flags = 0x01;
  writeFileEntry(idx, &e);
  uint8_t prog[] = {0x40, 0x29, 'H','E','L','L','O', 0x00};
  for (int i=0; i<sizeof(prog); i++) writeEEPROM(start * BLOCK_SIZE + i, prog[i]);
  for (int i=sizeof(prog); i<BLOCK_SIZE; i++) writeEEPROM(start * BLOCK_SIZE + i, 0x00);
}

void createHiOef() {
  if (getFileCount() >= getMaxFiles()) return;
  uint16_t start = findFreeBlock(); if (start == 0xFFFF) return;
  uint8_t idx = 0xFF;
  for (uint8_t i = 0; i < getMaxFiles(); i++) { if (isFreeEntry(i)) { idx = i; break; } }
  if (idx == 0xFF) return;
  FileEntry e;
  memset(e.name, ' ', FULLNAME_LEN);
  strcpy(e.name, "HI.OEF");
  e.startBlock = start;
  e.sizeBlocks = 1;
  e.flags = 0x01;
  writeFileEntry(idx, &e);
  uint8_t prog[] = {0x40, 0x29, 'H','I', 0x00, 0x47, 0x41};
  for (int i=0; i<sizeof(prog); i++) writeEEPROM(start * BLOCK_SIZE + i, prog[i]);
  for (int i=sizeof(prog); i<BLOCK_SIZE; i++) writeEEPROM(start * BLOCK_SIZE + i, 0x00);
}
