#ifndef ORANGEOS_H
#define ORANGEOS_H

#include <Wire.h>
#include <EEPROM.h>
#include <string.h>
#include <stdlib.h>
#include <avr/pgmspace.h>

// ---------------- Tiny DS3231 RTC driver (replaces RTClib, no Adafruit_BusIO/SPI) ----------------
class DateTime {
public:
  uint16_t y; uint8_t mo, d, h, mi, s;
  DateTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
    : y(year), mo(month), d(day), h(hour), mi(minute), s(second) {}
  uint16_t year() const { return y; }
  uint8_t month() const { return mo; }
  uint8_t day() const { return d; }
  uint8_t hour() const { return h; }
  uint8_t minute() const { return mi; }
  uint8_t second() const { return s; }
};

class RTC_DS3231 {
  static uint8_t bcd2bin(uint8_t v) { return (uint8_t)(v - 6 * (v >> 4)); }
  static uint8_t bin2bcd(uint8_t v) { return (uint8_t)(v + 6 * (v >> 4)); }
  uint8_t readReg(uint8_t r) {
    Wire.beginTransmission((uint8_t)0x68); Wire.write(r); Wire.endTransmission();
    Wire.requestFrom((uint8_t)0x68, (uint8_t)1);
    return Wire.available() ? (uint8_t)Wire.read() : 0;
  }
  void writeReg(uint8_t r, uint8_t v) {
    Wire.beginTransmission(0x68); Wire.write(r); Wire.write(v); Wire.endTransmission();
  }
  void readBuf(uint8_t r, uint8_t* buf, uint8_t n) {
    Wire.beginTransmission((uint8_t)0x68); Wire.write(r); Wire.endTransmission();
    Wire.requestFrom((uint8_t)0x68, n);
    for (uint8_t i = 0; i < n && Wire.available(); i++) buf[i] = (uint8_t)Wire.read();
  }
  void writeBuf(uint8_t r, uint8_t* buf, uint8_t n) {
    Wire.beginTransmission(0x68); Wire.write(r);
    for (uint8_t i = 0; i < n; i++) Wire.write(buf[i]);
    Wire.endTransmission();
  }
public:
  bool begin() { Wire.begin(); return true; }
  bool lostPower() { return (readReg(0x0F) & 0x80) != 0; }
  void adjust(const DateTime& dt) {
    uint8_t buf[7];
    buf[0] = bin2bcd(dt.second());
    buf[1] = bin2bcd(dt.minute());
    buf[2] = bin2bcd(dt.hour());
    buf[3] = 0;
    buf[4] = bin2bcd(dt.day());
    buf[5] = bin2bcd(dt.month());
    buf[6] = bin2bcd((uint8_t)(dt.year() - 2000));
    writeBuf(0x00, buf, 7);
    uint8_t st = readReg(0x0F);
    writeReg(0x0F, st & 0x7F);
  }
  DateTime now() {
    uint8_t buf[7];
    readBuf(0x00, buf, 7);
    uint8_t ss = bcd2bin(buf[0] & 0x7F);
    uint8_t mm = bcd2bin(buf[1] & 0x7F);
    uint8_t hh = bcd2bin(buf[2] & 0x3F);
    uint8_t dd = bcd2bin(buf[4] & 0x3F);
    uint8_t mo = bcd2bin(buf[5] & 0x1F);
    uint16_t yr = bcd2bin(buf[6]);
    if (buf[5] & 0x80) yr += 100;
    yr += 2000;
    return DateTime(yr, mo, dd, hh, mm, ss);
  }
};

