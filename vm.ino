#include "orangeos.h"

// ===================== VIRTUAL MACHINE / SYSCALLS =====================

uint16_t getFreeRam() {
    extern int __heap_start, *__brkval;
    int v;
    return (uint16_t)((int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval));
}

void syscall(uint8_t id, uint8_t* args, uint8_t argCount) {
    (void)argCount;
    switch (id) {
        case SYS_PRINT_STR: lcd.print((char*)args); break;
        case SYS_PRINT_CHAR: lcd.print((char)*args); break;
        case SYS_PRINT_INT8: lcd.print(*(int8_t*)args); break;
        case SYS_PRINT_INT16: lcd.print(*(int16_t*)args); break;
        case SYS_PRINT_INT32: lcd.print(*(int32_t*)args); break;
        case SYS_CLS: lcd.clear(); break;
        case SYS_LOCATE: lcd.setCursor(args[0], args[1]); break;
        case SYS_GETBTN: *(uint8_t*)args = getButtonMask(); break;
        case SYS_DELAY_MS: delay(*(uint16_t*)args); break;
        case SYS_TONE: if (soundEnabled) tone(BUZZER, *(uint16_t*)args, ((uint8_t*)args)[2] * 10); break;
        case SYS_EXIT: oefRunning = false; currentState = MAIN; displayNeedsFullRedraw = true; break;
        case SYS_GET_VERSION: if (args) strcpy((char*)args, "v1.00"); break;
        case SYS_GET_FILE_COUNT: if (args) *(uint8_t*)args = getFileCount(); break;
        case SYS_GET_USED_SPACE: if (args) *(uint16_t*)args = getUsedBytes(); break;
        case SYS_GET_FREE_SPACE: if (args) *(uint16_t*)args = getFreeBytes(); break;
        case SYS_GET_UPTIME: if (args) *(uint32_t*)args = millis(); break;
        case SYS_GET_FREE_RAM: if (args) *(uint16_t*)args = getFreeRam(); break;
        case SYS_SHOW_RAM: lcd.print(F("RAM:")); lcd.print(getFreeRam()); lcd.print(F("B")); break;
        case SYS_SHOW_UPTIME: { uint32_t s = millis() / 1000; lcd.print(F("Up:")); lcd.print(s); lcd.print(F("s")); } break;
        case SYS_PRINT_STR_PGM: lcd.print((const __FlashStringHelper*)args); break;
        case SYS_GET_FILENAME: if (args) {
            uint8_t idx = args[0];
            if (idx < getMaxFiles()) strcpy((char*)&args[1], getFileName(idx));
            else args[1] = 0;
        } break;
        case SYS_GET_FILE_SIZE: if (args) {
            uint8_t idx = args[0];
            if (idx < getMaxFiles()) {
                FileEntry e; readFileEntry(idx, &e);
                *(uint16_t*)(&args[1]) = e.sizeBlocks * BLOCK_SIZE;
            } else *(uint16_t*)(&args[1]) = 0;
        } break;
        case SYS_GET_FILE_FLAGS: if (args) {
            uint8_t idx = args[0];
            if (idx < getMaxFiles()) {
                FileEntry e; readFileEntry(idx, &e);
                args[1] = e.flags;
            } else args[1] = 0;
        } break;
        case SYS_READ_BYTE: if (args) {
            uint8_t idx = args[0];
            uint16_t pos = *(uint16_t*)(&args[1]);
            if (idx < getMaxFiles()) {
                FileEntry e; readFileEntry(idx, &e);
                uint16_t addr = e.startBlock * BLOCK_SIZE + pos;
                args[3] = readEEPROM(addr);
            } else args[3] = 0xFF;
        } break;
        case SYS_WRITE_BYTE: if (args) {
            if (isSystemDisk()) break;
            uint8_t idx = args[0];
            uint16_t pos = *(uint16_t*)(&args[1]);
            uint8_t byte = args[3];
            if (idx < getMaxFiles()) {
                FileEntry e; readFileEntry(idx, &e);
                uint16_t addr = e.startBlock * BLOCK_SIZE + pos;
                writeEEPROM(addr, byte);
            }
        } break;
        case SYS_CREATE_FILE: if (args) {
            if (isSystemDisk()) break;
            char* name = (char*)args;
            uint8_t blocks = args[strlen(name) + 1];
            uint8_t idx = getFileCount();
            if (idx < getMaxFiles()) {
                uint16_t start = findFreeBlock();
                if (start != 0xFFFF) {
                    FileEntry e;
                    memset(e.name, ' ', FULLNAME_LEN);
                    strncpy(e.name, name, FULLNAME_LEN);
                    e.name[FULLNAME_LEN] = 0;
                    e.startBlock = start;
                    e.sizeBlocks = blocks;
                    e.flags = (strstr(name, ".OEF") ? 0x01 : 0x00);
                    writeFileEntry(idx, &e);
                    for (uint16_t i = 0; i < blocks * BLOCK_SIZE; i++)
                        writeEEPROM(start * BLOCK_SIZE + i, 0x00);
                }
            }
            args[0] = idx;
        } break;
        case SYS_DELETE_FILE: if (args) {
            if (isSystemDisk()) break;
            uint8_t idx = args[0];
            if (idx < getFileCount()) {
                deleteFileEntry(idx);
            }
        } break;
        case SYS_RENAME_FILE: if (args) {
            if (isSystemDisk()) break;
            uint8_t idx = args[0];
            char* newName = (char*)&args[1];
            if (idx < getMaxFiles()) {
                FileEntry e; readFileEntry(idx, &e);
                strncpy(e.name, newName, FULLNAME_LEN);
                e.name[FULLNAME_LEN] = 0;
                writeFileEntry(idx, &e);
            }
        } break;
        case SYS_RUN_OEF: if (args) {
            uint8_t idx = args[0];
            if (idx < getMaxFiles()) {
                FileEntry e; readFileEntry(idx, &e);
                currentFileIdx = idx;
                startOEF();
            }
        } break;
        case SYS_GET_KEY: if (args) {
            uint8_t m;
            do { m = getButtonMask(); delay(10); } while (m == 0);
            if (m == 7) { oefRunning = false; oefPaused = false; currentState = MAIN; displayNeedsFullRedraw = true; break; }
            while (getButtonMask() != 0) delay(10);
            args[0] = m;
        } break;
        case SYS_GET_EEPROM_FREE: if (args) {
            *(uint16_t*)args = getFreeBytes();
        } break;
        case SYS_SWITCH_DISK: if (args) {
            uint8_t disk = args[0];
            if (disk <= 1) currentDisk = disk;
        } break;
        case SYS_GET_CURRENT_DISK: if (args) args[0] = currentDisk; break;
        case SYS_TOGGLE_SOUND: soundEnabled = !soundEnabled; break;
        case SYS_GET_FILE_COUNT_DISK: if (args) {
            uint8_t disk = args[0];
            uint8_t oldDisk = currentDisk;
            currentDisk = disk;
            args[1] = getFileCount();
            currentDisk = oldDisk;
        } break;
        case SYS_GET_FILENAME_DISK: if (args) {
            uint8_t disk = args[0];
            uint8_t idx = args[1];
            uint8_t oldDisk = currentDisk;
            currentDisk = disk;
            if (idx < getMaxFiles()) strcpy((char*)&args[2], getFileName(idx));
            else args[2] = 0;
            currentDisk = oldDisk;
        } break;
        case SYS_GET_FREE_SPACE_DISK: if (args) {
            uint8_t disk = args[0];
            uint8_t oldDisk = currentDisk;
            currentDisk = disk;
            *(uint16_t*)(&args[1]) = getFreeBytes();
            currentDisk = oldDisk;
        } break;
        case SYS_GET_USED_SPACE_DISK: if (args) {
            uint8_t disk = args[0];
            uint8_t oldDisk = currentDisk;
            currentDisk = disk;
            *(uint16_t*)(&args[1]) = getUsedBytes();
            currentDisk = oldDisk;
        } break;
        case SYS_SHOW_TASKS: {
            oefRunning = false; oefPaused = false;
            currentDisk = 0;
              taskmanIndex = 0;
            currentState = TASKMAN; displayNeedsFullRedraw = true;
        } break;
        case SYS_SHOW_INFO: showSystemInfo(); break;
        case SYS_SHOW_TIME: updateClockDisplay(); break;
        case SYS_OPEN_SETTINGS:
          oefRunning = false; oefPaused = false;
          currentState = SETTINGS; settingsIndex = 0;
          displayNeedsFullRedraw = true;
          break;
        default: break;
    }
}

