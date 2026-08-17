#include "orangeos.h"

// ===================== UI / INPUT =====================

const char TEXT_CHARSET[] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
char nextTextChar(char c) {
  const char* p = strchr(TEXT_CHARSET, c);
  if (!p || *(p + 1) == '\0') return TEXT_CHARSET[0];
  return *(p + 1);
}
char prevTextChar(char c) {
  const char* p = strchr(TEXT_CHARSET, c);
  if (!p || p == TEXT_CHARSET) return TEXT_CHARSET[strlen(TEXT_CHARSET) - 1];
  return *(p - 1);
}
void writeCharAt(uint16_t index, char c) {
  FileEntry e; readFileEntry(currentFileIdx, &e);
  writeEEPROM((uint16_t)e.startBlock * BLOCK_SIZE + index, (uint8_t)c);
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
    case SELECT_DISK: drawSelectDiskFull(); break;
    case DISK: drawDiskFull(); break;
    case EDIT: drawEditScreenFull(); break;
    case VIEWER: drawViewerFull(); break;
    case SETTINGS: drawSettingsFull(); break;
    case PLAYER: drawPlayerFull(); break;
    case RENAME_ST: drawRenameFull(); break;
    case RUN_OEF: drawRunOefFull(); break;
    case CONTEXT_MENU: drawContextMenuFull(); break;
    case INFO_SCREEN: drawInfoScreenFull(); break;
    case TASKMAN: drawTaskmanFull(); break;
  }
}

void print2(uint8_t v) {
  if (v < 10) lcd.print('0');
  lcd.print(v);
}
void print3(uint16_t v) {
  if (v < 100) lcd.print('0');
  if (v < 10) lcd.print('0');
  lcd.print(v);
}
void printHex2(uint8_t v) {
  uint8_t hi = v >> 4;
  uint8_t lo = v & 0x0F;
  lcd.print((char)(hi < 10 ? '0' + hi : 'A' + hi - 10));
  lcd.print((char)(lo < 10 ? '0' + lo : 'A' + lo - 10));
}
void printHex4(uint16_t v) {
  printHex2(v >> 8);
  printHex2(v & 0xFF);
}
void printBlockTag(uint16_t b, const __FlashStringHelper* tag) {
  lcd.print('B');
  print3(b);
  lcd.print(' ');
  lcd.print(tag);
  lcd.print(' ');
}

void drawMainScreenFull() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(greetings[currentGreeting]);
  updateClockDisplay();
}
void updateClockDisplay() {
  DateTime now = rtc.now();
  lcd.setCursor(0,1);
  print2(now.hour());
  lcd.print(':');
  print2(now.minute());
  lcd.print(' ');
  print2(now.month());
  lcd.print('/');
  print2(now.day());
  lcd.print('/');
  print2(now.year() % 100);
}
void drawSelectDiskFull() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("1-C:OS  2-D:Data");
  lcd.setCursor(0,1);
  lcd.print("3-back");
}
void drawDiskFull() {
  lcd.clear();
  uint8_t total = getFileCount();
  if (total == 0) {
    lcd.setCursor(0, 0);
    lcd.print(F("No files"));
    lcd.setCursor(0, 1);
    lcd.print(F("1+2 to create"));
    return;
  }
  if (selectedFile >= total) selectedFile = total - 1;
  lcd.setCursor(0, 0);
  lcd.print('>');
  lcd.print(getFileName(selectedFile));
  lcd.setCursor(0, 1);
  if (total == 1) {
    lcd.print(F(" (only one)"));
  } else {
    uint8_t next = (selectedFile + 1) % total;
    lcd.print(' ');
    lcd.print(getFileName(next));
  }
}

