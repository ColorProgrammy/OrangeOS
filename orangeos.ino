#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <EEPROM.h>

#define BTN1 2
#define BTN2 3
#define BTN3 4
#define BUZZER 5
#define EEPROM_ADDR 0x57
#define EXT_EEPROM_SIZE 4096
#define INT_EEPROM_SIZE 1024
#define FILE_TABLE_START 0
#define MAX_FILES_EXT 32
#define MAX_FILES_INT 16
#define NAME_LEN 8
#define EXT_LEN 3
#define FULLNAME_LEN 13
#define BLOCK_SIZE 8
#define FLAG_DIR 0x80
#define ROOT 0xFF
#define FS_MAGIC 0x5A
#define INT_DATA_START_BLOCK ((MAX_FILES_INT * (FULLNAME_LEN + 5) + BLOCK_SIZE - 1) / BLOCK_SIZE)
#define EXT_DATA_START_BLOCK ((MAX_FILES_EXT * (FULLNAME_LEN + 5) + BLOCK_SIZE - 1) / BLOCK_SIZE)

LiquidCrystal_I2C lcd(0x27, 16, 2);
RTC_DS3231 rtc;

enum State { MAIN, SELECT_DISK, DISK, EDIT, VIEWER, SETTINGS, PLAYER, RENAME_ST, RUN_OEF, CONTEXT_MENU, INFO_SCREEN, TASKMAN };
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
  uint8_t parent;
};

uint8_t selectedFile = 0;
uint8_t currentDisk = 0;
uint8_t currentFileIdx = 255;
uint8_t currentDir = ROOT;
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

// Settings menu
#define SETTINGS_ITEMS 5
const char* settingsItems[] = {"Sound", "Set Time", "Erase Data", "System Info", "Back"};
uint8_t settingsIndex = 0;

void handleShort(uint8_t mask);
void handleVeryLongAction(uint8_t mask);
void handleExit(uint8_t mask);
void playToneForMask(uint8_t mask);
void redrawFullScreen();
void drawMainScreenFull();
void updateClockDisplay();
void drawSelectDiskFull();
void drawDiskFull();
void drawEditScreenFull();
void drawSettingsFull();
void drawViewerFull();
void drawPlayerFull();
void drawRenameFull();
void drawRunOefFull();
void drawContextMenuFull();
void drawInfoScreenFull();
void drawTaskmanFull();
bool getVisibleEntry(uint8_t pos, uint8_t* flatIdx);
uint8_t getDirCount();
bool resolveSelected(uint8_t* flat);
uint8_t getAppCount();
uint8_t getAppFile(uint8_t pos);
void readOnlyMsg();
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
void renameDelete();
void renameInsert();
void cyclicMove(uint8_t &pos, uint8_t max, bool forward);
void insertInstruction();
uint8_t getFileSizeBlocks(uint8_t idx);
void selectDisk(uint8_t disk);
uint8_t getMaxFiles();
uint16_t getDataStartBlock();
uint16_t getEepromSize();
void createBuiltinFiles();
void eraseInternalEeprom();
void showSystemInfo();
void adjustTime();
void print2(uint8_t v);
void print3(uint16_t v);
void printHex2(uint8_t v);
void printHex4(uint16_t v);
void printBlockTag(uint16_t b, const __FlashStringHelper* tag);