static const char str_STR[] PROGMEM = "STR";
static const char str_STRV[] PROGMEM = "STRV";
static const char str_PRINT[] PROGMEM = "PRINT";
static const char str_EXIT[] PROGMEM = "EXIT";
static const char str_JUMP[] PROGMEM = "JUMP";
static const char str_LET[] PROGMEM = "LET";
static const char str_DELAY[] PROGMEM = "DELAY";
static const char str_TONE[] PROGMEM = "TONE";
static const char str_CLS[] PROGMEM = "CLS";
static const char str_PAUSE[] PROGMEM = "PAUSE";
static const char str_INC[] PROGMEM = "INC";
static const char str_DEC[] PROGMEM = "DEC";
static const char str_IF[] PROGMEM = "IF";
static const char str_WAITKEY[] PROGMEM = "WAITKEY";
static const char str_RAND[] PROGMEM = "RAND";
static const char str_LOCATE[] PROGMEM = "LOCATE";
static const char str_PLAYNOTE[] PROGMEM = "PLAYNOTE";
static const char str_ADD[] PROGMEM = "ADD";
static const char str_SUB[] PROGMEM = "SUB";
static const char str_MUL[] PROGMEM = "MUL";
static const char str_DIV[] PROGMEM = "DIV";
static const char str_GETBTN[] PROGMEM = "GETBTN";
static const char str_LET16[] PROGMEM = "LET16";
static const char str_INC16[] PROGMEM = "INC16";
static const char str_DEC16[] PROGMEM = "DEC16";
static const char str_ADD16[] PROGMEM = "ADD16";
static const char str_SUB16[] PROGMEM = "SUB16";
static const char str_MUL16[] PROGMEM = "MUL16";
static const char str_DIV16[] PROGMEM = "DIV16";
static const char str_PRINT16[] PROGMEM = "PRINT16";
static const char str_IFEQ16[] PROGMEM = "IFEQ16";
static const char str_IFNE16[] PROGMEM = "IFNE16";
static const char str_IFGT16[] PROGMEM = "IFGT16";
static const char str_IFLT16[] PROGMEM = "IFLT16";
static const char str_RAND16[] PROGMEM = "RAND16";
static const char str_LET32[] PROGMEM = "LET32";
static const char str_ADD32[] PROGMEM = "ADD32";
static const char str_SUB32[] PROGMEM = "SUB32";
static const char str_MUL32[] PROGMEM = "MUL32";
static const char str_DIV32[] PROGMEM = "DIV32";
static const char str_INC32[] PROGMEM = "INC32";
static const char str_DEC32[] PROGMEM = "DEC32";
static const char str_PRINT32[] PROGMEM = "PRINT32";
static const char str_IFEQ32[] PROGMEM = "IFEQ32";
static const char str_IFNE32[] PROGMEM = "IFNE32";
static const char str_IFGT32[] PROGMEM = "IFGT32";
static const char str_IFLT32[] PROGMEM = "IFLT32";
static const char str_RAND32[] PROGMEM = "RAND32";
static const char str_JMP16[] PROGMEM = "JMP16";
static const char str_SYSCALL[] PROGMEM = "SYSCALL";

