// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved


#define WIN32_LEAN_AND_MEAN //inside windows.h winsock.h include is ommitted
//winsock api add ons
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winuser.h>
#include <stdlib.h>
#include <stdio.h>
#include <mutex>
#include <thread>

// Need to link with Ws2_32.lib, Mswsock.lib, and Advapi32.lib
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")

#define DEFAULT_PORT 3478

#include "DisplayManager.h"
#include "DuplicationManager.h"
#include "H264Encoder2.h"
#include "Preproc.h"
#include "OutputManager.h"
#include "ThreadManager.h"
#include "SDL_keycode.h"
#include <chrono>
#include <fstream>
#include <mfapi.h>
#include <string>
#include <sstream>
#include <vector>

#pragma comment(lib, "mfplat")
#pragma comment(lib, "mfuuid")

#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)

// byte constants
#define maxByteVal 256
constexpr auto maxPacketCount = 256 * 60;

// gui window identifiers
#define ddID 2
#define dconnID 3
#define connID 4
#define connStatus 5
HWND ddhndl, mainhndl, dconbutton, conbutton, conStatus;

//
// Globals
//
OUTPUTMANAGER OutMgr;
RECT WindowRect = { 0, 0, 800, 600 };

WSADATA wsaData;
SOCKET sock = INVALID_SOCKET;
fd_set activeFdSet, readFdSet;
sockaddr_in dest;
int recvbuflen = 10240;
string recvbuf;
struct timeval tv;

bool haveClient = false;
bool needFlush = true;
BYTE backupBuf[maxPacketCount * 1450];
int backupLens[maxPacketCount];
mutex retransMutex;
const chrono::duration<int, milli> retransmitTimeout = 300ms;
struct retransmitRequest {
    chrono::time_point<chrono::steady_clock> start;
    chrono::duration<int, milli> elapsed = retransmitTimeout;
};
map<int, retransmitRequest> retransmits;


// packet input variables
int hpCount = 0;
int lpCount = 0;
mutex sendMutex;
int prevIndex = -1;
int index = 0;
int packetPos = 0;
char inputBack[maxPacketCount * 30];
vector<int> unorderedPack;
int visited[maxPacketCount];

enum packetType {
    FRAME = 0,
    FRAMERETRANSMIT = 1,
    KEEPALIVE = 2,
    INPUTREQUEST = 3,
    ACKNOWLEDGE = 4,
    INPUTREQIND = 5
};


