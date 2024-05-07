// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved


//------->changes starts here
#define WIN32_LEAN_AND_MEAN //inside windows.h winsock.h include is ommitted
//winsock api add ons
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winuser.h>
#include <stdlib.h>
#include <stdio.h>
#include <thread>

// Need to link with Ws2_32.lib, Mswsock.lib, and Advapi32.lib
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")


#define DEFAULT_BUFLEN 512
#define DEFAULT_PORT "8080"
//------->changes ends here


#include <limits.h>

#include "DisplayManager.h"
#include "DuplicationManager.h"
#include "H264Encoder2.h"
#include "Preproc.h"
#include "OutputManager.h"
#include "ThreadManager.h"
#include <mfapi.h>
#include <fstream>
#include <string>


#pragma comment(lib, "mfplat")
#pragma comment(lib, "mfuuid")

//
// Globals
//
OUTPUTMANAGER OutMgr;
const UINT32 VIDEO_WIDTH = 1920;
const UINT32 VIDEO_HEIGHT = 1080;
const UINT32 VIDEO_FPS = 30;
H264Encoder2* encoder = NULL;
INT64 rtStart = 0;

WSADATA wsaData;
SOCKET server = INVALID_SOCKET;
SOCKET ClientSocket = INVALID_SOCKET;
struct addrinfo* result = NULL;
struct addrinfo hints;
int iResult;
int recvbuflen = 10240;
char recvbuf[10240];

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
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
bool ProcessCmdline(_Out_ INT* Output);
void ShowHelp();

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

HRESULT InitializeTransform()
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr)) {
        HRESULT hr = MFStartup(MF_VERSION, 0);
    }

    if (SUCCEEDED(hr)) {
        encoder = new H264Encoder2();
    }

    if (SUCCEEDED(hr)) {
        hr = encoder->Initialize(1920, 1080);
    }
    return hr;
}

static HRESULT CreateMediaSample(IMFSample** ppSample, FRAME_DATA curData)
{
    HRESULT hr = S_OK;

    IMFSample* pSample = NULL;
    IMFMediaBuffer* pBuffer = NULL;

    D3D11_TEXTURE2D_DESC desc;
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    ID3D11Texture2D* pTexture = NULL;

    curData.Frame->GetDevice(&device);
    device->GetImmediateContext(&context);
    RGBToNV12 converter(device, context);

    desc.Width = VIDEO_WIDTH;
    desc.Height = VIDEO_HEIGHT;
    desc.MipLevels = desc.ArraySize = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    hr = device->CreateTexture2D(&desc, NULL, &pTexture);

    if (SUCCEEDED(hr)) {
        hr = MFCreateSample(&pSample);
    }
    if (SUCCEEDED(hr)) {
        hr = converter.Init();
    }

    if (SUCCEEDED(hr)) {
        converter.Convert(curData.Frame, pTexture);
    }

    if (SUCCEEDED(hr))
    {
        hr = MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), pTexture, 0, FALSE, &pBuffer);
    }

    if (SUCCEEDED(hr))
    {
        hr = pSample->AddBuffer(pBuffer);
    }

    if (SUCCEEDED(hr))
    {
        *ppSample = pSample;
        (*ppSample)->AddRef();
    }

    SafeRelease(&pSample);
    SafeRelease(&pBuffer);
    SafeRelease(&pTexture);
    return hr;
}

HRESULT WriteFrame(FRAME_DATA curData)
{
    HRESULT hr = S_OK;

    IMFSample* pSample = NULL;
    IMFSample* pSampleOut = NULL;
    UINT64 rtDuration;

    hr = MFFrameRateToAverageTimePerFrame(VIDEO_FPS, 1, &rtDuration);
    hr = CreateMediaSample(&pSample, curData);
    pSample->SetSampleDuration(rtDuration);
    hr = pSample->SetSampleTime(rtStart);
    rtStart += rtDuration;
    hr = encoder->ProcessInput(pSample);
    hr = encoder->ProcessOutput(&pSampleOut);
    IMFMediaBuffer* outBuffer = NULL;
    pSampleOut->GetBufferByIndex(0, &outBuffer);

    BYTE* data;
    DWORD length;

    outBuffer->Lock(&data, NULL, &length); 
    // Send an initial buffer
    iResult = send(ClientSocket, (char*)data, length, 0);
    if (iResult == SOCKET_ERROR) {
        DisplayMsg(L"Send failed with error \n", L"Send Fail", E_FAIL);
        closesocket(ClientSocket);
        WSACleanup();
        return 1;
    }

    if (iResult == SOCKET_ERROR) {
        DisplayMsg(L"Shutdown failed with error: \n", L"Shutdown Fail", E_FAIL);
        closesocket(ClientSocket);
        WSACleanup();
        return 1;
    }

    outBuffer->Unlock();
    SafeRelease(&pSample);
    SafeRelease(&pSampleOut);
    return hr;
}


