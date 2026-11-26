#include <windows.h>
#include <commdlg.h>
#include <iostream>
#include <string>

#pragma comment(lib, "comdlg32.lib")

std::wstring OpenFileDialog() {
    wchar_t fileName[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"Game Executables (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Select DX12 Game Executable";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn)) {
        return std::wstring(fileName);
    }
    return L"";
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleTitleW(L"DX12-FL-Spoofer v0.2 - Legacy GPU Enabler");
    std::wcout << L"==========================================================\n";
    std::wcout << L"  DX12-FL-Spoofer v0.2 (GTX 750 Ti / Maxwell / Kepler)\n";
    std::wcout << L"==========================================================\n\n";

    std::wstring gamePath;
    bool useVkd3d = false;

    if (argc >= 2) {
        gamePath = *(argv + 1);
        for (int i = 2; i < argc; ++i) {
            if (std::wstring(*(argv + i)) == L"--vkd3d") {
                useVkd3d = true;
            }
        }
    } else {
        std::wcout << L"[*] Opening File Explorer to select your game .exe...\n";
        gamePath = OpenFileDialog();
        if (gamePath.empty()) {
            std::wcout << L"[!] No game selected. Exiting.\n";
            return 0;
        }

        std::wcout << L"[+] Selected: " << gamePath << L"\n\n";
        std::wcout << L"Choose Spoofing Backend:\n";
        std::wcout << L"  [1] Native Windows DX12 (Best for UE5 / GoW Ragnarok / Phantom Blade)\n";
        std::wcout << L"  [2] vkd3d-proton Vulkan 1.3 (Best for RE9 / TLOU Part 2 / Ghost of Tsushima)\n\n";
        std::wcout << L"Enter choice (1 or 2): ";

        int choice = 1;
        std::wcin >> choice;
        useVkd3d = (choice == 2);
    }

    SetEnvironmentVariableW(L"SPOOFER_USE_VKD3D", useVkd3d ? L"1" : L"0");
    std::wcout << L"\n[*] Active Mode: " << (useVkd3d ? L"FL 12_0 + Tier 3 Spoof -> vkd3d-proton Vulkan 1.3\n" : L"Native DX12 FL 12_0 + Tier 3 Spoof\n");

    wchar_t launcherDir[MAX_PATH];
    GetModuleFileNameW(NULL, launcherDir, MAX_PATH);
    std::wstring dllPath(launcherDir);
    dllPath = dllPath.substr(0, dllPath.find_last_of(L"\\/")) + L"\\SpooferDLL.dll";

    std::wstring gameDir = gamePath.substr(0, gamePath.find_last_of(L"\\/"));

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };

    if (!CreateProcessW(gamePath.c_str(), NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, gameDir.c_str(), &si, &pi)) {
        std::wcerr << L"[!] Failed to launch game. Error code: " << GetLastError() << L"\n";
        system("pause");
        return 1;
    }

    size_t pathSize = (dllPath.size() + 1) * sizeof(wchar_t);
    LPVOID remoteMem = VirtualAllocEx(pi.hProcess, NULL, pathSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    WriteProcessMemory(pi.hProcess, remoteMem, dllPath.c_str(), pathSize, NULL);

    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    LPTHREAD_START_ROUTINE loadLibAddr = (LPTHREAD_START_ROUTINE)GetProcAddress(hKernel32, "LoadLibraryW");

    HANDLE hThread = CreateRemoteThread(pi.hProcess, NULL, 0, loadLibAddr, remoteMem, 0, NULL);
    if (hThread) {
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
    }

    VirtualFreeEx(pi.hProcess, remoteMem, 0, MEM_RELEASE);

    std::wcout << L"[+] SpooferDLL injected! Resuming game process...\n";
    ResumeThread(pi.hThread);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    Sleep(1500);
    return 0;
}