enum SyscallId {
    SYS_PRINT_STR = 1,
    SYS_PRINT_CHAR,
    SYS_PRINT_INT8,
    SYS_PRINT_INT16,
    SYS_PRINT_INT32,
    SYS_CLS,
    SYS_LOCATE,
    SYS_GETBTN,
    SYS_DELAY_MS,
    SYS_TONE,
    SYS_EXIT,
    SYS_GET_VERSION,
    SYS_GET_FILE_COUNT,
    SYS_GET_USED_SPACE,
    SYS_GET_FREE_SPACE,
    SYS_GET_UPTIME,
    SYS_GET_FREE_RAM,
    SYS_PRINT_STR_PGM,
    SYS_GET_FILENAME,
    SYS_GET_FILE_SIZE,
    SYS_GET_FILE_FLAGS,
    SYS_READ_BYTE,
    SYS_WRITE_BYTE,
    SYS_CREATE_FILE,
    SYS_DELETE_FILE,
    SYS_RENAME_FILE,
    SYS_RUN_OEF,
    SYS_GET_KEY,
    SYS_GET_EEPROM_FREE,
    SYS_SWITCH_DISK,
    SYS_GET_CURRENT_DISK,
    SYS_TOGGLE_SOUND,
    SYS_GET_FILE_COUNT_DISK,
    SYS_GET_FILENAME_DISK,
    SYS_GET_FREE_SPACE_DISK,
    SYS_GET_USED_SPACE_DISK,
    SYS_SET_PRIVILEGE,
    SYS_SHOW_INFO,
    SYS_SHOW_TIME,
    SYS_OPEN_SETTINGS,
    SYS_SHOW_RAM,
    SYS_SHOW_UPTIME,
    SYS_MKDIR,
    SYS_CHDIR,
    SYS_LS,
    SYS_CD_ROOT,
    SYS_SHOW_TASKS,
};