//
// Program entry point
//
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ INT nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    INT SingleOutput;

    // Synchronization
    HANDLE UnexpectedErrorEvent = nullptr;
    HANDLE ExpectedErrorEvent = nullptr;
    HANDLE TerminateThreadsEvent = nullptr;

    // Window
    HWND WindowHandle = nullptr;

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

    HRESULT hr = InitializeTransform();
    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed with error: %d\n", iResult);
        return 1;
    }
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;
    // Resolve the server address and port
    iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);
    if (iResult != 0) {
        printf("getaddrinfo failed with error: %d\n", iResult);
        WSACleanup();
        return 1;
    }
    
    server = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (server == INVALID_SOCKET) {
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    freeaddrinfo(result);

    if (iResult == SOCKET_ERROR) {
        closesocket(server);
        WSACleanup();
        return 1;
    }

    while (ClientSocket == INVALID_SOCKET) {
        ClientSocket = accept(server, NULL, NULL);
    }
    if (ClientSocket == INVALID_SOCKET) {
        closesocket(server);
        WSACleanup();
        return 1;
    }
    
    // Register class
    WNDCLASSEXW Wc;
    Wc.cbSize           = sizeof(WNDCLASSEXW);
    Wc.style            = CS_HREDRAW | CS_VREDRAW;
    Wc.lpfnWndProc      = WndProc;
    Wc.cbClsExtra       = 0;
    Wc.cbWndExtra       = 0;
    Wc.hInstance        = hInstance;
    Wc.hIcon            = nullptr;
    Wc.hCursor          = Cursor;
    Wc.hbrBackground    = nullptr;
    Wc.lpszMenuName     = nullptr;
    Wc.lpszClassName    = L"ddasample";
    Wc.hIconSm          = nullptr;
    if (!RegisterClassExW(&Wc))
    {
        ProcessFailure(nullptr, L"Window class registration failed", L"Error", E_UNEXPECTED);
        return 0;
    }

    // Create window
    RECT WindowRect = {0, 0, 800, 600};
    AdjustWindowRect(&WindowRect, WS_OVERLAPPEDWINDOW, FALSE);
    WindowHandle = CreateWindowW(L"ddasample", L"Remote Desktop App",
                           WS_OVERLAPPEDWINDOW,
                           0, 0,
                           WindowRect.right - WindowRect.left, WindowRect.bottom - WindowRect.top,
                           nullptr, nullptr, hInstance, nullptr);
    if (!WindowHandle)
    {
        ProcessFailure(nullptr, L"Window creation failed", L"Error", E_FAIL);
        return 0;
    }

    DestroyCursor(Cursor);

    ShowWindow(WindowHandle, nCmdShow);
    UpdateWindow(WindowHandle);

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
            Ret = OutMgr.InitOutput(WindowHandle, SingleOutput, &OutputCount, &DeskBounds);
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
        ThreadMgr.WaitForThreadTermination();
    }

    // Clean up
    CloseHandle(UnexpectedErrorEvent);
    CloseHandle(ExpectedErrorEvent);
    CloseHandle(TerminateThreadsEvent);
    CoUninitialize();
    MFShutdown();
    closesocket(ClientSocket);
    closesocket(server);
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

//
// Window message processor
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_DESTROY:
        {
            PostQuitMessage(0);
            break;
        }
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
            hr = WriteFrame(CurrentData);
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
    // Receive until the peer closes the connection
    string output;
    while ((WaitForSingleObjectEx(TData->TerminateThreadsEvent, 0, FALSE) == WAIT_TIMEOUT)) {
        ZeroMemory(recvbuf, recvbuflen);
<<<<<<< Updated upstream
        iResult = recv(ClientSocket, recvbuf, recvbuflen, 0);
        string output = recvbuf;
=======
        iResult = recv(ConnectSocket, recvbuf, recvbuflen, 0);
        output = recvbuf;
>>>>>>> Stashed changes
        if (output[0] == '0') {
            INPUT inputs[1] = {};
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki.wVk = toupper(output[1]);
            SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
        }
        if(output[0] == '1') {
            INPUT inputs[1] = {};
            inputs[0].type = INPUT_MOUSE;                                                                                                                                                                                                                              
            string x;
            int i = 1;
            while (i < output.length() && output[i] != 'a') {
                x += output[i];
                i++;
            }
            inputs[0].mi.dx = (int) (65535 * stof(x.c_str()));
            i++;
            x = ""; 
            while (i < output.length()) {
                x += output[i];
                i++;
            }
            inputs[0].mi.dy = (int) (65535 * stof(x.c_str()));
            inputs[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
            SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
        }
        else if (output[0] == '2') {
            INPUT inputs[1] = {};
            inputs[0].type = INPUT_MOUSE;
            inputs[0].mi.dx = 0;
            inputs[0].mi.dy = 0;
            inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_ABSOLUTE;
            SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
        } else if (output[0] == '3') {
            INPUT inputs[1] = {};
            inputs[0].type = INPUT_MOUSE;
            inputs[0].mi.dx = 0;
            inputs[0].mi.dy = 0;
            inputs[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_ABSOLUTE;
            SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
        } else if (output[0] == '4') {
            INPUT inputs[1] = {};
            inputs[0].type = INPUT_MOUSE;
            inputs[0].mi.dx = 0;
            inputs[0].mi.dy = 0;
            inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTUP | MOUSEEVENTF_ABSOLUTE;
            SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
        } else if (output[0] == '5') {
            INPUT inputs[1] = {};
            inputs[0].type = INPUT_MOUSE;
            inputs[0].mi.dx = 0;
            inputs[0].mi.dy = 0;
            inputs[0].mi.dwFlags = MOUSEEVENTF_RIGHTUP | MOUSEEVENTF_ABSOLUTE;
            SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
        } else if (output[0] == '6') {
            INPUT inputs[1] = {};
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
            inputs[0].ki.wVk = VK_LSHIFT;
            SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
        } else if (output[0] == '7') {
            INPUT inputs[1] = {};
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki.dwFlags = KEYEVENTF_KEYUP | KEYEVENTF_EXTENDEDKEY;
            inputs[0].ki.wVk = VK_LSHIFT;
            SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
        }
        
        
        if (iResult > 0) {  
            printf("Bytes received: %d\n", iResult);
            //SendInput(sizeof(inputs), inputs, sizeof(INPUT));
        }
        else if (iResult == 0)
            printf("Connection closed\n");
        else
            printf("recv failed with error: %d\n", WSAGetLastError());
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
