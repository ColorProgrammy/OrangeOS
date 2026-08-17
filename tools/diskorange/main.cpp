// DiskOrange - PC utility for OrangeOS (Connection protocol, disk D: only)
// Win32, C++98 (builds in Visual Studio 2008). No MFC, no resources.
//
// Build (VS2008 command prompt):
//   cl /EHsc DiskOrange.cpp user32.lib gdi32.lib kernel32.lib
//
// Or: create an empty "Win32 Project" (Windows application) in VS2008 and add
// this file. UI is built from child windows at runtime (no .rc needed).
// Protocol is documented in ../OrangeEmulator/PROTOCOL.txt
//
// All UI strings are plain ASCII on purpose (no em-dash / non-ASCII) so the
// text renders correctly regardless of the project's character-set setting.

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ---- control ids ----
#define ID_COMPORT    100
#define ID_CONNECT    101
#define ID_REFRESH    102
#define ID_LIST       103
#define ID_HEX        104
#define ID_LOAD       105
#define ID_SAVE       106
#define ID_CREATE     107
#define ID_DELETE     108
#define ID_RUN        109
#define ID_STATUS     110
#define ID_NEWNAME    111
#define ID_NEWBLOCKS  112
#define ID_BLOCKINFO  113

static HANDLE hCom = INVALID_HANDLE_VALUE;
static bool   connected = false;
static WORD    curBlocks = 0; // block count of the file loaded into the hex box
static BYTE    curIdx    = 0; // file index loaded into the hex box
static HFONT   hGui      = NULL; // UI font
static HFONT   hMono     = NULL; // hex editor font
static HBRUSH  hBg       = NULL; // dialog background brush

// ---- serial helpers ----
static DWORD readExact(HANDLE h, BYTE* buf, DWORD n) {
    DWORD total = 0;
    while (total < n) {
        DWORD got = 0;
        if (!ReadFile(h, buf + total, n - total, &got, NULL) || got == 0) return total;
        total += got;
    }
    return total;
}