uint8_t getAppCount() {
  uint8_t oldDisk = currentDisk;
  currentDisk = 0;
  uint8_t c = 0;
  for (uint8_t i = 0; i < getMaxFiles(); i++) {
    if (isFreeEntry(i)) continue;
    FileEntry e; readFileEntry(i, &e);
    if (e.flags & 0x01) c++;
  }
  currentDisk = oldDisk;
  return c;
}
uint8_t getAppFile(uint8_t pos) {
  uint8_t oldDisk = currentDisk;
  currentDisk = 0;
  uint8_t seen = 0;
  uint8_t result = 255;
  for (uint8_t i = 0; i < getMaxFiles(); i++) {
    if (isFreeEntry(i)) continue;
    FileEntry e; readFileEntry(i, &e);
    if (!(e.flags & 0x01)) continue;
    if (seen == pos) { result = i; break; }
    seen++;
  }
  currentDisk = oldDisk;
  return result;
}
void drawTaskmanFull() {
  lcd.clear();
  uint8_t n = getAppCount();
  lcd.setCursor(0,0);
  lcd.print(F("Apps:"));
  lcd.print(n);
  lcd.print(F(" RAM:"));
  lcd.print(getFreeRam());
  if (n == 0) { lcd.setCursor(0,1); lcd.print(F("No apps")); return; }
  if (taskmanIndex >= n) taskmanIndex = n - 1;
  char buf[17];
  for (uint8_t line = 0; line < 2; line++) {
    uint8_t pos = (taskmanIndex + line) % n;
    buf[0] = 0;
    uint8_t flat = getAppFile(pos);
    if (flat != 255) {
      uint8_t old = currentDisk; currentDisk = 0;
      strcpy(buf, getFileName(flat));
      currentDisk = old;
    }
    lcd.setCursor(0, line);
    if (line == 0) { lcd.print('>'); lcd.print(buf); }
    else { lcd.print(' '); lcd.print(buf); }
  }
}

void drawEditScreenFull() {
  lcd.clear();
  if (getCurrentExtension() == EXT_TXT) {
    lcd.setCursor(0,0); lcd.print(F("TXT Edit"));
    lcd.setCursor(0,1); lcd.print(F("Char:")); char c = getCharAt(currentCharIndex); lcd.print(c); lcd.print(F(" idx:")); lcd.print(currentCharIndex);
  } else {
    uint8_t curByte = block[currentByte];
    int8_t idx = getOpcodeIndex(curByte);
    lcd.setCursor(0,0);
    if (idx >= 0) {
      char mnem[9];
      strcpy_P(mnem, (const char*)pgm_read_word(&opcodeMnemonics[idx]));
      lcd.print('B');
      print3(currentBlock);
      lcd.print(' ');
      lcd.print(mnem);
      lcd.print(' ');
    } else {
      bool isArg = false;
      if (currentByte > 0) {
        uint8_t prev = block[currentByte - 1];
        int8_t prevIdx = getOpcodeIndex(prev);
        if (prevIdx >= 0) {
          uint8_t argBytes = pgm_read_byte(&opcodeArgBytes[prevIdx]);
          if (argBytes > 0) {
            uint8_t offset = 1;
            if (prev == 0x40) offset = 2;
            if (currentByte - offset < argBytes) {
              isArg = true;
            }
          }
        }
      }
      if (isArg) {
        uint8_t prev = block[currentByte - 1];
        int8_t prevIdx = getOpcodeIndex(prev);
        uint8_t offset = 1;
        if (prev == 0x40) offset = 2;
        uint8_t argPos = currentByte - offset;
        if (prev == 0x45) {
          if (argPos == 0) {
            uint16_t freq = block[currentByte] | (block[currentByte+1] << 8);
            printBlockTag(currentBlock, F("FREQ"));
            lcd.print(freq);
          } else if (argPos == 2) {
            printBlockTag(currentBlock, F("DUR"));
            lcd.print(curByte);
          } else {
            printBlockTag(currentBlock, F("ARG"));
            printHex2(curByte);
          }
        } else if (prev == 0x42) {
          // JUMP is a short 1-byte jump
          printBlockTag(currentBlock, F("ADDR"));
          printHex2(curByte);
        } else if (prev == 0x43) {
          if (argPos == 0) {
            printBlockTag(currentBlock, F("VAR"));
            lcd.print(curByte - 0x60);
          } else if (argPos == 1) {
            printBlockTag(currentBlock, F("VAL"));
            lcd.print((int)(int8_t)curByte);
          } else {
            printBlockTag(currentBlock, F("ARG"));
            printHex2(curByte);
          }
        } else if (prev == 0x4A) {
          if (argPos == 0) {
            printBlockTag(currentBlock, F("VAR"));
            lcd.print(curByte - 0x60);
          } else if (argPos == 1) {
            printBlockTag(currentBlock, F("COND"));
            lcd.print((char)curByte);
          } else if (argPos == 2) {
            printBlockTag(currentBlock, F("VAL"));
            lcd.print((int)(int8_t)curByte);
          } else if (argPos >= 3) {
            uint16_t jumpAddr = block[currentByte] | (block[currentByte+1] << 8);
            printBlockTag(currentBlock, F("JUMP"));
            printHex4(jumpAddr);
          } else {
            printBlockTag(currentBlock, F("ARG"));
            printHex2(curByte);
          }
        } else if (prev == 0x54) {
          if (argPos == 0) {
            printBlockTag(currentBlock, F("PAIR"));
            lcd.print(curByte - 0x60);
          } else if (argPos >= 1) {
            uint16_t val = (block[currentByte] | (block[currentByte+1] << 8));
            printBlockTag(currentBlock, F("VAL"));
            lcd.print(val);
          } else {
            printBlockTag(currentBlock, F("ARG"));
            printHex2(curByte);
          }
        } else if (prev == 0x61) {
          if (argPos == 0) {
            printBlockTag(currentBlock, F("VAR"));
            lcd.print(curByte - 0x80);
          } else if (argPos >= 1) {
            uint32_t val = 0;
            for (int i=0; i<4 && (argPos+i)<BLOCK_SIZE; i++) val |= (block[currentByte+i] << (8*i));
            printBlockTag(currentBlock, F("VAL"));
            lcd.print((unsigned long)val);
          } else {
            printBlockTag(currentBlock, F("ARG"));
            printHex2(curByte);
          }
        } else {
          printBlockTag(currentBlock, F("ARG"));
          printHex2(curByte);
        }
      } else {
        printBlockTag(currentBlock, F("HEX"));
        printHex2(curByte);
      }
    }
    lcd.setCursor(0,1);
    for (int i = 0; i < BLOCK_SIZE; i++) {
      printHex2(block[i]);
    }
    lcd.setCursor(currentByte * 2, 1);
    lcd.blink();
  }
}