const char* const opcodeMnemonics[] PROGMEM = {
  str_STR, str_STRV, str_PRINT, str_EXIT, str_JUMP, str_LET, str_DELAY,
  str_TONE, str_CLS, str_PAUSE, str_INC, str_DEC, str_IF,
  str_WAITKEY, str_RAND, str_LOCATE, str_PLAYNOTE, str_ADD, str_SUB,
  str_MUL, str_DIV, str_GETBTN,
  str_LET16, str_INC16, str_DEC16, str_ADD16, str_SUB16, str_MUL16, str_DIV16,
  str_PRINT16, str_IFEQ16, str_IFNE16, str_IFGT16, str_IFLT16, str_RAND16,
  str_LET32, str_ADD32, str_SUB32, str_MUL32, str_DIV32,
  str_INC32, str_DEC32, str_PRINT32, str_IFEQ32, str_IFNE32,
  str_IFGT32, str_IFLT32, str_RAND32,
  str_JMP16, str_SYSCALL
};

const uint8_t opcodeCodes[] PROGMEM = {
  0x29, 0x2A, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A,
  0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53,
  0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60,
  0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D,
  0x2B, 0x70
};

// NOTE: JUMP (0x42) is a short 1-byte jump (within first 256 bytes of the file).
// JMP16 (0x2B) is the full 16-bit jump. The table now matches the implementation.
const uint8_t opcodeArgBytes[] PROGMEM = {
  0, 0, 0, 0, 1, 2, 1, 3, 0, 0, 1, 1, 4,
  1, 2, 2, 2, 2, 2, 2, 1,
  3, 1, 1, 2, 2, 2, 2, 1, 3, 3, 3, 3, 2,
  5, 2, 2, 2, 2, 1, 1, 1, 7, 7, 7, 7, 5,
  2, 0
};

const uint8_t opcodeCount = sizeof(opcodeCodes) / sizeof(opcodeCodes[0]);

int8_t getOpcodeIndex(uint8_t byte) {
  for (uint8_t i = 0; i < opcodeCount; i++) {
    if (pgm_read_byte(&opcodeCodes[i]) == byte) return i;
  }
  return -1;
}

void startOEF() {
  pc = 0;
  memset(vars8, 0, sizeof(vars8));
  memset(vars16, 0, sizeof(vars16));
  memset(vars32, 0, sizeof(vars32));
  currentPrivilege = 0;
  oefRunning = true; oefPaused = false; oefDelayUntil = 0;
  currentState = RUN_OEF; displayNeedsFullRedraw = true;
}

