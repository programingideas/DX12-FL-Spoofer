#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <fstream>
#include <string>
#include "MinHook.h"

#pragma comment(lib, "d3d12.lib")

typedef HRESULT(WINAPI* PFN_D3D12CreateDevice)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
typedef HRESULT(WINAPI* PFN_CheckFeatureSupport)(ID3D12Device*, D3D12_FEATURE, void*, UINT);
typedef HMODULE(WINAPI* PFN_LoadLibraryExW)(LPCWSTR, HANDLE, DWORD);

PFN_D3D12CreateDevice orig_D3D12CreateDevice = nullptr;
PFN_CheckFeatureSupport orig_CheckFeatureSupport = nullptr;
PFN_LoadLibraryExW orig_LoadLibraryExW = nullptr;

bool g_UseVkd3d = false;
HMODULE g_Vkd3dModule = nullptr;
std::wstring g_SpooferDir;

void Log(const std::string& msg) {
    std::ofstream logFile(g_SpooferDir + L"\\spoofer.log", std::ios::app);
    if (logFile.is_open()) {
        logFile << msg << "\n";
    }
}

// VTable Hook #13: Spoof CheckFeatureSupport so games think FL 12_0 is natively supported
HRESULT WINAPI hooked_CheckFeatureSupport(ID3D12Device* pThis, D3D12_FEATURE Feature, void* pFeatureSupportData, UINT FeatureSupportDataSize) {
    HRESULT hr = orig_CheckFeatureSupport(pThis, Feature, pFeatureSupportData, FeatureSupportDataSize);
    if (SUCCEEDED(hr) && Feature == D3D12_FEATURE_FEATURE_LEVELS && pFeatureSupportData) {
        auto* pLevels = static_cast<D3D12_FEATURE_DATA_FEATURE_LEVELS*>(pFeatureSupportData);
        if (pLevels->MaxSupportedFeatureLevel < D3D_FEATURE_LEVEL_12_0) {
            pLevels->MaxSupportedFeatureLevel = D3D_FEATURE_LEVEL_12_0;
            Log("[HOOK] CheckFeatureSupport: Spoofed MaxSupportedFeatureLevel -> 12_0");
        }
    }
    return hr;
}

// Intercept D3D12CreateDevice: Downgrade FL 12_0 request -> 11_0 or route to vkd3d-proton
HRESULT WINAPI hooked_D3D12CreateDevice(IUnknown* pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid, void** ppDevice) {
    D3D_FEATURE_LEVEL actualLevel = MinimumFeatureLevel;
    if (MinimumFeatureLevel > D3D_FEATURE_LEVEL_11_0) {
        actualLevel = D3D_FEATURE_LEVEL_11_0;
        Log("[HOOK] D3D12CreateDevice: Intercepted FL >= 12_0 request, spoofing with FL 11_0");
    }

    PFN_D3D12CreateDevice targetCreateDevice = orig_D3D12CreateDevice;

    // If vkd3d mode is active, forward device creation to bundled vkd3d-proton d3d12.dll
    if (g_UseVkd3d && g_Vkd3dModule) {
        auto vkd3dCreate = (PFN_D3D12CreateDevice)GetProcAddress(g_Vkd3dModule, "D3D12CreateDevice");
        if (vkd3dCreate) {
            targetCreateDevice = vkd3dCreate;
            actualLevel = MinimumFeatureLevel; // vkd3d-proton handles 12_0 natively over Vulkan 1.3
            Log("[HOOK] Routing D3D12CreateDevice -> vkd3d-proton (Vulkan 1.3)");
        }
    }

    HRESULT hr = targetCreateDevice(pAdapter, actualLevel, riid, ppDevice);

    // Hook ID3D12Device::CheckFeatureSupport on the created device
    if (SUCCEEDED(hr) && ppDevice && *ppDevice && !orig_CheckFeatureSupport) {
        void** vtable = *reinterpret_cast<void***>(*ppDevice);
        if (MH_CreateHook(vtable[13], &hooked_CheckFeatureSupport, reinterpret_cast<void**>(&orig_CheckFeatureSupport)) == MH_OK) {
            MH_EnableHook(vtable[13]);
            Log("[HOOK] Installed ID3D12Device::CheckFeatureSupport VTable hook");
        }
    }
    return hr;
}

// Catch games trying to bypass folder proxies by calling System32 directly
HMODULE WINAPI hooked_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) {
    if (g_UseVkd3d && g_Vkd3dModule && lpLibFileName) {
        std::wstring lib(lpLibFileName);
        if (lib.find(L"d3d12.dll") != std::wstring::npos || lib.find(L"D3D12.dll") != std::wstring::npos) {
            Log("[HOOK] Blocked System32 d3d12.dll bypass -> Returning vkd3d-proton handle");
            return g_Vkd3dModule;
        }
    }
    return orig_LoadLibraryExW(lpLibFileName, hFile, dwFlags);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        wchar_t dllPath[MAX_PATH];
        GetModuleFileNameW(hModule, dllPath, MAX_PATH);
        std::wstring pathStr(dllPath);
        g_SpooferDir = pathStr.substr(0, pathStr.find_last_of(L"\\/"));

        wchar_t envBuf[8] = { 0 };
        if (GetEnvironmentVariableW(L"SPOOFER_USE_VKD3D", envBuf, 8) && wcscmp(envBuf, L"1") == 0) {
            g_UseVkd3d = true;
            std::wstring vkd3dPath = g_SpooferDir + L"\\vkd3d\\d3d12.dll";
            g_Vkd3dModule = LoadLibraryW(vkd3dPath.c_str());
        }

        Log("=== SpooferDLL Attached ===");

        if (MH_Initialize() == MH_OK) {
            HMODULE hD3D12 = LoadLibraryW(L"d3d12.dll");
            if (hD3D12) {
                void* pCreateDevice = GetProcAddress(hD3D12, "D3D12CreateDevice");
                MH_CreateHook(pCreateDevice, &hooked_D3D12CreateDevice, reinterpret_cast<void**>(&orig_D3D12CreateDevice));
            }
            MH_CreateHookApi(L"kernelbase.dll", "LoadLibraryExW", &hooked_LoadLibraryExW, reinterpret_cast<void**>(&orig_LoadLibraryExW));
            MH_EnableHook(MH_ALL_HOOKS);
        }
    } else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        MH_Uninitialize();
    }
    return TRUE;
}