static bool openPort(const char* port) {
    if (hCom != INVALID_HANDLE_VALUE) CloseHandle(hCom);
    hCom = CreateFileA(port, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hCom == INVALID_HANDLE_VALUE) return false;
    DCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    GetCommState(hCom, &dcb);
    dcb.BaudRate = 9600;
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    SetCommState(hCom, &dcb);
    COMMTIMEOUTS t;
    memset(&t, 0, sizeof(t));
    t.ReadIntervalTimeout       = 50;
    t.ReadTotalTimeoutConstant  = 400;
    t.ReadTotalTimeoutMultiplier= 10;
    t.WriteTotalTimeoutConstant = 200;
    t.WriteTotalTimeoutMultiplier=10;
    SetCommTimeouts(hCom, &t);
    PurgeComm(hCom, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return true;
}

static void closePort() {
    if (hCom != INVALID_HANDLE_VALUE) { CloseHandle(hCom); hCom = INVALID_HANDLE_VALUE; }
    connected = false;
}

// Send a command, read the reply. Returns payload WITHOUT the status byte.
// *st gets the status (0=OK,1=ERR,2=RO,3=NF). Returns false on transport/CRC error.
static bool transact(BYTE cmd, const BYTE* payload, WORD plen, BYTE* st, std::vector<BYTE>& out) {
    out.clear();
    if (hCom == INVALID_HANDLE_VALUE) return false;
    PurgeComm(hCom, PURGE_RXCLEAR); // drop any stale input so we don't desync
    BYTE hdr[5];
    hdr[0] = 0xAA; hdr[1] = 0x55; hdr[2] = cmd;
    hdr[3] = (BYTE)(plen & 0xFF); hdr[4] = (BYTE)(plen >> 8);
    BYTE crc = cmd ^ hdr[3] ^ hdr[4];
    for (WORD i = 0; i < plen; i++) crc ^= payload[i];
    DWORD w = 0;
    WriteFile(hCom, hdr, 5, &w, NULL);
    if (plen) WriteFile(hCom, payload, plen, &w, NULL);
    WriteFile(hCom, &crc, 1, &w, NULL);

    BYTE b;
    bool synced = false;
    for (int tries = 0; tries < 300; tries++) {
        if (readExact(hCom, &b, 1) != 1) return false;
        if (b == 0xAA) {
            if (readExact(hCom, &b, 1) != 1) return false;
            if (b == 0x55) { synced = true; break; }
        }
    }
    if (!synced) return false;
    BYTE rcmd, lo, hi;
    if (readExact(hCom, &rcmd, 1) != 1) return false;
    if (readExact(hCom, &lo,   1) != 1) return false;
    if (readExact(hCom, &hi,   1) != 1) return false;
    WORD rlen = (WORD)lo | ((WORD)hi << 8);
    std::vector<BYTE> data(rlen ? rlen : 1);
    if (rlen && readExact(hCom, &data[0], rlen) != rlen) return false;
    BYTE rcrc;
    if (readExact(hCom, &rcrc, 1) != 1) return false;
    BYTE cc = rcmd ^ lo ^ hi;
    for (WORD i = 0; i < rlen; i++) cc ^= data[i];
    if (cc != rcrc) return false;
    if (rlen == 0) return false;
    *st = data[0];
    out.assign(data.begin() + 1, data.end());
    return true;
}

static const char* statusText(BYTE st) {
    switch (st) {
        case 0: return "OK";
        case 1: return "device ERR";
        case 2: return "read-only (C:)";
        case 3: return "not found";
        default: return "unknown";
    }
}

// ---- helpers ----
static void setStatus(HWND hWnd, const char* msg) {
    SetWindowTextA(GetDlgItem(hWnd, ID_STATUS), msg);
}

static std::string bytesToHex(const BYTE* p, size_t n) {
    char tmp[4];
    std::string s;
    for (size_t i = 0; i < n; i++) {
        if (i > 0 && i % 8 == 0) s += "\r\n";
        sprintf(tmp, "%02X ", p[i]);
        s += tmp;
    }
    if (!s.empty()) s.erase(s.size() - 1);
    return s;
}

static bool hexToBytes(const char* txt, std::vector<BYTE>& out) {
    out.clear();
    const char* p = txt;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;
        int hi = -1, lo = -1;
        if (*p >= '0' && *p <= '9') hi = *p - '0';
        else if (*p >= 'A' && *p <= 'F') hi = *p - 'A' + 10;
        else if (*p >= 'a' && *p <= 'f') hi = *p - 'a' + 10;
        if (hi < 0) return false;
        p++;
        if (*p >= '0' && *p <= '9') lo = *p - '0';
        else if (*p >= 'A' && *p <= 'F') lo = *p - 'A' + 10;
        else if (*p >= 'a' && *p <= 'f') lo = *p - 'a' + 10;
        if (lo < 0) return false;
        p++;
        out.push_back((BYTE)((hi << 4) | lo));
    }
    return true;
}

// ---- protocol wrappers ----
static void doPing(HWND hWnd) {
    BYTE st; std::vector<BYTE> out;
    if (!transact(0x01, NULL, 0, &st, out)) { setStatus(hWnd, "PING: no reply / CRC error"); return; }
    if (st != 0) { setStatus(hWnd, "PING: device error"); return; }
    setStatus(hWnd, "Connected (PING OK)");
}

static void refreshList(HWND hWnd) {
    BYTE st; std::vector<BYTE> out;
    if (!transact(0x03, NULL, 0, &st, out) || st != 0) {
        setStatus(hWnd, st ? "LIST failed" : "LIST: no reply / CRC error"); return;
    }
    HWND lb = GetDlgItem(hWnd, ID_LIST);
    SendMessage(lb, LB_RESETCONTENT, 0, 0);
    WORD n = (WORD)out.size() / 15;
    for (WORD i = 0; i < n; i++) {
        const BYTE* rec = &out[i * 15];
        char name[14];
        memcpy(name, rec, 13); name[13] = 0;
        BYTE sz = rec[13]; BYTE fl = rec[14];
        char line[64];
        sprintf(line, "%2u %-13s %5u B  %s", i, name, (unsigned)(sz * 8), (fl & 0x01) ? "RUN" : "DATA");
        SendMessageA(lb, LB_ADDSTRING, 0, (LPARAM)line);
    }
    char s[64];
    sprintf(s, "LIST: %u files on D:", n);
    setStatus(hWnd, s);
}