uint8_t currentPrivilege = 0;

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
                if (e.flags & FLAG_DIR) *(uint16_t*)(&args[1]) = 0;
                else *(uint16_t*)(&args[1]) = e.sizeBlocks * BLOCK_SIZE;
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
                if (e.flags & FLAG_DIR) break;
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
                if (e.flags & FLAG_DIR) break;
                uint16_t addr = e.startBlock * BLOCK_SIZE + pos;
                writeEEPROM(addr, byte);
            }
        } break;
        case SYS_CREATE_FILE: if (args) {
            if (isSystemDisk()) break;
            char* name = (char*)args;
            uint8_t blocks = args[strlen(name) + 1];
            uint8_t idx = 0xFF;
            for (uint8_t i = 0; i < getMaxFiles(); i++) {
                if (isFreeEntry(i)) { idx = i; break; }
            }
            if (idx != 0xFF) {
                uint16_t start = findFreeBlock();
                if (start != 0xFFFF) {
                    FileEntry e;
                    memset(e.name, ' ', FULLNAME_LEN);
                    strncpy(e.name, name, FULLNAME_LEN);
                    e.name[FULLNAME_LEN] = 0;
                    e.startBlock = start;
                    e.sizeBlocks = blocks;
                    e.flags = (strstr(name, ".OEF") ? 0x01 : 0x00);
                    e.parent = ROOT;
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
            if (idx < getMaxFiles()) {
                FileEntry e; readFileEntry(idx, &e);
                if (e.flags & FLAG_DIR) break;
                clearFileEntry(idx);
            }
        } break;
        case SYS_RENAME_FILE: if (args) {
            if (isSystemDisk()) break;
            uint8_t idx = args[0];
            char* newName = (char*)&args[1];
            if (idx < getMaxFiles()) {
                FileEntry e; readFileEntry(idx, &e);
                if (e.flags & FLAG_DIR) break;
                strncpy(e.name, newName, FULLNAME_LEN);
                e.name[FULLNAME_LEN] = 0;
                writeFileEntry(idx, &e);
            }
        } break;
        case SYS_RUN_OEF: if (args) {
            uint8_t idx = args[0];
            if (idx < getMaxFiles()) {
                FileEntry e; readFileEntry(idx, &e);
                if (e.flags & FLAG_DIR) break;
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
        case SYS_MKDIR: if (args) {
            if (isSystemDisk()) break;
            char* name = (char*)args;
            uint8_t idx = 0xFF;
            for (uint8_t i = 0; i < getMaxFiles(); i++) {
                if (isFreeEntry(i)) { idx = i; break; }
            }
            if (idx != 0xFF) {
                FileEntry e;
                memset(e.name, ' ', FULLNAME_LEN);
                strncpy(e.name, name, FULLNAME_LEN);
                e.name[FULLNAME_LEN] = 0;
                e.startBlock = 0xFFFF;
                e.sizeBlocks = 0;
                e.flags = FLAG_DIR;
                e.parent = currentDir;
                writeFileEntry(idx, &e);
            }
            args[0] = idx;
        } break;
        case SYS_CHDIR: if (args) {
            int8_t dir = args[0];
            if (dir < 0) { currentDir = ROOT; break; }
            if (dir < getMaxFiles()) {
                FileEntry e; readFileEntry(dir, &e);
                if (e.flags & FLAG_DIR) currentDir = dir;
            }
        } break;
        case SYS_LS: if (args) {
            uint8_t pos = 0;
            args[1] = 0;
            if (currentDir != ROOT) {
                if (pos == args[0]) { strcpy((char*)&args[2], ".."); args[1] = 1; break; }
                pos++;
            }
            for (uint8_t i = 0; i < getMaxFiles() && !args[1]; i++) {
                if (isFreeEntry(i)) continue;
                FileEntry e; readFileEntry(i, &e);
                if (e.parent != currentDir) continue;
                if (pos == args[0]) {
                    strcpy((char*)&args[2], getFileName(i));
                    args[1] = e.flags & FLAG_DIR ? 1 : 0;
                }
                pos++;
            }
        } break;
        case SYS_CD_ROOT: currentDir = ROOT; break;
        case SYS_SHOW_TASKS: {
            oefRunning = false; oefPaused = false;
            currentDisk = 0;
            currentDir = ROOT;
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

const uint8_t opcodeArgBytes[] PROGMEM = {
  0, 0, 0, 0, 2, 2, 1, 3, 0, 0, 1, 1, 4,
  1, 2, 2, 2, 2, 2, 2, 2, 1,
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
  uint16_t addr = FILE_TABLE_START + idx * (FULLNAME_LEN + 5);
  for (uint8_t i = 0; i < FULLNAME_LEN; i++) entry->name[i] = readEEPROM(addr + i);
  entry->name[FULLNAME_LEN] = 0;
  entry->startBlock = readEEPROM(addr + FULLNAME_LEN) | (readEEPROM(addr + FULLNAME_LEN + 1) << 8);
  entry->sizeBlocks = readEEPROM(addr + FULLNAME_LEN + 2);
  entry->flags = readEEPROM(addr + FULLNAME_LEN + 3);
  entry->parent = readEEPROM(addr + FULLNAME_LEN + 4);
}

void writeFileEntry(uint8_t idx, const FileEntry* entry) {
  uint16_t addr = FILE_TABLE_START + idx * (FULLNAME_LEN + 5);
  for (uint8_t i = 0; i < FULLNAME_LEN; i++) writeEEPROM(addr + i, entry->name[i]);
  writeEEPROM(addr + FULLNAME_LEN, entry->startBlock & 0xFF);
  writeEEPROM(addr + FULLNAME_LEN + 1, entry->startBlock >> 8);
  writeEEPROM(addr + FULLNAME_LEN + 2, entry->sizeBlocks);
  writeEEPROM(addr + FULLNAME_LEN + 3, entry->flags);
  writeEEPROM(addr + FULLNAME_LEN + 4, entry->parent);
}

uint8_t getFileCount() {
  uint8_t count = 0;
  uint8_t max = getMaxFiles();
  for (uint8_t i = 0; i < max; i++) {
    if (readEEPROM(FILE_TABLE_START + i * (FULLNAME_LEN + 5)) != 0xFF) count++;
  }
  return count;
}

bool isFreeEntry(uint8_t idx) {
  return readEEPROM(FILE_TABLE_START + idx * (FULLNAME_LEN + 5)) == 0xFF;
}

uint16_t getUsedBytes() {
  uint16_t used = 0;
  for (uint8_t i = 0; i < getMaxFiles(); i++) {
    if (isFreeEntry(i)) continue;
    FileEntry e; readFileEntry(i, &e);
    if (e.flags & FLAG_DIR) continue;
    used += e.sizeBlocks * BLOCK_SIZE;
  }
  return used;
}

uint16_t getFreeBytes() {
  uint16_t total = (getEepromSize() / BLOCK_SIZE - 1 - getDataStartBlock()) * BLOCK_SIZE;
  uint16_t used = getUsedBytes();
  return (used > total) ? 0 : (total - used);
}

uint16_t getFsMagicAddr() {
  return getEepromSize() - 1;
}

void writeDirEntry(const char* name, uint8_t idx) {
  FileEntry e;
  memset(e.name, ' ', FULLNAME_LEN);
  strncpy(e.name, name, FULLNAME_LEN);
  e.name[FULLNAME_LEN] = 0;
  e.startBlock = 0xFFFF;
  e.sizeBlocks = 0;
  e.flags = FLAG_DIR;
  e.parent = ROOT;
  writeFileEntry(idx, &e);
}

void initDiskLayout() {
  writeEEPROM(getFsMagicAddr(), FS_MAGIC);
  uint8_t idx = 0;
  writeDirEntry("SYSTEM", idx++);
  writeDirEntry("APPS", idx++);
  writeDirEntry("USER", idx++);
  createDefaultFile();
  createHelloOef();
  createHiOef();
}

uint8_t getVisibleCount(uint8_t parent) {
  uint8_t c = 0;
  for (uint8_t i = 0; i < getMaxFiles(); i++) {
    if (isFreeEntry(i)) continue;
    FileEntry e; readFileEntry(i, &e);
    if (e.parent == parent) c++;
  }
  return c;
}

uint8_t getDirCount() {
  return getVisibleCount(currentDir) + (currentDir != ROOT ? 1 : 0);
}

bool getVisibleEntry(uint8_t pos, uint8_t* flatIdx) {
  if (currentDir != ROOT) {
    if (pos == 0) return false;
    pos--;
  }
  uint8_t seen = 0;
  for (uint8_t i = 0; i < getMaxFiles(); i++) {
    if (isFreeEntry(i)) continue;
    FileEntry e; readFileEntry(i, &e);
    if (e.parent == currentDir) {
      if (seen == pos) { *flatIdx = i; return true; }
      seen++;
    }
  }
  return false;
}

bool isSystemDisk() { return currentDisk == 0; }

void readOnlyMsg() {
  lcd.clear(); lcd.setCursor(0,0); lcd.print(F("C: read only"));
  delay(700); displayNeedsFullRedraw = true;
}

void clearFileEntry(uint8_t idx) {
  uint16_t addr = FILE_TABLE_START + idx * (FULLNAME_LEN + 5);
  for (uint8_t i = 0; i < FULLNAME_LEN + 5; i++) writeEEPROM(addr + i, 0xFF);
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
  for (uint16_t i = start; i < maxBlocks - 1; i++) {
    bool used = false;
    for (uint8_t j = 0; j < getMaxFiles(); j++) {
      if (isFreeEntry(j)) continue;
      FileEntry e; readFileEntry(j, &e);
      if (e.flags & FLAG_DIR) continue;
      if (i >= e.startBlock && i < e.startBlock + e.sizeBlocks) { used = true; break; }
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
  for (int i = 0; i < INT_EEPROM_SIZE; i++) {
    EEPROM.update(i, 0xFF);
  }
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

void setup() {
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  lcd.init(); lcd.backlight();
  Wire.begin();
  randomSeed(analogRead(A0));

  if (!rtc.begin()) {
    lcd.setCursor(0,0); lcd.print(F("RTC error")); while (1);
  }
  if (rtc.lostPower()) {
    DateTime now = rtc.now();
    if (now.year() < 2020) rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  currentDisk = 0;
  eraseInternalEeprom();
  if (getFileCount() == 0) {
    createBuiltinFiles();
  }
  currentDisk = 1;
  if (readEEPROM(getFsMagicAddr()) != FS_MAGIC) {
    initDiskLayout();
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
      if (mask == 1) { currentState = SELECT_DISK; displayNeedsFullRedraw = true; }
      else if (mask == 4) { currentState = CONTEXT_MENU; contextMenuIndex = 0; displayNeedsFullRedraw = true; }
      break;
    case SELECT_DISK:
      if (mask == 1) { selectDisk(0); currentState = DISK; selectedFile = 0; displayNeedsFullRedraw = true; }
      else if (mask == 2) { selectDisk(1); currentState = DISK; selectedFile = 0; displayNeedsFullRedraw = true; }
      else if (mask == 3 || mask == 7) { currentState = MAIN; displayNeedsFullRedraw = true; }
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
      uint8_t total = getDirCount();
      if (total == 0) {
        if (mask == 3) createFile();
        break;
      }
      if (mask == 1) openFile();
      else if (mask == 2) editFile();
      else if (mask == 4) fileInfo();
      else if (mask == 3) createFile();
      else if (mask == 5) { if (selectedFile > 0) selectedFile--; else selectedFile = total - 1; displayNeedsFullRedraw = true; }
      else if (mask == 6) { if (selectedFile < total - 1) selectedFile++; else selectedFile = 0; displayNeedsFullRedraw = true; }
      break;
    }
    case TASKMAN: {
      uint8_t n = getAppCount();
      if (n == 0) break;
      if (mask == 1) {
        uint8_t flat = getAppFile(taskmanIndex);
        if (flat != 255) {
          currentDisk = 0;
          currentDir = ROOT;
          currentFileIdx = flat;
          startOEF();
        }
      } else if (mask == 5) { if (taskmanIndex > 0) taskmanIndex--; else taskmanIndex = n - 1; displayNeedsFullRedraw = true; }
      else if (mask == 6) { if (taskmanIndex < n - 1) taskmanIndex++; else taskmanIndex = 0; displayNeedsFullRedraw = true; }
      else if (mask == 3 || mask == 7) { currentState = MAIN; displayNeedsFullRedraw = true; }
      break;
    }
    case VIEWER:
      if (mask == 1) viewerPrevPage(); else if (mask == 2) viewerNextPage();
      else if (mask == 4) { currentState = DISK; displayNeedsFullRedraw = true; }
      else if (mask == 5) viewerPrevLine(); else if (mask == 6) viewerNextLine();
      break;
    case EDIT:
      if (getCurrentExtension() == EXT_TXT) {
        if (mask == 1) changeCurrentChar(1);
        else if (mask == 2) changeCurrentChar(-1);
        else if (mask == 4) { saveBlock(currentBlock); currentState = DISK; displayNeedsFullRedraw = true; }
        else if (mask == 3) { currentCharIndex++; if (currentCharIndex >= BLOCK_SIZE * getFileSizeBlocks(currentFileIdx)) currentCharIndex = 0; displayNeedsFullRedraw = true; }
      } else {
        if (mask == 1) block[currentByte]++;
        else if (mask == 2) block[currentByte]--;
        else if (mask == 4) block[currentByte] = 0;
        else if (mask == 3) block[currentByte] += 0x55;
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
              if (f.flags & FLAG_DIR) continue;
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
        if (currentDir == ROOT && selectedFile == 0) break;
        uint8_t flat;
        if (!resolveSelected(&flat)) break;
        FileEntry e; readFileEntry(flat, &e);
        if (e.flags & FLAG_DIR) break;
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
          currentCharIndex--;
          if (currentCharIndex < 0) currentCharIndex = BLOCK_SIZE * getFileSizeBlocks(currentFileIdx) - 1;
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
  uint8_t total = getDirCount();
  if (total == 0) {
    lcd.setCursor(0,0); lcd.print(F("No files"));
    lcd.setCursor(0,1); lcd.print(F("1+2 to create"));
    return;
  }
  if (selectedFile >= total) selectedFile = total - 1;
  char buf[17];
  for (uint8_t line = 0; line < 2; line++) {
    uint8_t pos = (selectedFile + line) % total;
    buf[0] = 0;
    if (pos == 0 && currentDir != ROOT) {
      strcpy(buf, "<UP>");
    } else {
      uint8_t flat;
      if (getVisibleEntry(pos, &flat)) strcpy(buf, getFileName(flat));
    }
    lcd.setCursor(0, line);
    if (line == 0) { lcd.print('>'); lcd.print(buf); }
    else { lcd.print(' '); lcd.print(buf); }
  }
}

uint8_t getAppCount() {
  uint8_t oldDisk = currentDisk;
  currentDisk = 0;
  uint8_t c = 0;
  for (uint8_t i = 0; i < getMaxFiles(); i++) {
    if (isFreeEntry(i)) continue;
    FileEntry e; readFileEntry(i, &e);
    if (e.flags & FLAG_DIR) continue;
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
    if (e.flags & FLAG_DIR) continue;
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
          uint16_t addr = block[currentByte] | (block[currentByte+1] << 8);
          printBlockTag(currentBlock, F("ADDR"));
          printHex4(addr);
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
  lcd.setCursor(0,0); lcd.print(F("View:")); lcd.print(getFileName(currentFileIdx));
  lcd.setCursor(0,1); lcd.print(F("1/2-pg 3-exit"));
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

bool resolveSelected(uint8_t* flat) {
  if (currentDir != ROOT && selectedFile == 0) return false;
  return getVisibleEntry(selectedFile, flat);
}

void openFile() {
  if (getDirCount() == 0) return;
  uint8_t flat;
  if (!resolveSelected(&flat)) {
    if (currentDir != ROOT) {
      FileEntry d; readFileEntry(currentDir, &d);
      currentDir = d.parent;
    }
    selectedFile = 0; displayNeedsFullRedraw = true; return;
  }
  FileEntry e; readFileEntry(flat, &e);
  if (e.flags & FLAG_DIR) { currentDir = flat; selectedFile = 0; displayNeedsFullRedraw = true; return; }
  currentFileIdx = flat;
  Extension ext = getCurrentExtension();
  if (ext == EXT_OEF) startOEF();
  else if (ext == EXT_OMF) playOMF();
  else currentState = VIEWER;
  displayNeedsFullRedraw = true;
}

void editFile() {
  if (getDirCount() == 0) return;
  if (isSystemDisk()) { readOnlyMsg(); return; }
  uint8_t flat;
  if (!resolveSelected(&flat)) {
    if (currentDir != ROOT) {
      FileEntry d; readFileEntry(currentDir, &d);
      currentDir = d.parent;
    }
    selectedFile = 0; displayNeedsFullRedraw = true; return;
  }
  FileEntry e; readFileEntry(flat, &e);
  if (e.flags & FLAG_DIR) { currentDir = flat; selectedFile = 0; displayNeedsFullRedraw = true; return; }
  currentFileIdx = flat;
  Extension ext = getCurrentExtension();
  if (ext == EXT_TXT) { currentBlock = 0; loadBlock(currentBlock); currentCharIndex = 0; }
  else { currentBlock = 0; loadBlock(currentBlock); currentByte = 0; }
  currentState = EDIT;
  displayNeedsFullRedraw = true;
}

void fileInfo() {
  if (getDirCount() == 0) return;
  uint8_t flat;
  if (!resolveSelected(&flat)) return;
  FileEntry e; readFileEntry(flat, &e);
  lcd.clear(); lcd.setCursor(0,0); lcd.print(e.name);
  lcd.setCursor(0,1);
  if (e.flags & FLAG_DIR) lcd.print(F("<DIR>"));
  else {
    uint16_t fileBytes = e.sizeBlocks * BLOCK_SIZE;
    lcd.print(F("Sz:"));
    lcd.print(fileBytes);
    lcd.print(F(" B"));
  }
  delay(1500); displayNeedsFullRedraw = true;
}

void createFile() {
  if (isSystemDisk()) { readOnlyMsg(); return; }
  uint8_t idx = 0xFF;
  for (uint8_t i = 0; i < getMaxFiles(); i++) {
    if (isFreeEntry(i)) { idx = i; break; }
  }
  if (idx == 0xFF) { lcd.clear(); lcd.setCursor(0,0); lcd.print(F("Max files")); delay(1000); return; }
  char ext[4]; chooseExtensionDialog(ext);
  char newName[FULLNAME_LEN+1]; strcpy(newName, "NEWFILE."); strcat(newName, ext);
  uint16_t start = findFreeBlock();
  if (start == 0xFFFF) { lcd.clear(); lcd.setCursor(0,0); lcd.print(F("Disk full!")); delay(1000); return; }
  FileEntry newEntry;
  memset(newEntry.name, ' ', FULLNAME_LEN);
  strcpy(newEntry.name, newName);
  newEntry.startBlock = start;
  newEntry.sizeBlocks = 1;
  newEntry.flags = (strcmp(ext, "OEF") == 0) ? 0x01 : 0x00;
  newEntry.parent = currentDir;
  writeFileEntry(idx, &newEntry);
  for (int i = 0; i < BLOCK_SIZE; i++) writeEEPROM(start * BLOCK_SIZE + i, 0x00);
  selectedFile = getDirCount() - 1;
  lcd.clear(); lcd.setCursor(0,0); lcd.print(F("Created")); delay(500);
  displayNeedsFullRedraw = true;
}

void deleteFile() {
  if (isSystemDisk()) { readOnlyMsg(); return; }
  uint8_t flat;
  if (!resolveSelected(&flat)) return;
  FileEntry e; readFileEntry(flat, &e);
  if (e.flags & FLAG_DIR) return;
  clearFileEntry(flat);
  uint8_t total = getDirCount();
  if (selectedFile > 0) selectedFile--;
  if (selectedFile >= total && total > 0) selectedFile = total - 1;
  lcd.clear(); lcd.setCursor(0,0); lcd.print(F("Deleted")); delay(500);
  displayNeedsFullRedraw = true;
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
  currentDir = ROOT;
  currentState = MAIN; displayNeedsFullRedraw = true;
}

void viewerPrevPage() {}
void viewerNextPage() {}
void viewerPrevLine() {}
void viewerNextLine() {}

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
  uint16_t addr = e.startBlock * BLOCK_SIZE;
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
        while (true) {
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

void playOMF() {
  uint8_t flat;
  if (!resolveSelected(&flat)) return;
  currentFileIdx = flat; currentState = PLAYER; displayNeedsFullRedraw = true;
  FileEntry e; readFileEntry(currentFileIdx, &e);
  uint16_t addr = e.startBlock * BLOCK_SIZE;
  for (uint16_t i=0; i<e.sizeBlocks*BLOCK_SIZE; i++) {
    uint8_t note = readEEPROM(addr+i); if (note==0x00) break;
    if (note>=0x80 && note<=0xB6) { i++; if (i>=e.sizeBlocks*BLOCK_SIZE) break;
      uint8_t dur = readEEPROM(addr+i); if (soundEnabled) { tone(BUZZER, noteToFreq(note), dur*50); delay(dur*50); } }
  }
  currentState = DISK; displayNeedsFullRedraw = true;
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

void selectDisk(uint8_t disk) {
  currentDisk = disk;
  currentDir = ROOT;
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
  e.parent = ROOT;
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
  e.parent = ROOT;
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
  e.parent = ROOT;
  writeFileEntry(idx, &e);
  uint8_t prog[] = {0x40, 0x29, 'H','I', 0x00, 0x47, 0x41};
  for (int i=0; i<sizeof(prog); i++) writeEEPROM(start * BLOCK_SIZE + i, prog[i]);
  for (int i=sizeof(prog); i<BLOCK_SIZE; i++) writeEEPROM(start * BLOCK_SIZE + i, 0x00);
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
  e.parent = ROOT;
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