void executeOneInstruction() {
  FileEntry e; readFileEntry(currentFileIdx, &e);
  uint16_t fileSizeBytes = (uint16_t)e.sizeBlocks * BLOCK_SIZE;
  uint16_t addr = e.startBlock * BLOCK_SIZE;

  // Bounds check: stop if pc ran past the end of this file (prevents
  // the VM from executing bytes that belong to other files).
  if (pc >= fileSizeBytes) {
    oefRunning = false; oefPaused = false;
    currentState = MAIN; displayNeedsFullRedraw = true;
    return;
  }

  uint8_t op = readEEPROM(addr + pc);
  if (op == 0x00) { syscall(SYS_EXIT, NULL, 0); return; }
  pc++;
  switch (op) {
    case 0x40: {
      uint8_t marker = readEEPROM(addr + pc);
      if (marker == 0x29 || marker == 0x2A) {
        pc++;
        syscall(SYS_CLS, NULL, 0);
        uint8_t loc[] = {0,0};
        syscall(SYS_LOCATE, loc, 2);
        bool varMode = (marker == 0x2A);
        while (pc < fileSizeBytes) {
          uint8_t ch = readEEPROM(addr + pc);
          if (ch == 0x00) break;
          if (varMode && ch >= 0x60 && ch <= 0x7F) {
            int8_t val = vars8[ch - 0x60];
            syscall(SYS_PRINT_INT8, (uint8_t*)&val, 1);
          } else {
            syscall(SYS_PRINT_CHAR, &ch, 1);
          }
          pc++;
        }
        pc++;
      }
      break;
    }
    case 0x41: syscall(SYS_EXIT, NULL, 0); return;
    case 0x46: syscall(SYS_CLS, NULL, 0); break;
    case 0x44: { uint8_t d = readEEPROM(addr + pc); pc++; oefDelayUntil = millis() + d * 100; break; }
    case 0x47: oefPaused = true; break;
    case 0x42: pc = readEEPROM(addr + pc); break;
    case 0x2B: pc = readEEPROM(addr + pc) | (readEEPROM(addr + pc + 1) << 8); break;
    case 0x43: { uint8_t var = readEEPROM(addr + pc)-0x60; pc++; int8_t val = (int8_t)readEEPROM(addr + pc); pc++; if(var<32) vars8[var]=val; break; }
    case 0x48: { uint8_t var = readEEPROM(addr + pc)-0x60; pc++; if(var<32) vars8[var]++; break; }
    case 0x49: { uint8_t var = readEEPROM(addr + pc)-0x60; pc++; if(var<32) vars8[var]--; break; }
    case 0x4A: { uint8_t var = readEEPROM(addr+pc)-0x60; pc++; uint8_t cond = readEEPROM(addr+pc); pc++; int8_t val = (int8_t)readEEPROM(addr+pc); pc++;
      uint16_t jumpAddr = readEEPROM(addr+pc) | (readEEPROM(addr+pc+1)<<8); pc+=2;
      if (var>=32) break; bool res=false; int8_t v=vars8[var];
      if (cond==0x3A) res=(v==val); else if(cond==0x3B) res=(v!=val); else if(cond==0x3C) res=(v<val); else if(cond==0x3D) res=(v>val); else if(cond==0x3E) res=(v<=val); else if(cond==0x3F) res=(v>=val);
      if (res) pc=jumpAddr; break; }
    case 0x4B: { uint8_t var = readEEPROM(addr+pc)-0x60; pc++; if(var<32) { while(true) { uint8_t m; syscall(SYS_GETBTN, &m, 1); if(m!=0) { if(m==7) { oefRunning=false; oefPaused=false; currentState=MAIN; displayNeedsFullRedraw=true; return; } while(getButtonMask()!=0) delay(10); vars8[var]=m; break; } delay(10); } } break; }
    case 0x45: { uint16_t freq = readEEPROM(addr+pc) | (readEEPROM(addr+pc+1)<<8); pc+=2; uint8_t dur = readEEPROM(addr+pc); pc++; uint8_t args[3]; *(uint16_t*)(args) = freq; args[2] = dur; syscall(SYS_TONE, args, 3); oefDelayUntil=millis()+dur*10; break; }
    case 0x4C: { uint8_t var = readEEPROM(addr+pc)-0x60; pc++; uint8_t max = readEEPROM(addr+pc); pc++; if(var<32 && max>0) vars8[var] = random(max); break; }
    case 0x4D: { uint8_t col = readEEPROM(addr+pc); pc++; uint8_t row = readEEPROM(addr+pc); pc++; uint8_t loc[2] = {col, row}; syscall(SYS_LOCATE, loc, 2); break; }
    case 0x4E: { uint8_t note = readEEPROM(addr+pc); pc++; uint8_t dur = readEEPROM(addr+pc); pc++; uint16_t freq = noteToFreq(note); uint8_t args[3]; *(uint16_t*)(args) = freq; args[2] = dur; syscall(SYS_TONE, args, 3); oefDelayUntil = millis() + dur*50; break; }
    case 0x4F: { uint8_t var1 = readEEPROM(addr+pc)-0x60; pc++; uint8_t var2 = readEEPROM(addr+pc); pc++; if(var1<32) { if(var2>=0x60 && var2<0x80) vars8[var1]+=vars8[var2-0x60]; else vars8[var1]+=(int8_t)var2; } break; }
    case 0x50: { uint8_t var1 = readEEPROM(addr+pc)-0x60; pc++; uint8_t var2 = readEEPROM(addr+pc); pc++; if(var1<32) { if(var2>=0x60 && var2<0x80) vars8[var1]-=vars8[var2-0x60]; else vars8[var1]-=(int8_t)var2; } break; }
    case 0x51: { uint8_t var1 = readEEPROM(addr+pc)-0x60; pc++; uint8_t var2 = readEEPROM(addr+pc); pc++; if(var1<32) { int8_t v2=(var2>=0x60 && var2<0x80)?vars8[var2-0x60]:(int8_t)var2; vars8[var1]*=v2; } break; }
    case 0x52: { uint8_t var1 = readEEPROM(addr+pc)-0x60; pc++; uint8_t var2 = readEEPROM(addr+pc); pc++; if(var1<32) { int8_t v2=(var2>=0x60 && var2<0x80)?vars8[var2-0x60]:(int8_t)var2; if(v2!=0) vars8[var1]/=v2; } break; }
    case 0x53: { uint8_t var = readEEPROM(addr+pc)-0x60; pc++; if(var<32) { uint8_t m; syscall(SYS_GETBTN, &m, 1); vars8[var]=m; } break; }
    case 0x54: { uint8_t pair = readEEPROM(addr+pc)-0x60; pc++; uint16_t val = readEEPROM(addr+pc) | (readEEPROM(addr+pc+1)<<8); pc+=2; if(pair<8) vars16[pair]=val; break; }
    case 0x55: { uint8_t pair = readEEPROM(addr+pc)-0x60; pc++; if(pair<8) vars16[pair]++; break; }
    case 0x56: { uint8_t pair = readEEPROM(addr+pc)-0x60; pc++; if(pair<8) vars16[pair]--; break; }
    case 0x57: { uint8_t pairA = readEEPROM(addr+pc)-0x60; pc++; uint8_t pairB = readEEPROM(addr+pc)-0x60; pc++; if(pairA<8 && pairB<8) vars16[pairA] += vars16[pairB]; break; }
    case 0x58: { uint8_t pairA = readEEPROM(addr+pc)-0x60; pc++; uint8_t pairB = readEEPROM(addr+pc)-0x60; pc++; if(pairA<8 && pairB<8) vars16[pairA] -= vars16[pairB]; break; }
    case 0x59: { uint8_t pairA = readEEPROM(addr+pc)-0x60; pc++; uint8_t pairB = readEEPROM(addr+pc)-0x60; pc++; if(pairA<8 && pairB<8) vars16[pairA] *= vars16[pairB]; break; }
    case 0x5A: { uint8_t pairA = readEEPROM(addr+pc)-0x60; pc++; uint8_t pairB = readEEPROM(addr+pc)-0x60; pc++; if(pairA<8 && pairB<8 && vars16[pairB]!=0) vars16[pairA] /= vars16[pairB]; break; }
    case 0x5B: { uint8_t pair = readEEPROM(addr+pc)-0x60; pc++; if(pair<8) { syscall(SYS_PRINT_INT16, (uint8_t*)&vars16[pair], 2); } break; }
    case 0x5C: { uint8_t pair = readEEPROM(addr+pc)-0x60; pc++; uint16_t val = readEEPROM(addr+pc) | (readEEPROM(addr+pc+1)<<8); pc+=2; uint16_t jumpAddr = readEEPROM(addr+pc) | (readEEPROM(addr+pc+1)<<8); pc+=2; if(pair<8 && vars16[pair]==val) pc=jumpAddr; break; }
    case 0x5D: { uint8_t pair = readEEPROM(addr+pc)-0x60; pc++; uint16_t val = readEEPROM(addr+pc) | (readEEPROM(addr+pc+1)<<8); pc+=2; uint16_t jumpAddr = readEEPROM(addr+pc) | (readEEPROM(addr+pc+1)<<8); pc+=2; if(pair<8 && vars16[pair]!=val) pc=jumpAddr; break; }
    case 0x5E: { uint8_t pair = readEEPROM(addr+pc)-0x60; pc++; uint16_t val = readEEPROM(addr+pc) | (readEEPROM(addr+pc+1)<<8); pc+=2; uint16_t jumpAddr = readEEPROM(addr+pc) | (readEEPROM(addr+pc+1)<<8); pc+=2; if(pair<8 && vars16[pair]>val) pc=jumpAddr; break; }
    case 0x5F: { uint8_t pair = readEEPROM(addr+pc)-0x60; pc++; uint16_t val = readEEPROM(addr+pc) | (readEEPROM(addr+pc+1)<<8); pc+=2; uint16_t jumpAddr = readEEPROM(addr+pc) | (readEEPROM(addr+pc+1)<<8); pc+=2; if(pair<8 && vars16[pair]<val) pc=jumpAddr; break; }
    case 0x60: { uint8_t pair = readEEPROM(addr+pc)-0x60; pc++; uint16_t maxVal = readEEPROM(addr+pc) | (readEEPROM(addr+pc+1)<<8); pc+=2; if(pair<8 && maxVal>0) vars16[pair] = random(maxVal); break; }
    case 0x61: { uint8_t var = readEEPROM(addr+pc)-0x80; pc++; uint32_t val = readEEPROM(addr+pc) | (readEEPROM(addr+pc+1)<<8) | (readEEPROM(addr+pc+2)<<16) | (readEEPROM(addr+pc+3)<<24); pc+=4; if(var<16) vars32[var]=val; break; }
    case 0x62: { uint8_t varA = readEEPROM(addr+pc)-0x80; pc++; uint8_t varB = readEEPROM(addr+pc)-0x80; pc++; if(varA<16 && varB<16) vars32[varA] += vars32[varB]; break; }
    case 0x63: { uint8_t varA = readEEPROM(addr+pc)-0x80; pc++; uint8_t varB = readEEPROM(addr+pc)-0x80; pc++; if(varA<16 && varB<16) vars32[varA] -= vars32[varB]; break; }
    case 0x64: { uint8_t varA = readEEPROM(addr+pc)-0x80; pc++; uint8_t varB = readEEPROM(addr+pc)-0x80; pc++; if(varA<16 && varB<16) vars32[varA] *= vars32[varB]; break; }
    case 0x65: { uint8_t varA = readEEPROM(addr+pc)-0x80; pc++; uint8_t varB = readEEPROM(addr+pc)-0x80; pc++; if(varA<16 && varB<16 && vars32[varB]!=0) vars32[varA] /= vars32[varB]; break; }
    case 0x66: { uint8_t var = readEEPROM(addr+pc)-0x80; pc++; if(var<16) vars32[var]++; break; }
    case 0x67: { uint8_t var = readEEPROM(addr+pc)-0x80; pc++; if(var<16) vars32[var]--; break; }
    case 0x68: { uint8_t var = readEEPROM(addr+pc)-0x80; pc++; if(var<16) { syscall(SYS_PRINT_INT32, (uint8_t*)&vars32[var], 4); } break; }
    case 0x69: { uint8_t var = readEEPROM(addr+pc)-0x80; pc++; uint32_t val = readEEPROM(addr+pc) | (readEEPROM(addr+pc+1)<<8) | (readEEPROM(addr+pc+2)<<16) | (readEEPROM(addr+pc+3)<<24); pc+=4; uint16_t jumpAddr = readEEPROM(addr+pc) | (readEEPROM(addr+pc+1)<<8); pc+=2; if(var<16 && vars32[var]==val) pc=jumpAddr; break; }
    case 0x6A: { uint8_t var = readEEPROM(addr+pc)-0x80; pc++; uint32_t val = readEEPROM(addr+pc) | (readEEPROM(addr+pc+1)<<8) | (readEEPROM(addr+pc+2)<<16) | (readEEPROM(addr+pc+3)<<24); pc+=4; uint16_t jumpAddr = readEEPROM(addr+pc) | (readEEPROM(addr+pc+1)<<8); pc+=2; if(var<16 && vars32[var]!=val) pc=jumpAddr; break; }
    case 0x6B: { uint8_t var = readEEPROM(addr+pc)-0x80; pc++; uint32_t val = readEEPROM(addr+pc) | (readEEPROM(addr+pc+1)<<8) | (readEEPROM(addr+pc+2)<<16) | (readEEPROM(addr+pc+3)<<24); pc+=4; uint16_t jumpAddr = readEEPROM(addr+pc) | (readEEPROM(addr+pc+1)<<8); pc+=2; if(var<16 && vars32[var]>val) pc=jumpAddr; break; }
    case 0x6C: { uint8_t var = readEEPROM(addr+pc)-0x80; pc++; uint32_t val = readEEPROM(addr+pc) | (readEEPROM(addr+pc+1)<<8) | (readEEPROM(addr+pc+2)<<16) | (readEEPROM(addr+pc+3)<<24); pc+=4; uint16_t jumpAddr = readEEPROM(addr+pc) | (readEEPROM(addr+pc+1)<<8); pc+=2; if(var<16 && vars32[var]<val) pc=jumpAddr; break; }
    case 0x6D: { uint8_t var = readEEPROM(addr+pc)-0x80; pc++; uint32_t maxVal = readEEPROM(addr+pc) | (readEEPROM(addr+pc+1)<<8) | (readEEPROM(addr+pc+2)<<16) | (readEEPROM(addr+pc+3)<<24); pc+=4; if(var<16 && maxVal>0) vars32[var] = random(maxVal); break; }
    case 0x70: {
      uint8_t syscallId = readEEPROM(addr + pc); pc++;
      uint8_t argCount = readEEPROM(addr + pc); pc++;
      uint8_t args[8];
      for (uint8_t i = 0; i < argCount && i < 8; i++) {
        args[i] = readEEPROM(addr + pc); pc++;
      }
      syscall(syscallId, args, argCount);
      break;
    }
  }
}

