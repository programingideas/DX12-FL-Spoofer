#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <fstream>
#include <string>
#include "MinHook.h"

#pragma comment(lib, "d3d12.lib")

#ifndef D3D_FEATURE_LEVEL_12_2
#define D3D_FEATURE_LEVEL_12_2 ((D3D_FEATURE_LEVEL)0xc200)
#endif

typedef HRESULT(WINAPI* PFN_D3D12CreateDevice)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
typedef HRESULT(WINAPI* PFN_CheckFeatureSupport)(ID3D12Device*, D3D12_FEATURE, void*, UINT);
typedef HMODULE(WINAPI* PFN_LoadLibraryExW)(LPCWSTR, HANDLE, DWORD);
typedef HMODULE(WINAPI* PFN_LoadLibraryW)(LPCWSTR);

PFN_D3D12CreateDevice orig_D3D12CreateDevice = nullptr;
PFN_CheckFeatureSupport orig_CheckFeatureSupport = nullptr;
PFN_LoadLibraryExW orig_LoadLibraryExW = nullptr;
PFN_LoadLibraryW orig_LoadLibraryW = nullptr;

bool g_UseVkd3d = false;
HMODULE g_Vkd3dModule = nullptr;
std::wstring g_SpooferDir;

void Log(const std::string& msg) {
    std::ofstream logFile(g_SpooferDir + L"\\spoofer.log", std::ios::app);
    if (logFile.is_open()) {
        logFile << msg << "\n";
    }
}

// Probe the highest Feature Level the physical GPU actually supports (12_1 -> 12_0 -> 11_0)
D3D_FEATURE_LEVEL DetectNativeMaxFeatureLevel(IUnknown* pAdapter) {
    if (SUCCEEDED(orig_D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_12_1, __uuidof(ID3D12Device), nullptr))) {
        return D3D_FEATURE_LEVEL_12_1;
    }
    if (SUCCEEDED(orig_D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr))) {
        return D3D_FEATURE_LEVEL_12_0;
    }
    if (SUCCEEDED(orig_D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_11_1, __uuidof(ID3D12Device), nullptr))) {
        return D3D_FEATURE_LEVEL_11_1;
    }
    return D3D_FEATURE_LEVEL_11_0;
}

HRESULT WINAPI hooked_CheckFeatureSupport(ID3D12Device* pThis, D3D12_FEATURE Feature, void* pFeatureSupportData, UINT FeatureSupportDataSize) {
    HRESULT hr = orig_CheckFeatureSupport(pThis, Feature, pFeatureSupportData, FeatureSupportDataSize);
    if (!pFeatureSupportData) return hr;

    // 1. Spoof Feature Levels up to 12_2 (DX12 Ultimate) for Maxwell (11_0), Polaris (12_0), and Pascal/Turing (12_1)
    if (SUCCEEDED(hr) && Feature == D3D12_FEATURE_FEATURE_LEVELS) {
        auto* pLevels = static_cast<D3D12_FEATURE_DATA_FEATURE_LEVELS*>(pFeatureSupportData);
        if (pLevels->MaxSupportedFeatureLevel < D3D_FEATURE_LEVEL_12_2) {
            pLevels->MaxSupportedFeatureLevel = D3D_FEATURE_LEVEL_12_2;
            Log("[HOOK] CheckFeatureSupport: Spoofed MaxSupportedFeatureLevel -> 12_2 (DX12 Ultimate)");
        }
    }
    // 2. Spoof Resource Binding Tier 3 (Bindless Textures)
    else if (SUCCEEDED(hr) && Feature == D3D12_FEATURE_D3D12_OPTIONS) {
        auto* pOpts = static_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS*>(pFeatureSupportData);
        if (pOpts->ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_3) {
            pOpts->ResourceBindingTier = D3D12_RESOURCE_BINDING_TIER_3;
            Log("[HOOK] CheckFeatureSupport: Spoofed ResourceBindingTier -> Tier 3");
        }
    }
    // 3. Spoof Raytracing Tier 1.1 (Bypasses hardcoded RT startup gates like DOOM Dark Ages / Gears E-Day)
    else if (Feature == D3D12_FEATURE_D3D12_OPTIONS5) {
        auto* pOpts5 = static_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS5*>(pFeatureSupportData);
        if (FAILED(hr) || pOpts5->RaytracingTier < D3D12_RAYTRACING_TIER_1_1) {
            pOpts5->RaytracingTier = D3D12_RAYTRACING_TIER_1_1;
            hr = S_OK;
            Log("[HOOK] CheckFeatureSupport: Spoofed RaytracingTier -> Tier 1.1");
        }
    }
    // 4. Spoof Mesh Shader Tier 1 (Bypasses DX12 Ultimate Mesh Shader gates)
    else if (Feature == D3D12_FEATURE_D3D12_OPTIONS7) {
        auto* pOpts7 = static_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS7*>(pFeatureSupportData);
        if (FAILED(hr) || pOpts7->MeshShaderTier < D3D12_MESH_SHADER_TIER_1) {
            pOpts7->MeshShaderTier = D3D12_MESH_SHADER_TIER_1;
            hr = S_OK;
            Log("[HOOK] CheckFeatureSupport: Spoofed MeshShaderTier -> Tier 1");
        }
    }
    // 5. Spoof Shader Model 6.6
    else if (Feature == D3D12_FEATURE_SHADER_MODEL) {
        auto* pSM = static_cast<D3D12_FEATURE_DATA_SHADER_MODEL*>(pFeatureSupportData);
        D3D_SHADER_MODEL targetSM = static_cast<D3D_SHADER_MODEL>(0x66);
        if (FAILED(hr) || pSM->HighestShaderModel < targetSM) {
            pSM->HighestShaderModel = targetSM;
            hr = S_OK;
            Log("[HOOK] CheckFeatureSupport: Spoofed HighestShaderModel -> SM 6.6");
        }
    }
    return hr;
}

