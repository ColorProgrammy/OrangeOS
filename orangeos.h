#ifndef ORANGEOS_H
#define ORANGEOS_H

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <EEPROM.h>
#include <string.h>

// ---------------- Hardware / layout ----------------
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
#define FILE_ENTRY_SIZE (FULLNAME_LEN + 5)
#define INT_DATA_START_BLOCK ((MAX_FILES_INT * FILE_ENTRY_SIZE + BLOCK_SIZE - 1) / BLOCK_SIZE)
#define EXT_DATA_START_BLOCK ((MAX_FILES_EXT * FILE_ENTRY_SIZE + BLOCK_SIZE - 1) / BLOCK_SIZE)

#define SETTINGS_ITEMS 5

// ---------------- Timing ----------------
const unsigned long LONG_MS = 800;
const unsigned long VERY_LONG_MS = 1500;
const unsigned long EXIT_MS = 2000;
const unsigned long DEBOUNCE_MS = 30;

// ---------------- Enums ----------------
enum State { MAIN, SELECT_DISK, DISK, EDIT, VIEWER, SETTINGS, PLAYER, RENAME_ST, RUN_OEF, CONTEXT_MENU, INFO_SCREEN, TASKMAN };
enum Extension { EXT_TXT, EXT_OEF, EXT_OMF, EXT_UNKNOWN };
enum SyscallId {
    SYS_PRINT_STR = 1, SYS_PRINT_CHAR, SYS_PRINT_INT8, SYS_PRINT_INT16, SYS_PRINT_INT32,
    SYS_CLS, SYS_LOCATE, SYS_GETBTN, SYS_DELAY_MS, SYS_TONE, SYS_EXIT,
    SYS_GET_VERSION, SYS_GET_FILE_COUNT, SYS_GET_USED_SPACE, SYS_GET_FREE_SPACE,
    SYS_GET_UPTIME, SYS_GET_FREE_RAM, SYS_PRINT_STR_PGM, SYS_GET_FILENAME, SYS_GET_FILE_SIZE,
    SYS_GET_FILE_FLAGS, SYS_READ_BYTE, SYS_WRITE_BYTE, SYS_CREATE_FILE, SYS_DELETE_FILE,
    SYS_RENAME_FILE, SYS_RUN_OEF, SYS_GET_KEY, SYS_GET_EEPROM_FREE, SYS_SWITCH_DISK,
    SYS_GET_CURRENT_DISK, SYS_TOGGLE_SOUND, SYS_GET_FILE_COUNT_DISK, SYS_GET_FILENAME_DISK,
    SYS_GET_FREE_SPACE_DISK, SYS_GET_USED_SPACE_DISK, SYS_SET_PRIVILEGE, SYS_SHOW_INFO,
    SYS_SHOW_TIME, SYS_OPEN_SETTINGS, SYS_SHOW_RAM, SYS_SHOW_UPTIME, SYS_SHOW_TASKS
};

struct FileEntry {
  char name[FULLNAME_LEN + 1];
  uint16_t startBlock;
  uint8_t sizeBlocks;
  uint8_t flags;
};

// ---------------- Globals (defined in orangeos.ino) ----------------
extern LiquidCrystal_I2C lcd;
extern RTC_DS3231 rtc;
extern State currentState;
extern State prevState;
extern bool soundEnabled;
extern uint8_t selectedFile;
extern uint8_t currentDisk;
extern uint8_t currentFileIdx;
extern uint8_t renameTarget;
extern uint8_t taskmanIndex;
extern uint16_t currentBlock;
extern uint8_t currentByte;
extern uint8_t block[BLOCK_SIZE];
extern bool displayNeedsFullRedraw;
extern uint8_t currentCharIndex;
extern int8_t vars8[32];
extern uint16_t vars16[8];
extern uint32_t vars32[16];
extern uint16_t pc;
extern bool oefRunning;
extern bool oefPaused;
extern unsigned long oefDelayUntil;
extern char renameBuffer[FULLNAME_LEN + 1];
extern uint8_t renamePos;
extern bool renameJustEntered;
extern uint8_t contextMenuIndex;
extern const char* contextItems[];
extern const uint8_t contextCount;
extern const char* greetings[];
extern const uint8_t greetCount;
extern uint8_t currentGreeting;
extern const char* settingsItems[];
extern uint8_t settingsIndex;
extern uint8_t currentPrivilege;
extern uint16_t viewerOffset;

// ---------------- VM tables (defined in vm.ino) ----------------
extern const char* const opcodeMnemonics[] PROGMEM;
extern const uint8_t opcodeCodes[] PROGMEM;
extern const uint8_t opcodeArgBytes[] PROGMEM;
extern const uint8_t opcodeCount;

// ---------------- Function prototypes ----------------
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
uint8_t getFileCount();
uint16_t getUsedBytes();
uint16_t getFreeBytes();
bool isSystemDisk();
void readFileEntry(uint8_t idx, FileEntry* entry);
void writeFileEntry(uint8_t idx, const FileEntry* entry);
bool isFreeEntry(uint8_t idx);
void clearFileEntry(uint8_t idx);
void deleteFileEntry(uint8_t idx);
bool resolveSelected(uint8_t* flat);
uint16_t getFreeRam();
void syscall(uint8_t id, uint8_t* args, uint8_t argCount);
int8_t getOpcodeIndex(uint8_t byte);
char nextTextChar(char c);
char prevTextChar(char c);
void writeCharAt(uint16_t index, char c);
void initDiskLayout();
int strcasecmp_local(const char* a, const char* b);

#endif