static const uint16_t noteFreqTable[] PROGMEM = {
  261,277,294,311,330,349,370,392,415,440,466,494,523,554,587,622,659,698,740,784,
  831,880,932,988,1047,1109,1175,1245,1319,1397,1480,1568,1661,1760,1865,1976,2093,
  2217,2349,2489,2637,2794,2960,3136,3322,3520,3729,3951,4186,4435,4699,4978,5274,
  5588,5920
};

int noteToFreq(uint8_t note) {
  uint8_t idx = note - 0x80;
  if (idx > 54) idx = 54;
  return pgm_read_word(&noteFreqTable[idx]);
}

char getCharAt(uint16_t index) {
  FileEntry e; readFileEntry(currentFileIdx, &e);
  return (char)readEEPROM(e.startBlock * BLOCK_SIZE + index);
}

void changeCurrentChar(int dir) {
  FileEntry e; readFileEntry(currentFileIdx, &e);
  uint16_t addr = e.startBlock * BLOCK_SIZE + currentCharIndex;
  uint8_t cur = readEEPROM(addr);
  if(dir>0) cur++; else cur--;
  writeEEPROM(addr, cur);
}

void buildProgram() {
  bool ok = true;
  uint8_t size = getFileSizeBlocks(currentFileIdx);
  FileEntry e; readFileEntry(currentFileIdx, &e);
  for (int i = 0; i < size; i++) {
    for (int j = 0; j < BLOCK_SIZE; j++) {
      uint8_t b = readEEPROM((e.startBlock + i) * BLOCK_SIZE + j);
      if (b == 0xFF) break;
      if (b >= 0x40 && b <= 0x5F) {
        if (b == 0x40 && j + 1 < BLOCK_SIZE) {
          uint8_t next = readEEPROM((e.startBlock + i) * BLOCK_SIZE + j + 1);
          if (next != 0x29 && next != 0x2A) { ok = false; break; }
        }
      }
    }
    if (!ok) break;
  }
  if (ok) { e.flags |= 0x01; writeFileEntry(currentFileIdx, &e); lcd.clear(); lcd.setCursor(0,0); lcd.print(F("Build OK!")); }
  else { e.flags &= ~0x01; writeFileEntry(currentFileIdx, &e); lcd.clear(); lcd.setCursor(0,0); lcd.print(F("Build ERR!")); }
  delay(1000); displayNeedsFullRedraw = true;
}