static void loadFile(HWND hWnd) {
    HWND lb = GetDlgItem(hWnd, ID_LIST);
    LRESULT sel = SendMessage(lb, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) { setStatus(hWnd, "Select a file first"); return; }
    curIdx = (BYTE)sel;
    BYTE st; std::vector<BYTE> out;
    if (!transact(0x03, NULL, 0, &st, out) || st != 0) { setStatus(hWnd, "LIST failed"); return; }
    WORD n = (WORD)out.size() / 15;
    if (curIdx >= n) return;
    curBlocks = out[curIdx * 15 + 13];

    std::vector<BYTE> all;
    all.reserve((size_t)curBlocks * 8);
    for (BYTE blk = 0; blk < curBlocks; blk++) {
        BYTE pl[3]; pl[0] = 1; pl[1] = curIdx; pl[2] = blk; // disk D: = 1
        std::vector<BYTE> rd;
        BYTE rs;
        if (!transact(0x04, pl, 3, &rs, rd) || rs != 0 || rd.size() < 8) {
            setStatus(hWnd, rs ? "READ failed" : "READ: no reply / CRC error"); return;
        }
        all.insert(all.end(), rd.begin(), rd.end());
    }
    std::string hex = bytesToHex(&all[0], all.size());
    SetWindowTextA(GetDlgItem(hWnd, ID_HEX), hex.c_str());
    char s[64]; sprintf(s, "Loaded %u blocks (%u bytes)", curBlocks, curBlocks * 8);
    setStatus(hWnd, s);
}

static void saveFile(HWND hWnd) {
    if (!connected) { setStatus(hWnd, "Not connected"); return; }
    char buf[16384];
    GetWindowTextA(GetDlgItem(hWnd, ID_HEX), buf, sizeof(buf));
    std::vector<BYTE> bytes;
    if (!hexToBytes(buf, bytes)) { setStatus(hWnd, "Hex parse error"); return; }
    if (bytes.size() != (size_t)curBlocks * 8) {
        char s[64]; sprintf(s, "Need %u bytes, got %u", curBlocks * 8, (unsigned)bytes.size());
        setStatus(hWnd, s); return;
    }
    for (BYTE blk = 0; blk < curBlocks; blk++) {
        BYTE pl[11];
        pl[0] = 1; pl[1] = curIdx; pl[2] = blk;
        memcpy(pl + 3, &bytes[blk * 8], 8);
        std::vector<BYTE> rd; BYTE rs;
        if (!transact(0x05, pl, 11, &rs, rd) || rs != 0) {
            setStatus(hWnd, rs ? "WRITE failed (RO?)" : "WRITE: no reply / CRC error"); return;
        }
    }
    setStatus(hWnd, "Written");
}

static void createFile(HWND hWnd) {
    if (!connected) { setStatus(hWnd, "Not connected"); return; }
    char name[16]; GetWindowTextA(GetDlgItem(hWnd, ID_NEWNAME), name, sizeof(name));
    char blkTxt[16]; GetWindowTextA(GetDlgItem(hWnd, ID_NEWBLOCKS), blkTxt, sizeof(blkTxt));
    int blocks = atoi(blkTxt);
    if (strlen(name) == 0 || blocks <= 0) { setStatus(hWnd, "Name + blocks required"); return; }
    if (strlen(name) > 13) { setStatus(hWnd, "Name too long (max 13)"); return; }
    BYTE pl[1 + 13 + 1];
    pl[0] = 1; // disk D:
    memset(pl + 1, ' ', 13);
    for (int i = 0; i < 13 && name[i]; i++) pl[1 + i] = (BYTE)name[i];
    pl[14] = (BYTE)blocks;
    std::vector<BYTE> rd; BYTE rs;
    if (!transact(0x07, pl, sizeof(pl), &rs, rd)) { setStatus(hWnd, "CREATE: no reply / CRC error"); return; }
    if (rs != 0) { char s[64]; sprintf(s, "CREATE failed: %s", statusText(rs)); setStatus(hWnd, s); return; }
    char s[64]; sprintf(s, "Created '%s' (%u blocks)", name, blocks);
    setStatus(hWnd, s);
    refreshList(hWnd);
}

static void deleteFile(HWND hWnd) {
    HWND lb = GetDlgItem(hWnd, ID_LIST);
    LRESULT sel = SendMessage(lb, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) { setStatus(hWnd, "Select a file first"); return; }
    BYTE pl[2]; pl[0] = 1; pl[1] = (BYTE)sel;
    std::vector<BYTE> rd; BYTE rs;
    if (!transact(0x06, pl, 2, &rs, rd)) { setStatus(hWnd, "DELETE: no reply / CRC error"); return; }
    if (rs != 0) { char s[64]; sprintf(s, "DELETE failed: %s", statusText(rs)); setStatus(hWnd, s); return; }
    setStatus(hWnd, "Deleted");
    refreshList(hWnd);
}