void insertInstruction() {
  saveBlock(currentBlock);
  uint8_t selected = 0;
  bool done = false;
  while (!done) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print(F("Ins: "));
    char mnem[9];
    strcpy_P(mnem, (const char*)pgm_read_word(&opcodeMnemonics[selected]));
    lcd.print(mnem);
    lcd.setCursor(0,1);
    lcd.print(F("1-prev 2-next 3-ok"));
    uint8_t mask = getButtonMask();
    if (mask == 1) { if (selected > 0) selected--; else selected = opcodeCount-1; delay(150); }
    else if (mask == 2) { if (selected < opcodeCount-1) selected++; else selected = 0; delay(150); }
    else if (mask == 4) {
      uint8_t code = pgm_read_byte(&opcodeCodes[selected]);
      uint8_t argBytes = pgm_read_byte(&opcodeArgBytes[selected]);
      if (currentByte + 1 + argBytes > BLOCK_SIZE) {
        lcd.clear(); lcd.print(F("No space")); delay(500);
        done = true; break;
      }
      block[currentByte++] = code;
      for (uint8_t i = 0; i < argBytes; i++) {
        if (currentByte < BLOCK_SIZE) block[currentByte++] = 0x00;
      }
      if (code == 0x40) {
        if (currentByte < BLOCK_SIZE) block[currentByte++] = 0x29;
        for (uint8_t i = 0; i < 8 && currentByte < BLOCK_SIZE; i++) {
          block[currentByte++] = ' ';
        }
        block[currentByte++] = 0x00;
      }
      if (currentByte >= BLOCK_SIZE) currentByte = BLOCK_SIZE - 1;
      saveBlock(currentBlock);
      done = true;
      displayNeedsFullRedraw = true;
    }
    delay(50);
  }
}

void drawSettingsFull() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(F("Settings:"));
  lcd.setCursor(0,1);
  lcd.print(settingsItems[settingsIndex]);
  lcd.setCursor(strlen(settingsItems[settingsIndex]), 1);
  if (settingsIndex == 0) {
    lcd.print(soundEnabled ? F(" On") : F(" Off"));
  }
}

void drawViewerFull() {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(getFileName(currentFileIdx));
  lcd.setCursor(0,1);
  FileEntry e; readFileEntry(currentFileIdx, &e);
  uint16_t sizeBytes = (uint16_t)e.sizeBlocks * BLOCK_SIZE;
  for (uint8_t i = 0; i < 16; i++) {
    uint16_t idx = viewerOffset + i;
    if (idx < sizeBytes) {
      char c = (char)readEEPROM(e.startBlock * BLOCK_SIZE + idx);
      if (c < 0x20) c = ' ';
      lcd.print(c);
    } else lcd.print(' ');
  }
}