// ---------------- Tiny PCF8574 I2C LCD driver (replaces LiquidCrystal_I2C lib) ----------------
// Standard "backpack" wiring: D4..D7 = P4..P7, RS=P0, RW=P1, E=P2, BL=P3.
class LiquidCrystal_I2C : public Print {
  uint8_t _addr;
  uint8_t _cols, _rows;
  uint8_t _backlight;
  void expanderWrite(uint8_t v) {
    Wire.beginTransmission(_addr);
    Wire.write((uint8_t)(v | _backlight));
    Wire.endTransmission();
  }
  void pulseEnable(uint8_t v) {
    expanderWrite(v | 0x04);
    delayMicroseconds(1);
    expanderWrite(v & ~0x04);
    delayMicroseconds(50);
  }
  void write4bits(uint8_t v) { expanderWrite(v); pulseEnable(v); }
  void send(uint8_t value, uint8_t mode) {
    write4bits((uint8_t)((value & 0xF0) | mode));
    write4bits((uint8_t)(((value << 4) & 0xF0) | mode));
  }
  void command(uint8_t c) { send(c, 0); }
public:
  LiquidCrystal_I2C(uint8_t addr, uint8_t cols, uint8_t rows)
    : _addr(addr), _cols(cols), _rows(rows), _backlight(0x08) {}
  void init() { begin(); }
  void begin() {
    Wire.begin();
    _backlight = 0x08;
    delay(50);
    expanderWrite(_backlight);
    write4bits(0x30); delay(5);
    write4bits(0x30); delay(5);
    write4bits(0x30); delay(2);
    write4bits(0x20);
    command(0x28); // 4-bit, 2 lines, 5x8 font
    command(0x0C); // display on, cursor off, blink off
    command(0x06); // entry mode: increment, no shift
    command(0x01); delay(2);
    command(0x80);
  }
  void clear() { command(0x01); delay(2); }
  void home() { command(0x02); delay(2); }
  void display() { command(0x0C); }
  void noDisplay() { command(0x08); }
  void backlight() { _backlight = 0x08; expanderWrite(_backlight); }
  void noBacklight() { _backlight = 0x00; expanderWrite(_backlight); }
  void blink() { command(0x0D); }
  void noBlink() { command(0x0C); }
  void setCursor(uint8_t col, uint8_t row) {
    static const uint8_t row_off[] = {0x00, 0x40, 0x14, 0x54};
    if (row >= _rows) row = _rows - 1;
    command((uint8_t)(0x80 | (col + row_off[row])));
  }
  size_t write(uint8_t c) { send(c, 0x01); return 1; }
  using Print::write;
};

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

// ---------------- Connection (binary Serial protocol) ----------------
// Packet: MAGIC0 MAGIC1 CMD LEN_LO LEN_HI [PAYLOAD...] CRC
// CRC = XOR of (CMD, LEN_LO, LEN_HI, all payload bytes)
#define CONN_BAUD 9600
#define CONN_MAGIC0 0xAA
#define CONN_MAGIC1 0x55
#define CONN_CMD_PING   0x01
#define CONN_CMD_INFO   0x02
#define CONN_CMD_LIST   0x03
#define CONN_CMD_READ   0x04
#define CONN_CMD_WRITE  0x05
#define CONN_CMD_DELETE 0x06
#define CONN_CMD_CREATE 0x07
#define CONN_CMD_RUN    0x08
#define CONN_ST_OK   0x00
#define CONN_ST_ERR  0x01
#define CONN_ST_RO   0x02   // read-only (tried to write C:)
#define CONN_ST_NF   0x03   // not found / invalid
#define CONN_MAX_PAYLOAD 32

// ---------------- Power / sleep ----------------
#define SLEEP_TIMEOUT_MS 30000

// ---------------- Settings (reserved tail of internal EEPROM) ----------------
#define SETTINGS_EE_ADDR (INT_EEPROM_SIZE - 16)

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
extern bool sleeping;
extern unsigned long lastActivity;
extern bool pcConnected;
extern uint8_t execFileIdx;

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
void loadSettings();
void saveSettings();
void enterSleep();
void wakeFromSleep();
void connectionPoll();
void handleConnectionPacket(uint8_t cmd, uint8_t* payload, uint8_t len);
void connSend(uint8_t rcmd, const uint8_t* data, uint16_t len);
bool createFileNamed(uint8_t disk, const char* name, uint8_t blocks);

#endif