WORD getWinCommand(int input) {
    switch(input) {
        case SDLK_RETURN: return VK_RETURN;
        case SDLK_ESCAPE: return VK_ESCAPE;
        case SDLK_BACKSPACE: return VK_BACK;
        case SDLK_TAB: return VK_TAB;
        case SDLK_SPACE: return VK_SPACE;
        case SDLK_EXCLAIM: return '!';
        case SDLK_QUOTEDBL: return '"';
        case SDLK_HASH: return '#';
        case SDLK_PERCENT: return '%';
        case SDLK_DOLLAR: return '$';
        case SDLK_AMPERSAND: return '&';
        case SDLK_QUOTE: return '\'';
        case SDLK_LEFTPAREN: return '(';
        case SDLK_RIGHTPAREN: return ')';
        case SDLK_ASTERISK: return '*';
        case SDLK_PLUS: return '+';
        case SDLK_COMMA: return ',';
        case SDLK_MINUS: return VK_OEM_MINUS;
        case SDLK_PERIOD: VK_OEM_PERIOD;
        case SDLK_SLASH: return '/';
        case SDLK_0: return '0';
        case SDLK_1: return '1';
        case SDLK_2: return '2';
        case SDLK_3: return '3';
        case SDLK_4: return '4';
        case SDLK_5: return '5';
        case SDLK_6: return '6';
        case SDLK_7: return '7';
        case SDLK_8: return '8';
        case SDLK_9: return '9';
        case SDLK_COLON: return ':';
        case SDLK_SEMICOLON: return ';';
        case SDLK_LESS: return '<';
        case SDLK_EQUALS: return '=';
        case SDLK_GREATER: return '>';
        case SDLK_QUESTION: return '?';
        case SDLK_AT: return '@';
        case SDLK_LEFTBRACKET: return '[';
        case SDLK_BACKSLASH: return '\\';
        case SDLK_RIGHTBRACKET: return ']';
        case SDLK_CARET: return '^';
        case SDLK_UNDERSCORE: return '_';
        case SDLK_BACKQUOTE: return '`';
        case SDLK_a: return 'A';
        case SDLK_b: return 'B';
        case SDLK_c: return 'C';
        case SDLK_d: return 'D';
        case SDLK_e: return 'E';
        case SDLK_f: return 'F';
        case SDLK_g: return 'G';
        case SDLK_h: return 'H';
        case SDLK_i: return 'I';
        case SDLK_j: return 'J';
        case SDLK_k: return 'K';
        case SDLK_l: return 'L';
        case SDLK_m: return 'M';
        case SDLK_n: return 'N';
        case SDLK_o: return 'O';
        case SDLK_p: return 'P';
        case SDLK_q: return 'Q';
        case SDLK_r: return 'R';
        case SDLK_s: return 'S';
        case SDLK_t: return 'T';
        case SDLK_u: return 'U';
        case SDLK_v: return 'V';
        case SDLK_w: return 'W';
        case SDLK_x: return 'X';
        case SDLK_y: return 'Y';
        case SDLK_z: return 'Z';
        case SDLK_CAPSLOCK: return VK_CAPITAL;
        case SDLK_F1: return VK_F1;
        case SDLK_F2: return VK_F2;
        case SDLK_F3: return VK_F3;
        case SDLK_F4: return VK_F4;
        case SDLK_F5: return VK_F5;
        case SDLK_F6: return VK_F6;
        case SDLK_F7: return VK_F7;
        case SDLK_F8: return VK_F8;
        case SDLK_F9: return VK_F9;
        case SDLK_F10: return VK_F10;
        case SDLK_F11: return VK_F11;
        case SDLK_F12: return VK_F12;
        case SDLK_PRINTSCREEN: return VK_PRINT;
        case SDLK_SCROLLLOCK: return VK_SCROLL;
        case SDLK_PAUSE: return VK_MEDIA_PLAY_PAUSE;
        case SDLK_INSERT: return VK_INSERT;
        case SDLK_HOME: return VK_HOME;
        case SDLK_PAGEUP:
        case SDLK_DELETE:
        case SDLK_END:
        case SDLK_PAGEDOWN:
        case SDLK_RIGHT: return VK_RIGHT;
        case SDLK_LEFT: return VK_LEFT;
        case SDLK_DOWN: return VK_DOWN;
        case SDLK_UP: return VK_UP;
        case SDLK_NUMLOCKCLEAR: return VK_NUMLOCK;
        case SDLK_KP_DIVIDE: return VK_DIVIDE;
        case SDLK_KP_MULTIPLY: return VK_MULTIPLY;
        case SDLK_KP_MINUS: return VK_OEM_MINUS;
        case SDLK_KP_PLUS: return VK_OEM_PLUS;
        case SDLK_KP_ENTER: return VK_RETURN;
        case SDLK_KP_1: return VK_NUMPAD1;
        case SDLK_KP_2: return VK_NUMPAD2;
        case SDLK_KP_3: return VK_NUMPAD3;
        case SDLK_KP_4: return VK_NUMPAD4;
        case SDLK_KP_5: return VK_NUMPAD5;
        case SDLK_KP_6: return VK_NUMPAD6;
        case SDLK_KP_7: return VK_NUMPAD7;
        case SDLK_KP_8: return VK_NUMPAD8;
        case SDLK_KP_9: return VK_NUMPAD9;
        case SDLK_KP_0: return VK_NUMPAD0;
        case SDLK_KP_PERIOD: return VK_OEM_PERIOD;
        case SDLK_APPLICATION:
        case SDLK_POWER:
        case SDLK_KP_EQUALS:
        case SDLK_F13: return VK_F13;
        case SDLK_F14: return VK_F14;
        case SDLK_F15: return VK_F15;
        case SDLK_F16: return VK_F16;
        case SDLK_F17: return VK_F17;
        case SDLK_F18: return VK_F18;
        case SDLK_F19: return VK_F19;
        case SDLK_F20: return VK_F20;
        case SDLK_F21: return VK_F21;
        case SDLK_F22: return VK_F22;
        case SDLK_F23: return VK_F23;
        case SDLK_F24: return VK_F24;
        case SDLK_EXECUTE: return VK_EXECUTE;
        case SDLK_HELP: return VK_HELP;
        //case SDLK_MENU:
        case SDLK_SELECT: return VK_SELECT;
        case SDLK_STOP:
        case SDLK_AGAIN:
        case SDLK_UNDO:
        case SDLK_CUT:
        case SDLK_COPY:
        case SDLK_PASTE:
        case SDLK_FIND:
        case SDLK_MUTE:
        case SDLK_VOLUMEUP:
        case SDLK_VOLUMEDOWN:
        case SDLK_KP_COMMA:
        case SDLK_KP_EQUALSAS400:
        case SDLK_ALTERASE:
        case SDLK_SYSREQ:
        case SDLK_CANCEL:
        case SDLK_CLEAR:
        case SDLK_PRIOR:
        case SDLK_RETURN2:
        case SDLK_SEPARATOR:
        case SDLK_OUT:
        case SDLK_OPER:
        case SDLK_CLEARAGAIN:
        case SDLK_CRSEL:
        case SDLK_EXSEL:
        case SDLK_KP_00:
        case SDLK_KP_000:
        case SDLK_THOUSANDSSEPARATOR:
        case SDLK_DECIMALSEPARATOR:
        case SDLK_CURRENCYUNIT:
        case SDLK_CURRENCYSUBUNIT:
        case SDLK_KP_LEFTPAREN:
        case SDLK_KP_RIGHTPAREN:
        case SDLK_KP_LEFTBRACE:
        case SDLK_KP_RIGHTBRACE:
        case SDLK_KP_TAB:
        case SDLK_KP_BACKSPACE:
        case SDLK_KP_A:
        case SDLK_KP_B:
        case SDLK_KP_C:
        case SDLK_KP_D:
        case SDLK_KP_E:
        case SDLK_KP_F:
        case SDLK_KP_XOR:
        case SDLK_KP_POWER:
        case SDLK_KP_PERCENT:
        case SDLK_KP_LESS:
        case SDLK_KP_GREATER:
        case SDLK_KP_AMPERSAND:
        case SDLK_KP_DBLAMPERSAND:
        case SDLK_KP_VERTICALBAR:
        case SDLK_KP_DBLVERTICALBAR:
        case SDLK_KP_COLON:
        case SDLK_KP_HASH:
        case SDLK_KP_SPACE:
        case SDLK_KP_AT:
        case SDLK_KP_EXCLAM:
        case SDLK_KP_MEMSTORE:
        case SDLK_KP_MEMRECALL:
        case SDLK_KP_MEMCLEAR:
        case SDLK_KP_MEMADD:
        case SDLK_KP_MEMSUBTRACT:
        case SDLK_KP_MEMMULTIPLY:
        case SDLK_KP_MEMDIVIDE:
        case SDLK_KP_PLUSMINUS:
        case SDLK_KP_CLEAR:
        case SDLK_KP_CLEARENTRY:
        case SDLK_KP_BINARY:
        case SDLK_KP_OCTAL:
        case SDLK_KP_DECIMAL:
        case SDLK_KP_HEXADECIMAL:
        case SDLK_LCTRL: return VK_LCONTROL;
        case SDLK_LSHIFT: return VK_LSHIFT;
        case SDLK_LALT: return VK_LMENU;
        case SDLK_LGUI: return VK_LWIN;
        case SDLK_RCTRL: return VK_RCONTROL;
        case SDLK_RSHIFT: return VK_RSHIFT;
        case SDLK_RALT: return VK_RMENU;
        case SDLK_RGUI: return VK_RWIN;
        case SDLK_MODE:
        case SDLK_AUDIONEXT:
        case SDLK_AUDIOPREV:
        case SDLK_AUDIOSTOP:
        case SDLK_AUDIOPLAY:
        case SDLK_AUDIOMUTE:
        case SDLK_MEDIASELECT:
        case SDLK_WWW:
        case SDLK_MAIL:
        case SDLK_CALCULATOR:
        case SDLK_COMPUTER:
        case SDLK_AC_SEARCH:
        case SDLK_AC_HOME:
        case SDLK_AC_BACK:
        case SDLK_AC_FORWARD:
        case SDLK_AC_STOP:
        case SDLK_AC_REFRESH:
        case SDLK_AC_BOOKMARKS:
        case SDLK_BRIGHTNESSDOWN:
        case SDLK_BRIGHTNESSUP:
        case SDLK_DISPLAYSWITCH:
        case SDLK_KBDILLUMTOGGLE:
        case SDLK_KBDILLUMDOWN:
        case SDLK_KBDILLUMUP:
        case SDLK_EJECT:
        case SDLK_SLEEP:
        case SDLK_APP1:
        case SDLK_APP2:
        case SDLK_AUDIOREWIND:
        case SDLK_AUDIOFASTFORWARD:
        case SDLK_SOFTLEFT:
        case SDLK_SOFTRIGHT:
        case SDLK_CALL:
        case SDLK_ENDCALL:
        default:
            return NULL;
    }

}


// Below are lists of errors expect from Dxgi API calls when a transition event like mode change, PnpStop, PnpStart
// desktop switch, TDR or session disconnect/reconnect. In all these cases we want the application to clean up the threads that process
// the desktop updates and attempt to recreate them.
// If we get an error that is not on the appropriate list then we exit the application

// These are the errors we expect from general Dxgi API due to a transition
HRESULT SystemTransitionsExpectedErrors[] = {
                                                DXGI_ERROR_DEVICE_REMOVED,
                                                DXGI_ERROR_ACCESS_LOST,
                                                static_cast<HRESULT>(WAIT_ABANDONED),
                                                S_OK                                    // Terminate list with zero valued HRESULT
                                            };