void drawPlayerFull() {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(F("Playing:")); lcd.print(getFileName(currentFileIdx));
  lcd.setCursor(0,1); lcd.print(F("any key to stop"));
}
void drawRenameFull() {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(F("Rename:"));
  lcd.setCursor(0,1); lcd.print(renameBuffer);
  lcd.setCursor(renamePos, 1); lcd.blink();
}
void drawRunOefFull() {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(F("Running:")); lcd.print(getFileName(currentFileIdx));
  lcd.setCursor(0,1); lcd.print(F("(1+2+3 to stop)"));
}
void drawContextMenuFull() {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(F("Context Menu:"));
  lcd.setCursor(0,1); lcd.print(contextItems[contextMenuIndex]);
}
void drawInfoScreenFull() {
  showSystemInfo();
}

void openFile() {
  uint8_t flat;
  if (!resolveSelected(&flat)) return;
  currentFileIdx = flat;
  viewerOffset = 0;
  Extension ext = getCurrentExtension();
  if (ext == EXT_OEF) startOEF();
  else if (ext == EXT_OMF) playOMF();
  else currentState = VIEWER;
  displayNeedsFullRedraw = true;
}
void editFile() {
  if (getFileCount() == 0) return;
  if (isSystemDisk()) {
    readOnlyMsg();
    return;
  }
  uint8_t flat;
  if (!resolveSelected(&flat)) return;
  currentFileIdx = flat;
  Extension ext = getCurrentExtension();
  if (ext == EXT_TXT) {
    currentBlock = 0;
    loadBlock(currentBlock);
    currentCharIndex = 0;
  } else {
    currentBlock = 0;
    loadBlock(currentBlock);
    currentByte = 0;
  }
  currentState = EDIT;
  displayNeedsFullRedraw = true;
}
void fileInfo() {
  if (getFileCount() == 0) return;
  uint8_t flat;
  if (!resolveSelected(&flat)) return;
  FileEntry e;
  readFileEntry(flat, &e);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(e.name);
  lcd.setCursor(0, 1);
  lcd.print(F("Sz:"));
  lcd.print(e.sizeBlocks * BLOCK_SIZE);
  lcd.print(F(" B"));
  delay(1500);
  displayNeedsFullRedraw = true;
}
void createFile() {
  if (isSystemDisk()) {
    readOnlyMsg();
    return;
  }
  uint8_t idx = getFileCount();
  if (idx >= getMaxFiles()) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Max files"));
    delay(1000);
    return;
  }
  char ext[4];
  chooseExtensionDialog(ext);
  char newName[FULLNAME_LEN + 1];
  strcpy(newName, "NEWFILE.");
  strcat(newName, ext);
  uint16_t start = findFreeBlock();
  if (start == 0xFFFF) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Disk full!"));
    delay(1000);
    return;
  }
  FileEntry e;
  memset(e.name, ' ', FULLNAME_LEN);
  strcpy(e.name, newName);
  e.startBlock = start;
  e.sizeBlocks = 1;
  e.flags = strcmp(ext, "OEF") == 0 ? 0x01 : 0x00;
  writeFileEntry(idx, &e);
  for (uint8_t i = 0; i < BLOCK_SIZE; i++) writeEEPROM(start * BLOCK_SIZE + i, 0x00);
  selectedFile = idx;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Created"));
  delay(500);
  displayNeedsFullRedraw = true;
}
void deleteFile() {
  if (isSystemDisk()) {
    readOnlyMsg();
    return;
  }
  uint8_t count = getFileCount();
  if (count == 0 || selectedFile >= count) return;
  deleteFileEntry(selectedFile);
  count--;
  if (count == 0) selectedFile = 0;
  else if (selectedFile >= count) selectedFile = count - 1;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Deleted"));
  delay(500);
  displayNeedsFullRedraw = true;
}

void viewerPrevPage() { if (viewerOffset >= 32) viewerOffset -= 32; else viewerOffset = 0; displayNeedsFullRedraw = true; }
void viewerNextPage() {
  FileEntry e; readFileEntry(currentFileIdx, &e);
  uint16_t sz = (uint16_t)e.sizeBlocks * BLOCK_SIZE;
  if (viewerOffset + 32 < sz) viewerOffset += 32;
  displayNeedsFullRedraw = true;
}
void viewerPrevLine() { if (viewerOffset >= 16) viewerOffset -= 16; else viewerOffset = 0; displayNeedsFullRedraw = true; }
void viewerNextLine() {
  FileEntry e; readFileEntry(currentFileIdx, &e);
  uint16_t sz = (uint16_t)e.sizeBlocks * BLOCK_SIZE;
  if (viewerOffset + 16 < sz) viewerOffset += 16;
  displayNeedsFullRedraw = true;
}