void playOMF() {
  uint8_t flat;
  if (!resolveSelected(&flat)) return;
  currentFileIdx = flat;
  currentState = PLAYER;
  displayNeedsFullRedraw = true;
  FileEntry e;
  readFileEntry(currentFileIdx, &e);
  uint16_t addr = e.startBlock * BLOCK_SIZE;
  for (uint16_t i = 0; i < e.sizeBlocks * BLOCK_SIZE; i++) {
    uint8_t note = readEEPROM(addr + i);
    if (note == 0x00) break;
    if (note >= 0x80 && note <= 0xB6) {
      i++;
      if (i >= e.sizeBlocks * BLOCK_SIZE) break;
      uint8_t dur = readEEPROM(addr + i);
      if (soundEnabled) {
        tone(BUZZER, noteToFreq(note), dur * 50);
        delay(dur * 50);
      }
    }
  }
  currentState = DISK;
  displayNeedsFullRedraw = true;
}

void writeBuiltinProgram(const char* name, const uint8_t* prog, uint16_t len, uint8_t blocks, uint8_t idx) {
  uint16_t start = findFreeBlock();
  if (start == 0xFFFF) return;
  FileEntry e;
  memset(e.name, ' ', FULLNAME_LEN);
  strncpy(e.name, name, FULLNAME_LEN);
  e.name[FULLNAME_LEN] = 0;
  e.startBlock = start;
  e.sizeBlocks = blocks;
  e.flags = 0x01;
  writeFileEntry(idx, &e);
  uint16_t total = blocks * BLOCK_SIZE;
  for (uint16_t i = 0; i < total; i++) {
    uint8_t b = (i < len) ? pgm_read_byte(prog + i) : 0x00;
    writeEEPROM(start * BLOCK_SIZE + i, b);
  }
}