// These are the errors we expect from IDXGIOutput1::DuplicateOutput due to a transition
HRESULT CreateDuplicationExpectedErrors[] = {
                                                DXGI_ERROR_DEVICE_REMOVED,
                                                static_cast<HRESULT>(E_ACCESSDENIED),
                                                DXGI_ERROR_UNSUPPORTED,
                                                DXGI_ERROR_SESSION_DISCONNECTED,
                                                S_OK                                    // Terminate list with zero valued HRESULT
                                            };

// These are the errors we expect from IDXGIOutputDuplication methods due to a transition
HRESULT FrameInfoExpectedErrors[] = {
                                        DXGI_ERROR_DEVICE_REMOVED,
                                        DXGI_ERROR_ACCESS_LOST,
                                        S_OK                                    // Terminate list with zero valued HRESULT
                                    };

// These are the errors we expect from IDXGIAdapter::EnumOutputs methods due to outputs becoming stale during a transition
HRESULT EnumOutputsExpectedErrors[] = {
                                          DXGI_ERROR_NOT_FOUND,
                                          S_OK                                    // Terminate list with zero valued HRESULT
                                      };


//
// Forward Declarations
//
DWORD WINAPI DDProc(_In_ void* Param);
LRESULT CALLBACK DDWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
bool ProcessCmdline(_Out_ INT* Output);
void ShowHelp();
int DisplayConfirmation(_In_ LPCWSTR Str, _In_ LPCWSTR Title);

//
// Class for progressive waits
//
typedef struct
{
    UINT    WaitTime;
    UINT    WaitCount;
}WAIT_BAND;

#define WAIT_BAND_COUNT 3
#define WAIT_BAND_STOP 0

class DYNAMIC_WAIT
{
    public :
        DYNAMIC_WAIT();
        ~DYNAMIC_WAIT();

        void Wait();

    private :

    static const WAIT_BAND   m_WaitBands[WAIT_BAND_COUNT];

    // Period in seconds that a new wait call is considered part of the same wait sequence
    static const UINT       m_WaitSequenceTimeInSeconds = 2;

    UINT                    m_CurrentWaitBandIdx;
    UINT                    m_WaitCountInCurrentBand;
    LARGE_INTEGER           m_QPCFrequency;
    LARGE_INTEGER           m_LastWakeUpTime;
    BOOL                    m_QPCValid;
};
const WAIT_BAND DYNAMIC_WAIT::m_WaitBands[WAIT_BAND_COUNT] = {
                                                                 {250, 20},
                                                                 {2000, 60},
                                                                 {5000, WAIT_BAND_STOP}   // Never move past this band
                                                             };

DYNAMIC_WAIT::DYNAMIC_WAIT() : m_CurrentWaitBandIdx(0), m_WaitCountInCurrentBand(0)
{
    m_QPCValid = QueryPerformanceFrequency(&m_QPCFrequency);
    m_LastWakeUpTime.QuadPart = 0L;
}

DYNAMIC_WAIT::~DYNAMIC_WAIT()
{
}

void DYNAMIC_WAIT::Wait()
{
    LARGE_INTEGER CurrentQPC = {0};

    // Is this wait being called with the period that we consider it to be part of the same wait sequence
    QueryPerformanceCounter(&CurrentQPC);
    if (m_QPCValid && (CurrentQPC.QuadPart <= (m_LastWakeUpTime.QuadPart + (m_QPCFrequency.QuadPart * m_WaitSequenceTimeInSeconds))))
    {
        // We are still in the same wait sequence, lets check if we should move to the next band
        if ((m_WaitBands[m_CurrentWaitBandIdx].WaitCount != WAIT_BAND_STOP) && (m_WaitCountInCurrentBand > m_WaitBands[m_CurrentWaitBandIdx].WaitCount))
        {
            m_CurrentWaitBandIdx++;
            m_WaitCountInCurrentBand = 0;
        }
    }
    else
    {
        // Either we could not get the current time or we are starting a new wait sequence
        m_WaitCountInCurrentBand = 0;
        m_CurrentWaitBandIdx = 0;
    }

    // Sleep for the required period of time
    Sleep(m_WaitBands[m_CurrentWaitBandIdx].WaitTime);

    // Record the time we woke up so we can detect wait sequences
    QueryPerformanceCounter(&m_LastWakeUpTime);
    m_WaitCountInCurrentBand++;
}

void printDebugMessage(string str) {
    wstring temp = wstring(str.begin(), str.end());
    OutputDebugString(temp.c_str());
}

int initSocket() {
    haveClient = false;
    memset(visited, -1, sizeof(visited));
    unorderedPack.clear();
    hpCount = 0;
    lpCount = 0;
    prevIndex = -1;
    index = 0;
    packetPos = 0;
    int iResult = 0;
    if (sock == INVALID_SOCKET) {
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET) {
            WSACleanup();
            return 1;
        }
        FD_ZERO(&activeFdSet);
        FD_SET(sock, &activeFdSet);
        dest.sin_family = AF_INET;
        dest.sin_port = htons(DEFAULT_PORT);
        inet_pton(AF_INET, "167.234.216.217", &dest.sin_addr.s_addr);
        //inet_pton(AF_INET, "192.168.100.2", &dest.sin_addr.s_addr);
        u_long mode = 0;
        ioctlsocket(sock, SIO_UDP_CONNRESET, &mode);
        sendto(sock, "1", 2, 0, (sockaddr*)&dest, sizeof(dest));
        recvbuf.resize(recvbuflen);
        while (!haveClient) {
            iResult = recvfrom(sock, &recvbuf[0], recvbuflen - 1, 0, NULL, NULL);
            if (iResult <= 0) {
                printDebugMessage(to_string(WSAGetLastError()) + "\n");
            }
            else if (DisplayConfirmation(L"A user has requested to join this session.\n Allow connection?", L"Connection Request") == IDYES) {
                recvbuf[iResult] = '\0';
                int newPort = stoi(recvbuf.substr(recvbuf.find(":") + 1, iResult));
                dest.sin_port = htons(newPort);
                inet_pton(AF_INET, recvbuf.substr(0, recvbuf.find(":")).c_str(), &dest.sin_addr.s_addr);
                needFlush = true;
                retransMutex.lock();
                retransmits.clear();
                retransMutex.unlock();
                haveClient = true;
            }
        }
    }
    return 0;
}

void stopSocket() {
    FD_CLR(sock, &activeFdSet);
    closesocket(sock);
    sock = INVALID_SOCKET;
    haveClient = false;
    SetWindowText(conStatus, L"Client disconnected");

    EnableWindow(conbutton, true);
    EnableWindow(dconbutton, false);
}