void renameDelete() {
  if (renamePos >= FULLNAME_LEN) return;
  for (uint8_t i = renamePos; i < FULLNAME_LEN - 1; i++) {
    renameBuffer[i] = renameBuffer[i + 1];
  }
  renameBuffer[FULLNAME_LEN - 1] = ' ';
}
void renameInsert() {
  if (renameBuffer[FULLNAME_LEN - 1] != ' ') return;
  for (uint8_t i = FULLNAME_LEN - 1; i > renamePos; i--) {
    renameBuffer[i] = renameBuffer[i - 1];
  }
  renameBuffer[renamePos] = ' ';
}

void handleShort(uint8_t mask) {
  switch (currentState) {
    case MAIN:
      if (mask == 1) { selectDisk(0); currentState = DISK; selectedFile = 0; displayNeedsFullRedraw = true; }
      else if (mask == 2) { selectDisk(1); currentState = DISK; selectedFile = 0; displayNeedsFullRedraw = true; }
      else if (mask == 4) { currentState = CONTEXT_MENU; contextMenuIndex = 0; displayNeedsFullRedraw = true; }
      break;
    case SELECT_DISK:
      if (mask == 1) { selectDisk(0); currentState = DISK; selectedFile = 0; displayNeedsFullRedraw = true; }
      else if (mask == 2) { selectDisk(1); currentState = DISK; selectedFile = 0; displayNeedsFullRedraw = true; }
      else if (mask == 3 || mask == 4 || mask == 7) { currentState = MAIN; displayNeedsFullRedraw = true; }
      break;
    case CONTEXT_MENU:
      if (mask == 1) { if (contextMenuIndex > 0) contextMenuIndex--; else contextMenuIndex = contextCount - 1; displayNeedsFullRedraw = true; }
      else if (mask == 2) { if (contextMenuIndex < contextCount - 1) contextMenuIndex++; else contextMenuIndex = 0; displayNeedsFullRedraw = true; }
      else if (mask == 4) {
        if (contextMenuIndex == 0) currentState = SETTINGS;
        else if (contextMenuIndex == 1) currentState = INFO_SCREEN;
        displayNeedsFullRedraw = true;
      }
      else if (mask == 3 || mask == 7) { currentState = MAIN; displayNeedsFullRedraw = true; }
      break;
    case INFO_SCREEN:
      if (mask == 3 || mask == 4 || mask == 7) { currentState = MAIN; displayNeedsFullRedraw = true; }
      break;
    case DISK: {
      uint8_t total = getFileCount();
      if (total == 0) {
        if (mask == 3) createFile();
        break;
      }
      if (mask == 1) openFile();
      else if (mask == 2) editFile();
      else if (mask == 4) fileInfo();
      else if (mask == 3) createFile();
      else if (mask == 5) {
        if (selectedFile > 0) selectedFile--;
        else selectedFile = total - 1;
        displayNeedsFullRedraw = true;
      } else if (mask == 6) {
        if (selectedFile < total - 1) selectedFile++;
        else selectedFile = 0;
        displayNeedsFullRedraw = true;
      }
      break;
    }
    case TASKMAN: {
      uint8_t n = getAppCount();
      if (n == 0) break;
      if (mask == 1) {
        uint8_t flat = getAppFile(taskmanIndex);
        if (flat != 255) {
          currentDisk = 0;
          currentFileIdx = flat;
          startOEF();
        }
      } else if (mask == 5) { if (taskmanIndex > 0) taskmanIndex--; else taskmanIndex = n - 1; displayNeedsFullRedraw = true; }
      else if (mask == 6) { if (taskmanIndex < n - 1) taskmanIndex++; else taskmanIndex = 0; displayNeedsFullRedraw = true; }
      else if (mask == 3 || mask == 7) { currentState = MAIN; displayNeedsFullRedraw = true; }
      break;
    }
    case VIEWER:
      if (mask == 1) viewerPrevPage();
      else if (mask == 2) viewerNextPage();
      else if (mask == 4) { currentState = DISK; displayNeedsFullRedraw = true; }
      else if (mask == 5) viewerPrevLine();
      else if (mask == 6) viewerNextLine();
      break;
    case EDIT:
      if (getCurrentExtension() == EXT_TXT) {
        // --- Text editor: cycle through a real charset, grow file at end ---
        FileEntry e; readFileEntry(currentFileIdx, &e);
        uint16_t sizeBytes = (uint16_t)e.sizeBlocks * BLOCK_SIZE;
        if (mask == 1) {
          writeCharAt(currentCharIndex, prevTextChar(getCharAt(currentCharIndex)));
          displayNeedsFullRedraw = true;
        } else if (mask == 2) {
          writeCharAt(currentCharIndex, nextTextChar(getCharAt(currentCharIndex)));
          displayNeedsFullRedraw = true;
        } else if (mask == 3) {
          if (currentCharIndex >= sizeBytes - 1) {
            if (e.sizeBlocks < 255) {
              uint16_t nextBlock = e.startBlock + e.sizeBlocks;
              bool freeBlk = true;
              for (uint8_t j = 0; j < getMaxFiles(); j++) {
                if (isFreeEntry(j)) continue;
                FileEntry f; readFileEntry(j, &f);
                if (f.startBlock <= nextBlock && nextBlock < f.startBlock + f.sizeBlocks) { freeBlk = false; break; }
              }
              if (freeBlk && nextBlock < getEepromSize()/BLOCK_SIZE) {
                for (uint8_t i = 0; i < BLOCK_SIZE; i++) writeEEPROM(nextBlock * BLOCK_SIZE + i, 0x00);
                e.sizeBlocks++; writeFileEntry(currentFileIdx, &e);
                currentCharIndex++;
                displayNeedsFullRedraw = true;
              } else { currentCharIndex = 0; displayNeedsFullRedraw = true; }
            } else { currentCharIndex = 0; displayNeedsFullRedraw = true; }
          } else { currentCharIndex++; displayNeedsFullRedraw = true; }
        } else if (mask == 4) {
          saveBlock(currentBlock); currentState = DISK; displayNeedsFullRedraw = true;
        } else if (mask == 5) {
          if (currentCharIndex > 0) currentCharIndex--; else currentCharIndex = sizeBytes - 1;
          displayNeedsFullRedraw = true;
        }
      } else {
        // --- Bytecode editor: modifying the current byte now refreshes the screen ---
        if (mask == 1) { block[currentByte]++; displayNeedsFullRedraw = true; }
        else if (mask == 2) { block[currentByte]--; displayNeedsFullRedraw = true; }
        else if (mask == 4) { block[currentByte] = 0; displayNeedsFullRedraw = true; }
        else if (mask == 3) { block[currentByte] += 0x55; displayNeedsFullRedraw = true; }
        else if (mask == 7) buildProgram();
        else if (mask == 5) { saveBlock(currentBlock); if (currentBlock > 0) currentBlock--; else currentBlock = getFileSizeBlocks(currentFileIdx) - 1; loadBlock(currentBlock); displayNeedsFullRedraw = true; }
        else if (mask == 6) {
          saveBlock(currentBlock);
          FileEntry e; readFileEntry(currentFileIdx, &e);
          if (currentBlock < e.sizeBlocks - 1) currentBlock++;
          else {
            uint16_t nextBlock = e.startBlock + e.sizeBlocks;
            bool free = true;
            for (uint8_t j = 0; j < getMaxFiles(); j++) {
              if (isFreeEntry(j)) continue;
              FileEntry f; readFileEntry(j, &f);
              if (f.startBlock <= nextBlock && nextBlock < f.startBlock + f.sizeBlocks) { free = false; break; }
            }
            if (free && nextBlock < getEepromSize()/BLOCK_SIZE && e.sizeBlocks < 255) {
              for (int i = 0; i < BLOCK_SIZE; i++) writeEEPROM(nextBlock * BLOCK_SIZE + i, 0x00);
              e.sizeBlocks++;
              writeFileEntry(currentFileIdx, &e);
              currentBlock = e.sizeBlocks - 1;
            }
          }
          loadBlock(currentBlock);
          displayNeedsFullRedraw = true;
        }
      }
      playToneForMask(mask);
      break;
    case SETTINGS:
      if (mask == 1) {
        if (settingsIndex > 0) settingsIndex--;
        else settingsIndex = SETTINGS_ITEMS - 1;
        displayNeedsFullRedraw = true;
      } else if (mask == 2) {
        if (settingsIndex < SETTINGS_ITEMS - 1) settingsIndex++;
        else settingsIndex = 0;
        displayNeedsFullRedraw = true;
      } else if (mask == 4) {
        if (settingsIndex == 0) {
          soundEnabled = !soundEnabled;
          if (soundEnabled) tone(BUZZER, 1500, 60);
          saveSettings();
          displayNeedsFullRedraw = true;
        } else if (settingsIndex == 1) {
          adjustTime();
          displayNeedsFullRedraw = true;
        } else if (settingsIndex == 2) {
          eraseDisk();
          displayNeedsFullRedraw = true;
        } else if (settingsIndex == 3) {
          showSystemInfo();
          delay(2000);
          displayNeedsFullRedraw = true;
        } else if (settingsIndex == 4) {
          currentState = MAIN;
          displayNeedsFullRedraw = true;
        }
      } else if (mask == 3 || mask == 7) {
        currentState = MAIN;
        displayNeedsFullRedraw = true;
      }
      break;
    case PLAYER: currentState = DISK; displayNeedsFullRedraw = true; break;
    case RENAME_ST:
      if (mask == 7) {
        FileEntry e; readFileEntry(renameTarget, &e);
        strcpy(e.name, renameBuffer);
        writeFileEntry(renameTarget, &e);
        currentState = DISK;
        renameJustEntered = false;
        displayNeedsFullRedraw = true;
        break;
      }
      if (mask == 3) { currentState = DISK; renameJustEntered = false; displayNeedsFullRedraw = true; break; }
      if (renameJustEntered) { renameJustEntered = false; break; }
      if (mask == 1) {
        renameBuffer[renamePos] = prevValidChar(renameBuffer[renamePos], renamePos);
        displayNeedsFullRedraw = true;
      } else if (mask == 2) {
        renameBuffer[renamePos] = nextValidChar(renameBuffer[renamePos], renamePos);
        displayNeedsFullRedraw = true;
      } else if (mask == 4) {
        cyclicMove(renamePos, FULLNAME_LEN, true);
        displayNeedsFullRedraw = true;
      } else if (mask == 5) {
        cyclicMove(renamePos, FULLNAME_LEN, false);
        displayNeedsFullRedraw = true;
      } else if (mask == 6) {
        renameDelete();
        displayNeedsFullRedraw = true;
      }
      break;
    case RUN_OEF: break;
  }
}