static void runFile(HWND hWnd) {
    HWND lb = GetDlgItem(hWnd, ID_LIST);
    LRESULT sel = SendMessage(lb, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) { setStatus(hWnd, "Select a file first"); return; }
    BYTE pl[2]; pl[0] = 1; pl[1] = (BYTE)sel;
    std::vector<BYTE> rd; BYTE rs;
    if (!transact(0x08, pl, 2, &rs, rd)) { setStatus(hWnd, "RUN: no reply / CRC error"); return; }
    if (rs != 0) { char s[64]; sprintf(s, "RUN failed: %s", statusText(rs)); setStatus(hWnd, s); return; }
    setStatus(hWnd, "Running on device");
}

// ---- control factory ----
static HWND mk(HWND parent, const char* cls, const char* txt, DWORD style,
               int x, int y, int w, int h, int id, HFONT f) {
    HWND c = CreateWindowA(cls, txt, style | WS_CHILD | WS_VISIBLE,
                           x, y, w, h, parent, (HMENU)(id ? id : 0), NULL, NULL);
    if (c && f) SendMessageA(c, WM_SETFONT, (WPARAM)f, TRUE);
    return c;
}

// ---- window ----
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            hGui  = CreateFontA(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                DEFAULT_PITCH | FF_SWISS, "Tahoma");
            hMono = CreateFontA(15, 0, 0, 0, FW_DONTCARE, 0, 0, 0, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                FIXED_PITCH, "Courier New");
            hBg = CreateSolidBrush(RGB(236, 240, 247));

            // ---- Connection group ----
            mk(hWnd, "BUTTON", "Connection", WS_GROUP | BS_GROUPBOX, 10, 10, 700, 56, 0, hGui);
            mk(hWnd, "STATIC", "COM port:", 0, 24, 32, 70, 18, 0, hGui);
            mk(hWnd, "EDIT", "COM5", WS_BORDER | ES_AUTOHSCROLL, 96, 28, 80, 22, ID_COMPORT, hGui);
            mk(hWnd, "BUTTON", "Connect", BS_PUSHBUTTON, 190, 26, 100, 26, ID_CONNECT, hGui);
            mk(hWnd, "BUTTON", "Refresh", BS_PUSHBUTTON, 300, 26, 100, 26, ID_REFRESH, hGui);

            // ---- D: Files group ----
            mk(hWnd, "BUTTON", "D: Files", WS_GROUP | BS_GROUPBOX, 10, 76, 330, 322, 0, hGui);
            mk(hWnd, "STATIC", "idx  name  bytes  type", 0, 24, 96, 300, 16, 0, hGui);
            HWND lb = mk(hWnd, "LISTBOX", "",
                         WS_BORDER | WS_VSCROLL | LBS_HASSTRINGS | LBS_NOTIFY,
                         24, 114, 302, 236, ID_LIST, hGui);
            SendMessageA(lb, LB_SETCOLUMNWIDTH, 290, 0);
            mk(hWnd, "BUTTON", "Load",   BS_PUSHBUTTON, 24,  356, 95, 26, ID_LOAD,   hGui);
            mk(hWnd, "BUTTON", "Run",    BS_PUSHBUTTON, 129, 356, 95, 26, ID_RUN,    hGui);
            mk(hWnd, "BUTTON", "Delete", BS_PUSHBUTTON, 234, 356, 95, 26, ID_DELETE, hGui);

            // ---- Hex Editor group ----
            mk(hWnd, "BUTTON", "Hex Editor", WS_GROUP | BS_GROUPBOX, 350, 76, 360, 322, 0, hGui);
            mk(hWnd, "STATIC", "bytes, 8 per line - edit then Save", 0, 364, 96, 330, 16, 0, hGui);
            HWND he = mk(hWnd, "EDIT", "",
                         WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL,
                         364, 114, 332, 236, ID_HEX, hMono);
            SendMessageA(he, EM_SETLIMITTEXT, 16384, 0);
            mk(hWnd, "BUTTON", "Save", BS_PUSHBUTTON, 364, 356, 100, 26, ID_SAVE, hGui);

            // ---- Create group ----
            mk(hWnd, "BUTTON", "Create New File", WS_GROUP | BS_GROUPBOX, 10, 410, 700, 64, 0, hGui);
            mk(hWnd, "STATIC", "Name (<=13):", 0, 24, 432, 150, 18, 0, hGui);
            mk(hWnd, "EDIT", "NEW.OEF", WS_BORDER | ES_AUTOHSCROLL, 180, 430, 150, 22, ID_NEWNAME, hGui);
            mk(hWnd, "STATIC", "Blocks:", 0, 345, 432, 50, 18, 0, hGui);
            mk(hWnd, "EDIT", "1", WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, 400, 430, 50, 22, ID_NEWBLOCKS, hGui);
            mk(hWnd, "STATIC", "= 8 bytes", 0, 458, 432, 90, 18, ID_BLOCKINFO, hGui);
            mk(hWnd, "BUTTON", "Create", BS_PUSHBUTTON, 560, 428, 110, 26, ID_CREATE, hGui);

            // ---- Status bar ----
            mk(hWnd, "STATIC", "Not connected", SS_SUNKEN | SS_LEFT | SS_CENTERIMAGE,
               10, 486, 700, 26, ID_STATUS, hGui);
            return 0;
        }
        case WM_CTLCOLORDLG:
            return (LRESULT)hBg;
        case WM_ERASEBKGND: {
            RECT rc; GetClientRect(hWnd, &rc);
            FillRect((HDC)wp, &rc, hBg);
            return 1;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wp;
            HWND hw = (HWND)lp;
            if (GetDlgCtrlID(hw) == ID_STATUS) {
                SetTextColor(hdc, RGB(0, 0, 0));
                SetBkColor(hdc, RGB(255, 255, 255));
                return (LRESULT)GetStockObject(WHITE_BRUSH);
            }
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)hBg;
        }
        case WM_COMMAND: {
            WORD id = LOWORD(wp);
            if (HIWORD(wp) == EN_CHANGE && id == ID_NEWBLOCKS) {
                char t[16]; GetWindowTextA(GetDlgItem(hWnd, ID_NEWBLOCKS), t, sizeof(t));
                int b = atoi(t); if (b < 0) b = 0; if (b > 255) b = 255;
                char s[32]; sprintf(s, "= %d bytes", b * 8);
                SetWindowTextA(GetDlgItem(hWnd, ID_BLOCKINFO), s);
                return 0;
            }
            if (HIWORD(wp) == BN_CLICKED) {
                if (id == ID_CONNECT) {
                    char port[16]; GetWindowTextA(GetDlgItem(hWnd, ID_COMPORT), port, sizeof(port));
                    if (openPort(port)) { connected = true; doPing(hWnd); refreshList(hWnd); }
                    else setStatus(hWnd, "Cannot open port");
                } else if (id == ID_REFRESH) { if (connected) refreshList(hWnd); }
                else if (id == ID_LOAD)    { loadFile(hWnd); }
                else if (id == ID_SAVE)    { saveFile(hWnd); }
                else if (id == ID_DELETE)  { deleteFile(hWnd); }
                else if (id == ID_CREATE)  { createFile(hWnd); }
                else if (id == ID_RUN)     { runFile(hWnd); }
            }
            return 0;
        }
        case WM_DESTROY:
            closePort();
            if (hGui)  DeleteObject(hGui);
            if (hMono) DeleteObject(hMono);
            if (hBg)   DeleteObject(hBg);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hWnd, msg, wp, lp);
}

static const char* CLASS = "DiskOrangeCls";

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = CLASS;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    // Size the window so the client area is exactly the layout we designed
    // (720 x 530), preventing controls from being clipped by the title bar / borders.
    int clientW = 720, clientH = 530;
    RECT r = { 0, 0, clientW, clientH };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    int winW = r.right - r.left, winH = r.bottom - r.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - winH) / 2;

    HWND h = CreateWindowA(CLASS, "DiskOrange - OrangeOS Disk Explorer",
        WS_OVERLAPPEDWINDOW, x, y, winW, winH, NULL, NULL, hInst, NULL);
    ShowWindow(h, SW_SHOW);
    UpdateWindow(h);

    MSG m;
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return 0;
}