void sendPacket(string toSend, packetType type) {
    lock_guard<mutex> lock{sendMutex};
    stringstream send;
    send << (char)type << (char)hpCount << (char)lpCount << toSend;
    if (type == INPUTREQUEST) {
        printDebugMessage("Sending input request with index " + to_string(hpCount * maxByteVal + lpCount));
    }

    memcpy(&backupBuf[(hpCount * maxByteVal + lpCount) * 1400], toSend.c_str(), toSend.length());
    backupLens[hpCount * maxByteVal + lpCount] = toSend.length();

    int iResult = sendto(sock, send.str().c_str(), send.str().length(), 0, (sockaddr*)&dest, sizeof(dest));
    lpCount++;
    if (lpCount > 255) {
        hpCount++;
        lpCount = 0;
        if (hpCount > 59) {
            hpCount = 0;
        }
    }
}
//
// Program entry point
//
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ INT nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    INT SingleOutput;
    int iResult = 0;

    // Synchronization
    HANDLE UnexpectedErrorEvent = nullptr;
    HANDLE ExpectedErrorEvent = nullptr;
    HANDLE TerminateThreadsEvent = nullptr;

    bool CmdResult = ProcessCmdline(&SingleOutput);
    if (!CmdResult)
    {
        ShowHelp();
        return 0;
    }

    // Event used by the threads to signal an unexpected error and we want to quit the app
    UnexpectedErrorEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!UnexpectedErrorEvent)
    {
        ProcessFailure(nullptr, L"UnexpectedErrorEvent creation failed", L"Error", E_UNEXPECTED);
        return 0;
    }

    // Event for when a thread encounters an expected error
    ExpectedErrorEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!ExpectedErrorEvent)
    {
        ProcessFailure(nullptr, L"ExpectedErrorEvent creation failed", L"Error", E_UNEXPECTED);
        return 0;
    }

    // Event to tell spawned threads to quit
    TerminateThreadsEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!TerminateThreadsEvent)
    {
        ProcessFailure(nullptr, L"TerminateThreadsEvent creation failed", L"Error", E_UNEXPECTED);
        return 0;
    }

    // Load simple cursor
    HCURSOR Cursor = nullptr;
    Cursor = LoadCursor(nullptr, IDC_ARROW);
    if (!Cursor)
    {
        ProcessFailure(nullptr, L"Cursor load failed", L"Error", E_UNEXPECTED);
        return 0;
    }
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed with error: %d\n", iResult);
        return 1;
    }

    //set timeout for select
    tv.tv_sec = 5;
    tv.tv_usec = 0;

    // Register class
    WNDCLASSEXW Wc;
    Wc.cbSize           = sizeof(WNDCLASSEXW);
    Wc.style            = CS_HREDRAW | CS_VREDRAW;
    Wc.lpfnWndProc      = DDWndProc;
    Wc.cbClsExtra       = 0;
    Wc.cbWndExtra       = 0;
    Wc.hInstance        = hInstance;
    Wc.hIcon            = nullptr;
    Wc.hCursor          = Cursor;
    Wc.hbrBackground    = (HBRUSH)(COLOR_BACKGROUND + 1);
    Wc.lpszMenuName     = nullptr;
    Wc.lpszClassName    = L"ddasample";
    Wc.hIconSm          = nullptr;
    if (!RegisterClassExW(&Wc))
    {
        ProcessFailure(nullptr, L"Window class registration failed", L"Error", E_UNEXPECTED);
        return 0;
    }

    Wc.lpfnWndProc      = WndProc;
    Wc.lpszClassName    = L"MainApp";
    if (!RegisterClassExW(&Wc))
    {
        ProcessFailure(nullptr, L"Window class registration failed", L"Error", E_UNEXPECTED);
        return 0;
    }

    // Create window
    AdjustWindowRect(&WindowRect, WS_OVERLAPPEDWINDOW, FALSE);
    mainhndl = CreateWindowW(L"MainApp", L"Remote Desktop App",
        WS_VISIBLE | WS_OVERLAPPEDWINDOW,
        0, 0,
        WindowRect.right - WindowRect.left, WindowRect.bottom - WindowRect.top,
        nullptr, nullptr, hInstance, nullptr);

    if (!mainhndl)
    {
        ProcessFailure(nullptr, L"Window creation failed", L"Error", E_FAIL);
        return 0;
    }
    

    DestroyCursor(Cursor);

    THREADMANAGER ThreadMgr;
    RECT DeskBounds;
    UINT OutputCount;

    // Message loop (attempts to update screen when no other messages to process)
    MSG msg = {0};
    bool FirstTime = true;
    bool Occluded = true;
    DYNAMIC_WAIT DynamicWait;

    while (WM_QUIT != msg.message)
    {
        DUPL_RETURN Ret = DUPL_RETURN_SUCCESS;
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == OCCLUSION_STATUS_MSG)
            {
                // Present may not be occluded now so try again
                Occluded = false;
            }
            else
            {
                // Process window messages
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        else if (WaitForSingleObjectEx(UnexpectedErrorEvent, 0, FALSE) == WAIT_OBJECT_0)
        {
            // Unexpected error occurred so exit the application
            break;
        }
        else if (FirstTime || WaitForSingleObjectEx(ExpectedErrorEvent, 0, FALSE) == WAIT_OBJECT_0)
        {
            if (!FirstTime)
            {
                // Terminate other threads
                SetEvent(TerminateThreadsEvent);
                ThreadMgr.WaitForThreadTermination();
                ResetEvent(TerminateThreadsEvent);
                ResetEvent(ExpectedErrorEvent);

                // Clean up
                ThreadMgr.Clean();
                OutMgr.CleanRefs();

                // As we have encountered an error due to a system transition we wait before trying again, using this dynamic wait
                // the wait periods will get progressively long to avoid wasting too much system resource if this state lasts a long time
                DynamicWait.Wait();
            }
            else
            {
                // First time through the loop so nothing to clean up
                FirstTime = false;
            }

            // Re-initialize
            Ret = OutMgr.InitOutput(ddhndl, SingleOutput, &OutputCount, &DeskBounds);
            MoveWindow(mainhndl, 0, 0, (DeskBounds.right - DeskBounds.left) / 2, (DeskBounds.bottom - DeskBounds.top) / 2, TRUE);

            if (Ret == DUPL_RETURN_SUCCESS)
            {
                HANDLE SharedHandle = OutMgr.GetSharedHandle();
                if (SharedHandle)
                {
                    Ret = ThreadMgr.Initialize(SingleOutput, OutputCount, UnexpectedErrorEvent, ExpectedErrorEvent, TerminateThreadsEvent, SharedHandle, &DeskBounds);
                }
                else
                {
                    DisplayMsg(L"Failed to get handle of shared surface", L"Error", S_OK);
                    Ret = DUPL_RETURN_ERROR_UNEXPECTED;
                }
            }

            // We start off in occluded state and we should immediate get a occlusion status window message
            Occluded = true;
        }
        else
        {
            // Nothing else to do, so try to present to write out to window if not occluded
            if (!Occluded)
            {
                Ret = OutMgr.UpdateApplicationWindow(ThreadMgr.GetPointerInfo(), &Occluded);
            }
        }

        // Check if for errors
        if (Ret != DUPL_RETURN_SUCCESS)
        {
            if (Ret == DUPL_RETURN_ERROR_EXPECTED)
            {
                // Some type of system transition is occurring so retry
                SetEvent(ExpectedErrorEvent);
            }
            else
            {
                // Unexpected error so exit
                break;
            }
        }
    }

    // Make sure all other threads have exited
    if (SetEvent(TerminateThreadsEvent))
    {
        closesocket(sock);
        sock = INVALID_SOCKET;
        ThreadMgr.WaitForThreadTermination();
    }

    // Clean up
    CloseHandle(UnexpectedErrorEvent);
    CloseHandle(ExpectedErrorEvent);
    CloseHandle(TerminateThreadsEvent);
    CoUninitialize();
    closesocket(sock);
    WSACleanup();

    if (msg.message == WM_QUIT)
    {
        // For a WM_QUIT message we should return the wParam value
        return static_cast<INT>(msg.wParam);
    }

    return 0;
}