static const uint8_t progInfo[] PROGMEM = {0x46, 0x70,0x26,0x00, 0x44,0x14, 0x41};
static const uint8_t progSettings[] PROGMEM = {0x46, 0x40,0x29,'S','e','t','t','i','n','g','s',0x00, 0x44,0x05, 0x70,0x28,0x00, 0x41};
static const uint8_t progClock[] PROGMEM = {0x46, 0x4D,0x00,0x00, 0x40,0x29,'T','i','m','e',0x00, 0x70,0x27,0x00, 0x4B,0x60, 0x41};
static const uint8_t progMelody[] PROGMEM = {0x46, 0x40,0x29,'M','e','l','o','d','y',0x00, 0x4E,0x80,0x02, 0x4E,0x84,0x02, 0x4E,0x87,0x02, 0x4E,0x8C,0x04, 0x4E,0x87,0x02, 0x4E,0x84,0x02, 0x4E,0x80,0x02, 0x4B,0x60, 0x41};
static const uint8_t progMem[] PROGMEM = {0x46, 0x4D,0x00,0x00, 0x40,0x29,'F','r','e','e',' ','R','A','M',0x00, 0x4D,0x00,0x01, 0x70,0x29,0x00, 0x4B,0x60, 0x41};
static const uint8_t progUptime[] PROGMEM = {0x46, 0x4D,0x00,0x00, 0x40,0x29,'U','p','t','i','m','e',0x00, 0x4D,0x00,0x01, 0x70,0x2A,0x00, 0x4B,0x60, 0x41};
static const uint8_t progTaskman[] PROGMEM = {0x70,0x2F,0x00, 0x41};
static const uint8_t progCalc[] PROGMEM = {0x43, 0x60, 0x00, 0x43, 0x61, 0x00, 0x61, 0x80, 0x00, 0x00, 0x00, 0x00, 0x61, 0x81, 0x00, 0x00, 0x00, 0x00, 0x61, 0x82, 0x00, 0x00, 0x00, 0x00, 0x61, 0x83, 0x01, 0x00, 0x00, 0x00, 0x46, 0x4D, 0x00, 0x00, 0x70, 0x01, 0x03, 0x41, 0x3D, 0x00, 0x68, 0x80, 0x4D, 0x08, 0x00, 0x70, 0x01, 0x03, 0x42, 0x3D, 0x00, 0x68, 0x81, 0x4D, 0x00, 0x01, 0x4A, 0x60, 0x3A, 0x00, 0x5B, 0x00, 0x4A, 0x60, 0x3A, 0x01, 0x65, 0x00, 0x4A, 0x60, 0x3A, 0x02, 0x6F, 0x00, 0x4A, 0x60, 0x3A, 0x03, 0x7A, 0x00, 0x70, 0x01, 0x05, 0x45, 0x58, 0x49, 0x54, 0x00, 0x2B, 0x82, 0x00, 0x70, 0x01, 0x04, 0x5B, 0x41, 0x5D, 0x00, 0x2B, 0x82, 0x00, 0x70, 0x01, 0x04, 0x5B, 0x42, 0x5D, 0x00, 0x2B, 0x82, 0x00, 0x70, 0x01, 0x05, 0x5B, 0x4F, 0x50, 0x5D, 0x00, 0x2B, 0x82, 0x00, 0x70, 0x01, 0x03, 0x53, 0x3D, 0x00, 0x68, 0x83, 0x70, 0x02, 0x01, 0x20, 0x4A, 0x61, 0x3A, 0x00, 0x9F, 0x00, 0x4A, 0x61, 0x3A, 0x01, 0xA6, 0x00, 0x4A, 0x61, 0x3A, 0x02, 0xAD, 0x00, 0x70, 0x02, 0x01, 0x2F, 0x2B, 0xB1, 0x00, 0x70, 0x02, 0x01, 0x2B, 0x2B, 0xB1, 0x00, 0x70, 0x02, 0x01, 0x2D, 0x2B, 0xB1, 0x00, 0x70, 0x02, 0x01, 0x2A, 0x70, 0x02, 0x01, 0x20, 0x68, 0x82, 0x70, 0x02, 0x01, 0x20, 0x69, 0x83, 0x01, 0x00, 0x00, 0x00, 0xD6, 0x00, 0x69, 0x83, 0x0A, 0x00, 0x00, 0x00, 0xDF, 0x00, 0x70, 0x01, 0x05, 0x78, 0x31, 0x30, 0x30, 0x00, 0x2B, 0xE6, 0x00, 0x70, 0x01, 0x03, 0x78, 0x31, 0x00, 0x2B, 0xE6, 0x00, 0x70, 0x01, 0x04, 0x78, 0x31, 0x30, 0x00, 0x4B, 0x62, 0x4A, 0x62, 0x3A, 0x01, 0xFD, 0x00, 0x4A, 0x62, 0x3A, 0x02, 0x5E, 0x01, 0x4A, 0x62, 0x3A, 0x04, 0xBF, 0x01, 0x2B, 0x1E, 0x00, 0x4A, 0x60, 0x3A, 0x00, 0x16, 0x01, 0x4A, 0x60, 0x3A, 0x01, 0x1C, 0x01, 0x4A, 0x60, 0x3A, 0x02, 0x22, 0x01, 0x4A, 0x60, 0x3A, 0x03, 0x33, 0x01, 0x41, 0x62, 0x80, 0x83, 0x2B, 0xD0, 0x01, 0x62, 0x81, 0x83, 0x2B, 0xD0, 0x01, 0x48, 0x61, 0x4A, 0x61, 0x3A, 0x04, 0x2D, 0x01, 0x2B, 0xD0, 0x01, 0x43, 0x61, 0x00, 0x2B, 0xD0, 0x01, 0x69, 0x83, 0x01, 0x00, 0x00, 0x00, 0x4C, 0x01, 0x69, 0x83, 0x0A, 0x00, 0x00, 0x00, 0x55, 0x01, 0x61, 0x83, 0x01, 0x00, 0x00, 0x00, 0x2B, 0xD0, 0x01, 0x61, 0x83, 0x0A, 0x00, 0x00, 0x00, 0x2B, 0xD0, 0x01, 0x61, 0x83, 0x64, 0x00, 0x00, 0x00, 0x2B, 0xD0, 0x01, 0x4A, 0x60, 0x3A, 0x00, 0x77, 0x01, 0x4A, 0x60, 0x3A, 0x01, 0x7D, 0x01, 0x4A, 0x60, 0x3A, 0x02, 0x83, 0x01, 0x4A, 0x60, 0x3A, 0x03, 0x94, 0x01, 0x41, 0x63, 0x80, 0x83, 0x2B, 0xD0, 0x01, 0x63, 0x81, 0x83, 0x2B, 0xD0, 0x01, 0x4A, 0x61, 0x3A, 0x00, 0x8E, 0x01, 0x49, 0x61, 0x2B, 0xD0, 0x01, 0x43, 0x61, 0x03, 0x2B, 0xD0, 0x01, 0x69, 0x83, 0x01, 0x00, 0x00, 0x00, 0xAD, 0x01, 0x69, 0x83, 0x0A, 0x00, 0x00, 0x00, 0xB6, 0x01, 0x61, 0x83, 0x0A, 0x00, 0x00, 0x00, 0x2B, 0xD0, 0x01, 0x61, 0x83, 0x64, 0x00, 0x00, 0x00, 0x2B, 0xD0, 0x01, 0x61, 0x83, 0x01, 0x00, 0x00, 0x00, 0x2B, 0xD0, 0x01, 0x48, 0x60, 0x4A, 0x60, 0x3A, 0x05, 0xCA, 0x01, 0x2B, 0xD0, 0x01, 0x43, 0x60, 0x00, 0x2B, 0xD0, 0x01, 0x61, 0x82, 0x00, 0x00, 0x00, 0x00, 0x62, 0x82, 0x80, 0x4A, 0x61, 0x3A, 0x00, 0xF1, 0x01, 0x4A, 0x61, 0x3A, 0x01, 0xF7, 0x01, 0x4A, 0x61, 0x3A, 0x02, 0xFD, 0x01, 0x65, 0x82, 0x81, 0x2B, 0x1E, 0x00, 0x62, 0x82, 0x81, 0x2B, 0x1E, 0x00, 0x63, 0x82, 0x81, 0x2B, 0x1E, 0x00, 0x64, 0x82, 0x81, 0x2B, 0x1E, 0x00};

void createBuiltinFiles() {
  uint8_t oldDisk = currentDisk;
  currentDisk = 0;
  uint8_t idx = 0;
  writeBuiltinProgram("INFO.OEF", progInfo, sizeof(progInfo), 1, idx++);
  writeBuiltinProgram("SETTINGS.OEF", progSettings, sizeof(progSettings), 4, idx++);
  writeBuiltinProgram("CLOCK.OEF", progClock, sizeof(progClock), 3, idx++);
  writeBuiltinProgram("CALC.OEF", progCalc, sizeof(progCalc), 65, idx++);
  writeBuiltinProgram("TASKMAN.OEF", progTaskman, sizeof(progTaskman), 1, idx++);
  writeBuiltinProgram("MELODY.OEF", progMelody, sizeof(progMelody), 5, idx++);
  writeBuiltinProgram("MEM.OEF", progMem, sizeof(progMem), 3, idx++);
  writeBuiltinProgram("UPTIME.OEF", progUptime, sizeof(progUptime), 3, idx++);
  currentDisk = oldDisk;
}