HRESULT WINAPI hooked_D3D12CreateDevice(IUnknown* pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid, void** ppDevice) {
    D3D_FEATURE_LEVEL actualLevel = MinimumFeatureLevel;

    if (!g_UseVkd3d) {
        D3D_FEATURE_LEVEL nativeMax = DetectNativeMaxFeatureLevel(pAdapter);
        if (MinimumFeatureLevel > nativeMax) {
            actualLevel = nativeMax;
            if (nativeMax == D3D_FEATURE_LEVEL_11_0) {
                Log("[HOOK] D3D12CreateDevice: GTX 750 Ti / Maxwell 1.0 detected -> Safe fallback to FL 11_0");
            } else if (nativeMax == D3D_FEATURE_LEVEL_12_0) {
                Log("[HOOK] D3D12CreateDevice: GCN/Maxwell 2.0 detected -> Safe fallback to FL 12_0");
            } else {
                Log("[HOOK] D3D12CreateDevice: Pascal/Turing detected -> Safe fallback to FL 12_1");
            }
        }
    }

    PFN_D3D12CreateDevice targetCreateDevice = orig_D3D12CreateDevice;

    if (g_UseVkd3d && g_Vkd3dModule) {
        auto vkd3dCreate = (PFN_D3D12CreateDevice)GetProcAddress(g_Vkd3dModule, "D3D12CreateDevice");
        if (vkd3dCreate) {
            targetCreateDevice = vkd3dCreate;
            actualLevel = MinimumFeatureLevel;
            Log("[HOOK] Routing D3D12CreateDevice -> vkd3d-proton (Vulkan 1.3)");
        }
    }

    HRESULT hr = targetCreateDevice(pAdapter, actualLevel, riid, ppDevice);

    if (SUCCEEDED(hr) && ppDevice && *ppDevice && !orig_CheckFeatureSupport) {
        void** vtable = *reinterpret_cast<void***>(*ppDevice);
        void* pCheckFeature = *(vtable + 13);
        if (MH_CreateHook(pCheckFeature, &hooked_CheckFeatureSupport, reinterpret_cast<void**>(&orig_CheckFeatureSupport)) == MH_OK) {
            MH_EnableHook(pCheckFeature);
            Log("[HOOK] Installed ID3D12Device::CheckFeatureSupport VTable hook");
        }
    }
    return hr;
}

bool IsD3D12Dll(LPCWSTR lpLibFileName) {
    if (!lpLibFileName) return false;
    std::wstring lib(lpLibFileName);
    return (lib.find(L"d3d12.dll") != std::wstring::npos || lib.find(L"D3D12.dll") != std::wstring::npos);
}

HMODULE WINAPI hooked_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) {
    if (g_UseVkd3d && g_Vkd3dModule && IsD3D12Dll(lpLibFileName)) {
        Log("[HOOK] LoadLibraryExW: Intercepted d3d12.dll -> Returning vkd3d-proton handle");
        return g_Vkd3dModule;
    }
    return orig_LoadLibraryExW(lpLibFileName, hFile, dwFlags);
}

HMODULE WINAPI hooked_LoadLibraryW(LPCWSTR lpLibFileName) {
    if (g_UseVkd3d && g_Vkd3dModule && IsD3D12Dll(lpLibFileName)) {
        Log("[HOOK] LoadLibraryW: Intercepted d3d12.dll -> Returning vkd3d-proton handle");
        return g_Vkd3dModule;
    }
    return orig_LoadLibraryW(lpLibFileName);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        std::wstring dllPath(MAX_PATH, L'\0');
        DWORD len = GetModuleFileNameW(hModule, dllPath.data(), MAX_PATH);
        dllPath.resize(len);
        g_SpooferDir = dllPath.substr(0, dllPath.find_last_of(L"\\/"));

        std::wstring envBuf(16, L'\0');
        DWORD envLen = GetEnvironmentVariableW(L"SPOOFER_USE_VKD3D", envBuf.data(), 16);
        if (envLen > 0) {
            envBuf.resize(envLen);
            if (envBuf == L"1") {
                g_UseVkd3d = true;
                std::wstring vkd3dPath = g_SpooferDir + L"\\vkd3d\\d3d12.dll";
                g_Vkd3dModule = LoadLibraryW(vkd3dPath.c_str());
            }
        }

        Log("=== SpooferDLL v0.3 (DX12 Ultimate Edition) Attached ===");

        if (MH_Initialize() == MH_OK) {
            HMODULE hD3D12 = LoadLibraryW(L"d3d12.dll");
            if (hD3D12) {
                void* pCreateDevice = GetProcAddress(hD3D12, "D3D12CreateDevice");
                MH_CreateHook(pCreateDevice, &hooked_D3D12CreateDevice, reinterpret_cast<void**>(&orig_D3D12CreateDevice));
            }
            MH_CreateHookApi(L"kernelbase.dll", "LoadLibraryExW", &hooked_LoadLibraryExW, reinterpret_cast<void**>(&orig_LoadLibraryExW));
            MH_CreateHookApi(L"kernelbase.dll", "LoadLibraryW", &hooked_LoadLibraryW, reinterpret_cast<void**>(&orig_LoadLibraryW));
            MH_EnableHook(MH_ALL_HOOKS);
        }
    } else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        MH_Uninitialize();
    }
    return TRUE;
}