//
// Shows help
//
void ShowHelp()
{
    DisplayMsg(L"The following optional parameters can be used -\n  /output [all | n]\t\tto duplicate all outputs or the nth output\n  /?\t\t\tto display this help section",
               L"Proper usage", S_OK);
}

//
// Process command line parameters
//
bool ProcessCmdline(_Out_ INT* Output)
{
    *Output = -1;

    // __argv and __argc are global vars set by system
    for (UINT i = 1; i < static_cast<UINT>(__argc); ++i)
    {
        if ((strcmp(__argv[i], "-output") == 0) ||
            (strcmp(__argv[i], "/output") == 0))
        {
            if (++i >= static_cast<UINT>(__argc))
            {
                return false;
            }

            if (strcmp(__argv[i], "all") == 0)
            {
                *Output = -1;
            }
            else
            {
                *Output = atoi(__argv[i]);
            }
            continue;
        }
        else
        {
            return false;
        }
    }
    return true;
}

BOOL CALLBACK resizeChildrenProc(HWND hwndChild, LPARAM lParam) {
    int id = GetWindowLong(hwndChild, GWL_ID);
    LPRECT parentRect = (LPRECT)lParam;
    switch (id) {
        case ddID:
            MoveWindow(hwndChild, NULL, NULL, parentRect->right * 0.9, parentRect->bottom * 0.9, TRUE);
            break;

        case dconnID:
            MoveWindow(hwndChild, NULL, parentRect->bottom - parentRect->bottom * 0.1, parentRect->right * 0.2, parentRect->bottom * 0.1, TRUE);
            break;

        case connID:
            MoveWindow(hwndChild, parentRect->left + parentRect->right * 0.2, parentRect->bottom - parentRect->bottom * 0.1, parentRect->right * 0.2, parentRect->bottom * 0.1, TRUE);
            break;

        case connStatus:
            MoveWindow(hwndChild, parentRect->right * 0.9, parentRect->bottom * 0.1, parentRect->right * 0.1, parentRect->bottom * 0.1, TRUE);
            break;
    }
    return TRUE;
}

// main window message processor
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message)
    {
        case WM_CREATE:
        {
            ddhndl = CreateWindowW(L"ddasample", L"Remote Desktop View",
                WS_VISIBLE | WS_CHILD,
                0, 0,
                WindowRect.right - WindowRect.left, WindowRect.bottom - WindowRect.top,
                hWnd, (HMENU)ddID, nullptr, nullptr);
            dconbutton = CreateWindow(
                L"BUTTON",  // Predefined class; Unicode assumed 
                L"Disconnect",      // Button text 
                WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,  // Styles 
                0,         // x position 
                WindowRect.bottom - 100,         // y position 
                100,        // Button width
                100,        // Button height
                hWnd,     // Parent window
                (HMENU)dconnID,       // No menu.
                nullptr,
                nullptr);      // Pointer not needed.
            conbutton = CreateWindow(
                L"BUTTON",  // Predefined class; Unicode assumed 
                L"Start Hosting",      // Button text 
                WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,  // Styles 
                100,         // x position 
                WindowRect.bottom - 100,         // y position 
                100,        // Button width
                100,        // Button height
                hWnd,     // Parent window
                (HMENU)connID,       // No menu.
                nullptr,
                nullptr);      // Pointer not needed.
            conStatus = CreateWindow(
                L"STATIC",
                L"No client connected",
                WS_VISIBLE | WS_CHILD | SS_CENTER,
                750, 20, 200, 20,
                hWnd,
                (HMENU) connStatus,
                nullptr, nullptr
            );
            EnableWindow(dconbutton, false);
            break;
        }
        case WM_DESTROY:
        {
            PostQuitMessage(0);
            break;
        }
        case WM_SIZE:
        {
            RECT parentSize;
            GetClientRect(hWnd, &parentSize);
            EnumChildWindows(hWnd, resizeChildrenProc, (LPARAM)&parentSize);
            break;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case dconnID:
                {
                    stopSocket();
                    break;
                }
                case connID:
                {
                    initSocket();
                    if (haveClient) {
                        SetWindowText(conStatus, L"Client connected");
                    }
                    EnableWindow(conbutton, false);
                    EnableWindow(dconbutton, true);
                    break;
                }
            }
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0;
}


//
// Window message processor
//
LRESULT CALLBACK DDWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_SIZE:
        {
            // Tell output manager that window size has changed
            OutMgr.WindowResize();
            break;
        }
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0;
}

