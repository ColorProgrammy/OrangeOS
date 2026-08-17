#include "orangeos.h"

// ===================== CONNECTION (binary Serial link to PC) =====================
// Packet: AA 55 CMD LEN_LO LEN_HI [PAYLOAD] CRC
// CRC = XOR(CMD, LEN_LO, LEN_HI, payload...)

void connSend(uint8_t rcmd, const uint8_t* data, uint16_t len) {
  Serial.write(CONN_MAGIC0);
  Serial.write(CONN_MAGIC1);
  Serial.write(rcmd);
  Serial.write(len & 0xFF);
  Serial.write(len >> 8);
  uint8_t crc = rcmd ^ (uint8_t)(len & 0xFF) ^ (uint8_t)(len >> 8);
  for (uint16_t i = 0; i < len; i++) {
    Serial.write(data[i]);
    crc ^= data[i];
  }
  Serial.write(crc);
}

static void connSendStatus(uint8_t cmd, uint8_t st) {
  uint8_t d[1] = { st };
  connSend(cmd | 0x80, d, 1);
}

void connectionPoll() {
  static uint8_t state = 0;
  static uint8_t cmd = 0;
  static uint16_t len = 0;
  static uint16_t idx = 0;
  static uint8_t crc = 0;
  static uint8_t payload[CONN_MAX_PAYLOAD];

  while (Serial.available()) {
    uint8_t b = Serial.read();
    switch (state) {
      case 0: if (b == CONN_MAGIC0) state = 1; break;
      case 1: state = (b == CONN_MAGIC1) ? 2 : 0; break;
      case 2: cmd = b; state = 3; break;
      case 3: len = b; state = 4; break;
      case 4:
        len |= (uint16_t)b << 8;
        idx = 0;
        crc = cmd ^ (uint8_t)(len & 0xFF) ^ (uint8_t)(len >> 8);
        if (len == 0) state = 6;
        else if (len > CONN_MAX_PAYLOAD) state = 0; // reject oversized request
        else state = 5;
        break;
      case 5:
        if (idx < CONN_MAX_PAYLOAD) payload[idx] = b;
        crc ^= b; idx++;
        if (idx >= len) state = 6;
        break;
      case 6:
        if (b == crc) {
          pcConnected = true;
          lastActivity = millis();
          handleConnectionPacket(cmd, payload, (uint8_t)len);
        }
        state = 0;
        break;
      default: state = 0; break;
    }
  }
}

