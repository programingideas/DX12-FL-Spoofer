#include <windows.h>
#include <iostream>
#include <string>

int wmain(int argc, wchar_t* argv[]) {
    std::wcout << L"=== DX12-FL-Spoofer v0.1 (GTX 750 Ti / Legacy GPU Enabler) ===\n";

    if (argc < 2) {
        std::wcout << L"Usage: Launcher.exe <path_to_game.exe> [--vkd3d]\n";
        std::wcout << L"Tip: Drag and drop your game .exe onto Launcher.exe!\n";
        system("pause");
        return 1;
    }

    std::wstring gamePath = argv[1];
    bool useVkd3d = false;

    for (int i = 2; i < argc; ++i) {
        if (std::wstring(argv[i]) == L"--vkd3d") {
            useVkd3d = true;
        }
    }

    SetEnvironmentVariableW(L"SPOOFER_USE_VKD3D", useVkd3d ? L"1" : L"0");
    std::wcout << L"[*] Mode: " << (useVkd3d ? L"Spoof FL 12_0 + vkd3d-proton Vulkan\n" : L"Native DX12 FL 12_0 Spoof\n");

    wchar_t launcherDir[MAX_PATH];
    GetModuleFileNameW(NULL, launcherDir, MAX_PATH);
    std::wstring dllPath(launcherDir);
    dllPath = dllPath.substr(0, dllPath.find_last_of(L"\\/")) + L"\\SpooferDLL.dll";

    std::wstring gameDir = gamePath.substr(0, gamePath.find_last_of(L"\\/"));

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };

    if (!CreateProcessW(gamePath.c_str(), NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, gameDir.c_str(), &si, &pi)) {
        std::wcerr << L"[!] Failed to launch game. Error: " << GetLastError() << L"\n";
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

    std::wcout << L"[+] SpooferDLL injected! Resuming game...\n";
    ResumeThread(pi.hThread);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