//
// Entry point for new duplication threads
//
DWORD WINAPI DDProc(_In_ void* Param)
{
    // Classes
    DISPLAYMANAGER DispMgr;
    DUPLICATIONMANAGER DuplMgr;

    // D3D objects
    ID3D11Texture2D* SharedSurf = nullptr;
    IDXGIKeyedMutex* KeyMutex = nullptr;

    // Data passed in from thread creation
    THREAD_DATA* TData = reinterpret_cast<THREAD_DATA*>(Param);

    // Get desktop
    DUPL_RETURN Ret;
    HDESK CurrentDesktop = nullptr;
    CurrentDesktop = OpenInputDesktop(0, FALSE, GENERIC_ALL);

    // Write frame objects
    IMFMediaBuffer* outBuffer;
    DWORD length;
    BYTE* data;
    int iResult = 0;


    if (!CurrentDesktop)
    {
        // We do not have access to the desktop so request a retry
        SetEvent(TData->ExpectedErrorEvent);
        Ret = DUPL_RETURN_ERROR_EXPECTED;
        goto Exit;
    }

    // Attach desktop to this thread
    bool DesktopAttached = SetThreadDesktop(CurrentDesktop) != 0;
    CloseDesktop(CurrentDesktop);
    CurrentDesktop = nullptr;
    if (!DesktopAttached)
    {
        // We do not have access to the desktop so request a retry
        Ret = DUPL_RETURN_ERROR_EXPECTED;
        goto Exit;
    }

    // New display manager
    DispMgr.InitD3D(&TData->DxRes);
 
    // Obtain handle to sync shared Surface
    HRESULT hr = TData->DxRes.Device->OpenSharedResource(TData->TexSharedHandle, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&SharedSurf));
    if (FAILED (hr))
    {
        Ret = ProcessFailure(TData->DxRes.Device, L"Opening shared texture failed", L"Error", hr, SystemTransitionsExpectedErrors);
        goto Exit;
    }

    hr = SharedSurf->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void**>(&KeyMutex));
    if (FAILED(hr))
    {
        Ret = ProcessFailure(nullptr, L"Failed to get keyed mutex interface in spawned thread", L"Error", hr);
        goto Exit;
    }

    // Make duplication manager
    Ret = DuplMgr.InitDupl(TData->DxRes.Device, TData->Output);
    if (Ret != DUPL_RETURN_SUCCESS)
    {
        goto Exit;
    }

    // Get output description
    DXGI_OUTPUT_DESC DesktopDesc;
    RtlZeroMemory(&DesktopDesc, sizeof(DXGI_OUTPUT_DESC));
    DuplMgr.GetOutputDesc(&DesktopDesc);

    // Main duplication loop
    bool WaitToProcessCurrentFrame = false;
    FRAME_DATA CurrentData;

    while ((WaitForSingleObjectEx(TData->TerminateThreadsEvent, 0, FALSE) == WAIT_TIMEOUT))
    {
        if (!WaitToProcessCurrentFrame)
        {
            // Get new frame from desktop duplication
            bool TimeOut;
            Ret = DuplMgr.GetFrame(&CurrentData, &TimeOut);
            if (Ret != DUPL_RETURN_SUCCESS)
            {
                // An error occurred getting the next frame drop out of loop which
                // will check if it was expected or not
                break;
            }

            // Check for timeout
            if (TimeOut)
            {
                // No new frame at the moment
                continue;
            }
        }

        // We have a new frame so try and process it
        // Try to acquire keyed mutex in order to access shared surface
        hr = KeyMutex->AcquireSync(0, 1000);
        if (hr == static_cast<HRESULT>(WAIT_TIMEOUT))
        {
            // Can't use shared surface right now, try again later
            WaitToProcessCurrentFrame = true;
            continue;
        }
        else if (FAILED(hr))
        {
            // Generic unknown failure
            Ret = ProcessFailure(TData->DxRes.Device, L"Unexpected error acquiring KeyMutex", L"Error", hr, SystemTransitionsExpectedErrors);
            DuplMgr.DoneWithFrame();
            break;
        }

        // We can now process the current frame
        WaitToProcessCurrentFrame = false;

        // Get mouse info
        Ret = DuplMgr.GetMouse(TData->PtrInfo, &(CurrentData.FrameInfo), TData->OffsetX, TData->OffsetY);
        if (Ret != DUPL_RETURN_SUCCESS)
        {
            DuplMgr.DoneWithFrame();
            KeyMutex->ReleaseSync(1);
            break;
        }

        // Process new frame
        Ret = DispMgr.ProcessFrame(&CurrentData, SharedSurf, TData->OffsetX, TData->OffsetY, &DesktopDesc);
        if (Ret != DUPL_RETURN_SUCCESS)
        {
            DuplMgr.DoneWithFrame();
            KeyMutex->ReleaseSync(1);
            break;
        }
 
        if (SUCCEEDED(hr))
        {
            // Send frames to the transformer
            retransMutex.lock();
            if (haveClient && retransmits.size() < 20) {
                hr = DispMgr.WriteFrame(SharedSurf, &needFlush, &outBuffer);
               
                if (SUCCEEDED(hr)) {
                    outBuffer->Lock(&data, NULL, &length);
                    int count = length;
                    while (count > 0) {
                        stringstream send;
                        int nextSub = min(count, 1400);
                        for (int i = 0; i < nextSub; i++) {
                            send << data[length - count + i];
                        }
                        sendPacket(send.str(), FRAME);
                        count -= nextSub;
                        }

                    outBuffer->Unlock();
                }
                SafeRelease(&outBuffer);
            }
            retransMutex.unlock();

            //GetCounter();

            if (FAILED(hr))
            {
                break;
            }
        }

        // Release acquired keyed mutex
        hr = KeyMutex->ReleaseSync(1);
        if (FAILED(hr))
        {
            Ret = ProcessFailure(TData->DxRes.Device, L"Unexpected error releasing the keyed mutex", L"Error", hr, SystemTransitionsExpectedErrors);
            DuplMgr.DoneWithFrame();
            break;
        }

        // Release frame back to desktop duplication
        Ret = DuplMgr.DoneWithFrame();
        if (Ret != DUPL_RETURN_SUCCESS)
        {
            break;
        }
    }

Exit:
    if (Ret != DUPL_RETURN_SUCCESS)
    {
        if (Ret == DUPL_RETURN_ERROR_EXPECTED)
        {
            // The system is in a transition state so request the duplication be restarted
            SetEvent(TData->ExpectedErrorEvent);
        }
        else
        {
            // Unexpected error so exit the application
            SetEvent(TData->UnexpectedErrorEvent);
        }
    }
    
    if (SharedSurf)
    {
        SharedSurf->Release();
        SharedSurf = nullptr;
    }

    if (KeyMutex)
    {
        KeyMutex->Release();
        KeyMutex = nullptr;
    }

    return 0;
}