void handleConnectionPacket(uint8_t cmd, uint8_t* p, uint8_t len) {
  switch (cmd) {
    case CONN_CMD_PING: {
      uint8_t d[3] = { CONN_ST_OK, 1, 0 }; // version 1.0
      connSend(cmd | 0x80, d, 3);
      break;
    }
    case CONN_CMD_INFO: {
      uint8_t old = currentDisk; currentDisk = 1;
      uint8_t count = getFileCount();
      uint16_t used = getUsedBytes();
      uint16_t free = getFreeBytes();
      uint8_t d[8];
      d[0] = CONN_ST_OK;
      d[1] = free & 0xFF; d[2] = free >> 8;
      d[3] = used & 0xFF; d[4] = used >> 8;
      d[5] = count;
      d[6] = soundEnabled ? 1 : 0;
      d[7] = 1;
      connSend(cmd | 0x80, d, 8);
      currentDisk = old;
      break;
    }
    case CONN_CMD_LIST: {
      uint8_t old = currentDisk; currentDisk = 1;
      uint8_t count = getFileCount();
      uint16_t total = 1 + (uint16_t)count * 15;
      Serial.write(CONN_MAGIC0); Serial.write(CONN_MAGIC1);
      uint8_t rcmd = cmd | 0x80;
      Serial.write(rcmd);
      Serial.write(total & 0xFF); Serial.write(total >> 8);
      uint8_t crc = rcmd ^ (uint8_t)(total & 0xFF) ^ (uint8_t)(total >> 8);
      Serial.write(CONN_ST_OK); crc ^= CONN_ST_OK;
      for (uint8_t i = 0; i < count; i++) {
        FileEntry e; readFileEntry(i, &e);
        for (uint8_t j = 0; j < FULLNAME_LEN; j++) { Serial.write((uint8_t)e.name[j]); crc ^= (uint8_t)e.name[j]; }
        Serial.write(e.sizeBlocks); crc ^= e.sizeBlocks;
        Serial.write(e.flags); crc ^= e.flags;
      }
      Serial.write(crc);
      currentDisk = old;
      break;
    }
    case CONN_CMD_READ: {
      if (len < 3) { connSendStatus(cmd, CONN_ST_ERR); break; }
      uint8_t disk = p[0]; uint8_t fidx = p[1]; uint8_t blk = p[2];
      uint8_t old = currentDisk; currentDisk = disk;
      if (fidx >= getFileCount()) { connSendStatus(cmd, CONN_ST_NF); currentDisk = old; break; }
      FileEntry e; readFileEntry(fidx, &e);
      uint16_t addr = (e.startBlock + blk) * BLOCK_SIZE;
      uint8_t d[1 + BLOCK_SIZE];
      d[0] = CONN_ST_OK;
      for (uint8_t i = 0; i < BLOCK_SIZE; i++) d[1 + i] = readEEPROM(addr + i);
      connSend(cmd | 0x80, d, 1 + BLOCK_SIZE);
      currentDisk = old;
      break;
    }
    case CONN_CMD_WRITE: {
      if (len < 3 + BLOCK_SIZE) { connSendStatus(cmd, CONN_ST_ERR); break; }
      uint8_t disk = p[0]; uint8_t fidx = p[1]; uint8_t blk = p[2];
      uint8_t old = currentDisk; currentDisk = disk;
      if (disk == 0) { connSendStatus(cmd, CONN_ST_RO); currentDisk = old; break; }
      if (fidx >= getFileCount()) { connSendStatus(cmd, CONN_ST_NF); currentDisk = old; break; }
      FileEntry e; readFileEntry(fidx, &e);
      uint16_t addr = (e.startBlock + blk) * BLOCK_SIZE;
      for (uint8_t i = 0; i < BLOCK_SIZE; i++) writeEEPROM(addr + i, p[3 + i]);
      connSendStatus(cmd, CONN_ST_OK);
      displayNeedsFullRedraw = true;
      currentDisk = old;
      break;
    }
    case CONN_CMD_DELETE: {
      if (len < 2) { connSendStatus(cmd, CONN_ST_ERR); break; }
      uint8_t disk = p[0]; uint8_t fidx = p[1];
      uint8_t old = currentDisk; currentDisk = disk;
      if (disk == 0) { connSendStatus(cmd, CONN_ST_RO); currentDisk = old; break; }
      if (fidx >= getFileCount()) { connSendStatus(cmd, CONN_ST_NF); currentDisk = old; break; }
      deleteFileEntry(fidx);
      connSendStatus(cmd, CONN_ST_OK);
      displayNeedsFullRedraw = true;
      currentDisk = old;
      break;
    }
    case CONN_CMD_CREATE: {
      if (len < 1 + FULLNAME_LEN + 1) { connSendStatus(cmd, CONN_ST_ERR); break; }
      uint8_t disk = p[0];
      char name[FULLNAME_LEN + 1];
      for (uint8_t i = 0; i < FULLNAME_LEN; i++) name[i] = p[1 + i];
      name[FULLNAME_LEN] = 0;
      uint8_t blocks = p[1 + FULLNAME_LEN];
      uint8_t old = currentDisk; currentDisk = disk;
      if (disk == 0) { connSendStatus(cmd, CONN_ST_RO); currentDisk = old; break; }
      bool ok = createFileNamed(disk, name, blocks);
      connSendStatus(cmd, ok ? CONN_ST_OK : CONN_ST_ERR);
      displayNeedsFullRedraw = true;
      currentDisk = old;
      break;
    }
    case CONN_CMD_RUN: {
      if (len < 2) { connSendStatus(cmd, CONN_ST_ERR); break; }
      uint8_t disk = p[0]; uint8_t fidx = p[1];
      currentDisk = disk;
      if (fidx >= getFileCount()) { connSendStatus(cmd, CONN_ST_NF); break; }
      currentFileIdx = fidx;
      startOEF();              // leaves currentDisk = disk so the program runs from the right EEPROM
      connSendStatus(cmd, CONN_ST_OK);
      break;
    }
    case CONN_CMD_SETTIME: {
      if (len < 7) { connSendStatus(cmd, CONN_ST_ERR); break; }
      uint16_t yr = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
      uint8_t mo = p[2], d = p[3], h = p[4], mi = p[5], s = p[6];
      DateTime dt(yr, mo, d, h, mi, s);
      rtc.adjust(dt);
      connSendStatus(cmd, CONN_ST_OK);
      break;
    }
    default:
      connSendStatus(cmd, CONN_ST_ERR);
      break;
  }
}