void handleVeryLongAction(uint8_t mask) {
  switch (currentState) {
    case DISK:
      if (mask == 3) deleteFile();
      else if (mask == 4) {
        uint8_t flat;
        if (!resolveSelected(&flat)) break;
        FileEntry e; readFileEntry(flat, &e);
        if (isSystemDisk()) { readOnlyMsg(); break; }
        renameTarget = flat;
        strcpy(renameBuffer, e.name);
        renamePos = 0;
        renameJustEntered = true;
        currentState = RENAME_ST;
        displayNeedsFullRedraw = true;
      }
      break;
    case EDIT:
      if (getCurrentExtension() == EXT_TXT) {
        if (mask == 3) {
          FileEntry e; readFileEntry(currentFileIdx, &e);
          uint16_t sizeBytes = (uint16_t)e.sizeBlocks * BLOCK_SIZE;
          if (currentCharIndex > 0) currentCharIndex--; else currentCharIndex = sizeBytes - 1;
          displayNeedsFullRedraw = true;
        }
      } else {
        if (mask == 2) { insertInstruction(); displayNeedsFullRedraw = true; }
        else if (mask == 3) { cyclicMove(currentByte, BLOCK_SIZE, true); displayNeedsFullRedraw = true; }
        else if (mask == 6) { cyclicMove(currentByte, BLOCK_SIZE, false); displayNeedsFullRedraw = true; }
        else if (mask == 5) {
          FileEntry e; readFileEntry(currentFileIdx, &e);
          if (currentBlock == e.sizeBlocks - 1 && e.sizeBlocks > 1) {
            saveBlock(currentBlock);
            e.sizeBlocks--;
            writeFileEntry(currentFileIdx, &e);
            currentBlock = e.sizeBlocks - 1;
            loadBlock(currentBlock);
            displayNeedsFullRedraw = true;
          }
        }
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