//
// Entry point for input thread
//
DWORD WINAPI InputProc(_In_ void* Param)
{
    THREAD_DATA* TData = reinterpret_cast<THREAD_DATA*>(Param);
    int iResult = 0;
    // Receive until the peer closes the connection
    
    while ((WaitForSingleObjectEx(TData->TerminateThreadsEvent, 0, FALSE) == WAIT_TIMEOUT)) {
        if (!haveClient || sock == INVALID_SOCKET) {
            Sleep(500);
        } else if (haveClient) {
            readFdSet = activeFdSet;
            iResult = select(FD_SETSIZE, &readFdSet, NULL, NULL, &tv);
            if (iResult < 0) {
                printDebugMessage(to_string(WSAGetLastError()) + "\n");
                continue;
            }
            if (iResult == 0) {
                stopSocket();
                continue;
            }
            iResult = recvfrom(sock, &recvbuf[0], recvbuflen - 1, 0, NULL, NULL);
            
            if ((uint8_t)recvbuf[0] == 0) {
                continue;
            }
            if (iResult < 4) { //minimum packet size past this
                OutputDebugString(L"Packet too short");
                continue;
            }

            // unnumbered packets
            if (recvbuf[0] == 3) {
                if (recvbuf[1] == '1' && iResult >= 5) { // mouse motion
                    INPUT inputs[1] = {};
                    inputs[0].type = INPUT_MOUSE;
                    string x;
                    int i = 2;
                    while (i < iResult && recvbuf[i] != 'a') {
                        x += recvbuf[i];
                        i++;
                    }
                    float per;
                    try {
                        per = stof(x.c_str());
                    }
                    catch (const exception& e) {
                        continue;
                    }
                    inputs[0].mi.dx = (int)(65535 * per);
                    i++;
                    x = "";
                    while (i < iResult) {
                        x += recvbuf[i];
                        i++;
                    }
                    try {
                        per = stof(x.c_str());
                    }
                    catch (const exception& e) {
                        continue;
                    }
                    inputs[0].mi.dy = (int)(65535 * per);
                    inputs[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
                    SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
                }
                else if (recvbuf[1] == '7' && iResult >= 4) { // frame retransmit request
                    for (int i = 2; i < iResult; i += 2) {
                        if (i + 1 < iResult) {
                            int ind = (uint8_t)(recvbuf[i]) * maxByteVal + (uint8_t)(recvbuf[i + 1]);
                            printDebugMessage("Retransmit " + to_string(ind) + "\n");
                            if (ind < maxPacketCount) {
                                stringstream send;
                                send << (char)FRAMERETRANSMIT << recvbuf[i] << recvbuf[i+1];
                                for (int i = 0; i < backupLens[ind]; i++) {
                                    send << (char)(backupBuf[ind * 1400 + i]);
                                }
                                sendto(sock, send.str().c_str(), backupLens[ind] + 3, 0, (sockaddr*)&dest, sizeof(dest));
                                retransmitRequest req;
                                req.start = chrono::steady_clock::now();
                                retransMutex.lock();
                                retransmits.emplace(ind, req);
                                retransMutex.unlock();
                            }
                        }
                    }
                }
                else if (recvbuf[1] == '9') { // frame retransmit request
                    int index = (uint8_t)(recvbuf[2]) * maxByteVal + (uint8_t)recvbuf[3];
                    if (index < maxPacketCount) {
                        retransMutex.lock();
                        retransmits.erase(index);
                        retransMutex.unlock();
                    }
                    printDebugMessage(to_string(index) + " acknowledged\n");
                }
            } else {
                index = (int)((uint8_t)(recvbuf[1]) * maxByteVal + (uint8_t)recvbuf[2]);

                memcpy(&inputBack[index * 30], &recvbuf[3], iResult - 3);
                visited[index] = iResult - 3;
                printDebugMessage(to_string(index) + " received\n");

            if (recvbuf[0] == 1) {
                auto match = find(unorderedPack.begin(), unorderedPack.end(), index);
                if (match != unorderedPack.end()) {
                    unorderedPack.erase(match);
            } else {
                    if (unorderedPack.size() > 0) {
                        auto minVal = min_element(unorderedPack.begin(), unorderedPack.end());
                        int unorderedDiff = min(abs(index - *minVal), maxPacketCount - abs(index - *minVal));
                            if (unorderedDiff > 2 || unorderedPack.size() >= 2) {
                            stringstream send;
                                //send << (char)INPUTREQIND;
                            for (auto it = unorderedPack.begin(); it != unorderedPack.end(); it++) {
                                    printDebugMessage(to_string(*it) + " asking for retransmit\n");
                                    send << (char)(*it / maxByteVal);
                                    send << (char)(*it % maxByteVal);

                            }
                            unorderedPack.clear();
                                sendPacket(send.str(), INPUTREQIND);
                        }
                    }
                    if ((prevIndex + 1) % maxPacketCount != index) {
                        int diff = min(abs(index - prevIndex), maxPacketCount - abs(index - prevIndex));

                            if (diff < 2) {
                                printDebugMessage(to_string(prevIndex) + " " + to_string(index) + "\n");
                            int smaller, bigger;
                            if (diff == abs(index - prevIndex)) {
                                smaller = prevIndex;
                                bigger = index;
                            }
                            else {
                                smaller = index;
                                bigger = prevIndex;
                            }
                            for (int i = smaller; i < bigger; i = (i + 1) % (maxPacketCount)) {
                                unorderedPack.push_back(i);
                            }
                        }
                        else {
                                printDebugMessage(to_string(prevIndex) + "\n" + to_string(index) + " missing\n");
                            stringstream send;
                                send << (char)(((prevIndex + 1) % maxPacketCount) / maxByteVal) << (char)(((prevIndex + 1) % maxPacketCount) % maxByteVal) << (char)(recvbuf[1]) << (char)(recvbuf[2]);
                                sendPacket(send.str(), INPUTREQUEST);
                            }
                        }

                        prevIndex = index;
                    }

            } else if (recvbuf[0] == 2) {
                    stringstream send;
                    send << (char)ACKNOWLEDGE << recvbuf[1] << recvbuf[2];
                    printDebugMessage("Sending acknowledgement" + to_string((uint8_t)recvbuf[1] * maxByteVal + (uint8_t)recvbuf[2]));
                    sendto(sock, send.str().c_str(), send.str().size() + 1, 0, (sockaddr*)&dest, sizeof(dest));
                }

            while (visited[packetPos] != -1) {
                if (recvbuf[3] == '0' && iResult >= 5) {
                    INPUT inputs[1] = {};
                    inputs[0].type = INPUT_KEYBOARD;
                    int command;
                    try {
                        command = stoi(recvbuf.substr(4, iResult - 4));
                    }
                    catch (const exception& e) {
                        continue;
                    }
                    inputs[0].ki.wVk = getWinCommand(command);
                    SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
                }
                else if (recvbuf[3] == '2' && iResult >= 5) {
                    INPUT inputs[1] = {};
                    inputs[0].type = INPUT_MOUSE;
                    inputs[0].mi.dx = 0;
                    inputs[0].mi.dy = 0;
                    inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_ABSOLUTE;
                    SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
                }
                else if (recvbuf[3] == '3' && iResult >= 4) {
                    INPUT inputs[1] = {};
                    inputs[0].type = INPUT_MOUSE;
                    inputs[0].mi.dx = 0;
                    inputs[0].mi.dy = 0;
                    inputs[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_ABSOLUTE;
                    SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
                }
                else if (recvbuf[3] == '4' && iResult >= 4) {
                    INPUT inputs[1] = {};
                    inputs[0].type = INPUT_MOUSE;
                    inputs[0].mi.dx = 0;
                    inputs[0].mi.dy = 0;
                    inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTUP | MOUSEEVENTF_ABSOLUTE;
                    SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
                }
                else if (recvbuf[3] == '5' && iResult >= 4) {
                    INPUT inputs[1] = {};
                    inputs[0].type = INPUT_MOUSE;
                    inputs[0].mi.dx = 0;
                    inputs[0].mi.dy = 0;
                    inputs[0].mi.dwFlags = MOUSEEVENTF_RIGHTUP | MOUSEEVENTF_ABSOLUTE;
                    SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
                }
                else if (recvbuf[3] == '6' && iResult >= 5) {
                    INPUT inputs[1] = {};
                    inputs[0].type = INPUT_KEYBOARD;
                    inputs[0].ki.dwFlags = KEYEVENTF_KEYUP;
                    int command;
                    try {
                        command = stoi(recvbuf.substr(4, iResult - 4));
                    }
                    catch (const exception& e) {
                        continue;
                    }
                    inputs[0].ki.wVk = getWinCommand(command);
                    SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
                }
                    
                    else if (inputBack[packetPos * 30] == '8' && visited[packetPos] >= 5) { // frame retransmit request
                        int sHigh = (uint8_t)inputBack[packetPos * 30 + 1];
                        int sLow = (uint8_t)inputBack[packetPos * 30 + 2];
                        int eHigh = (uint8_t)inputBack[packetPos * 30 + 3];
                        int eLow = (uint8_t)inputBack[packetPos * 30 + 4];
                            if (sHigh < 60 && eHigh < 60) {
                                int beginning = sHigh * maxByteVal + sLow;
                                int end = eHigh * maxByteVal + eLow;
                            int smaller, bigger;
                            int diff = min(abs(beginning - end), maxPacketCount - abs(beginning - end));
                            if (diff == beginning - end || diff == maxPacketCount - (beginning - end)) {
                                smaller = end;
                                bigger = beginning;
                            }
                            else {
                                smaller = beginning;
                                bigger = end;
                            }

                            printDebugMessage(to_string(smaller) + " " + to_string(bigger) + " requested\n");
                            for (int i = smaller; i != bigger; i = (i + 1) % maxPacketCount) {
                                stringstream send;
                                send << (char)FRAMERETRANSMIT << (char)(i / maxByteVal) << (char)(i % maxByteVal);
                                for (int j = 0; j < backupLens[i]; j++) {
                                    send << (char)(backupBuf[i * 1400 + j]);
                                }
                                iResult = sendto(sock, send.str().c_str(), backupLens[i] + 3, 0, (sockaddr*)&dest, sizeof(dest));
                                    retransmitRequest req;
                                    req.start = chrono::steady_clock::now();
                                    retransMutex.lock();
                                    retransmits.emplace(i, req);
                                    retransMutex.unlock();
                                }
                            }
                            else {
                                OutputDebugString(L"bad input received");
                            string out = to_string(visited[packetPos]);
                            for (int i = 0; i < visited[packetPos]; i++) {
                                out += to_string((uint8_t)inputBack[packetPos * 30 + i]) + " ";
                            }
                            printDebugMessage(out);
                        }
                    }
                    
                    visited[packetPos] = -1;
                    printDebugMessage("Packet pos visit: " + to_string(packetPos) + '\n');
                    packetPos++;
                    if (packetPos >= maxPacketCount) {
                        packetPos = 0;
                    }
                }
            }
                                               
        }
                                 
    }
    return 0;
}

DWORD WINAPI KeepAliveProc(_In_ void* Param) {
    THREAD_DATA* TData = reinterpret_cast<THREAD_DATA*>(Param);
    int iResult;
    string send;
    send += (char)KEEPALIVE;
    while ((WaitForSingleObjectEx(TData->TerminateThreadsEvent, 0, FALSE) == WAIT_TIMEOUT)) {
        if (haveClient) {
            iResult = sendto(sock, send.c_str(), send.length() + 1, 0, (sockaddr*)&dest, sizeof(dest));
        }
        Sleep(200);
    }
    return 0;
}

DWORD WINAPI HandleRetransmitsProc(_In_ void* Param) {
    THREAD_DATA* TData = reinterpret_cast<THREAD_DATA*>(Param);
    int iResult;
    while ((WaitForSingleObjectEx(TData->TerminateThreadsEvent, 0, FALSE) == WAIT_TIMEOUT)) {
        if (haveClient) {
            chrono::duration<int, milli> min = retransmitTimeout;
            auto start = chrono::steady_clock::now();
            retransMutex.lock();
            for (auto it = retransmits.begin(); it != retransmits.end(); it++) {
                if (it->second.elapsed < min) {
                    min = it->second.elapsed;
                }
            }
            retransMutex.unlock();
            this_thread::sleep_for(min);
            auto end = chrono::steady_clock::now();
            auto passed = end - start;
            retransMutex.lock();
            for (auto it = retransmits.begin(); it != retransmits.end(); it++) {
                it->second.elapsed -= chrono::milliseconds(passed.count() / 1000000);
                if (it->second.elapsed.count() <= 0) {
                    stringstream send;
                    send << (char) FRAMERETRANSMIT << (char)(it->first / maxByteVal) << (char)(it->first % maxByteVal); 
                    for (int i = 0; i < backupLens[it->first]; i++) {
                        send << (char)(backupBuf[it->first * 1400 + i]);
                    }
                    iResult = sendto(sock, send.str().c_str(), backupLens[it->first] + 3, 0, (sockaddr*)&dest, sizeof(dest));
                    it->second.elapsed = retransmitTimeout;
                }
            }
            retransMutex.unlock();

        } else {
            Sleep(100);
        }
    }
    return 0;
}

_Post_satisfies_(return != DUPL_RETURN_SUCCESS)
DUPL_RETURN ProcessFailure(_In_opt_ ID3D11Device* Device, _In_ LPCWSTR Str, _In_ LPCWSTR Title, HRESULT hr, _In_opt_z_ HRESULT* ExpectedErrors)
{
    HRESULT TranslatedHr;

    // On an error check if the DX device is lost
    if (Device)
    {
        HRESULT DeviceRemovedReason = Device->GetDeviceRemovedReason();

        switch (DeviceRemovedReason)
        {
            case DXGI_ERROR_DEVICE_REMOVED :
            case DXGI_ERROR_DEVICE_RESET :
            case static_cast<HRESULT>(E_OUTOFMEMORY) :
            {
                // Our device has been stopped due to an external event on the GPU so map them all to
                // device removed and continue processing the condition
                TranslatedHr = DXGI_ERROR_DEVICE_REMOVED;
                break;
            }

            case S_OK :
            {
                // Device is not removed so use original error
                TranslatedHr = hr;
                break;
            }

            default :
            {
                // Device is removed but not a error we want to remap
                TranslatedHr = DeviceRemovedReason;
            }
        }
    }
    else
    {
        TranslatedHr = hr;
    }

    // Check if this error was expected or not
    if (ExpectedErrors)
    {
        HRESULT* CurrentResult = ExpectedErrors;

        while (*CurrentResult != S_OK)
        {
            if (*(CurrentResult++) == TranslatedHr)
            {
                return DUPL_RETURN_ERROR_EXPECTED;
            }
        }
    }

    // Error was not expected so display the message box
    DisplayMsg(Str, Title, TranslatedHr);

    return DUPL_RETURN_ERROR_UNEXPECTED;
}

//
// Displays a message
//
void DisplayMsg(_In_ LPCWSTR Str, _In_ LPCWSTR Title, HRESULT hr)
{
    if (SUCCEEDED(hr))
    {
        MessageBoxW(nullptr, Str, Title, MB_OK);
        return;
    }

    const UINT StringLen = (UINT)(wcslen(Str) + sizeof(" with HRESULT 0x########."));
    wchar_t* OutStr = new wchar_t[StringLen];
    if (!OutStr)
    {
        return;
    }

    INT LenWritten = swprintf_s(OutStr, StringLen, L"%s with 0x%X.", Str, hr);
    if (LenWritten != -1)
    {
        MessageBoxW(nullptr, OutStr, Title, MB_OK);
    }

    delete [] OutStr;
}

int DisplayConfirmation(_In_ LPCWSTR Str, _In_ LPCWSTR Title)
{
   int choice = MessageBox(nullptr, Str, Title, MB_OK | MB_YESNO);
   return choice;
      
}