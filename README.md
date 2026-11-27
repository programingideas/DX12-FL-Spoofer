# DX12-FL-Spoofer (v0.3 — DX12 Ultimate Edition)

**Universal Windows DirectX 12 Feature Level (`11_0` / `12_0` / `12_1` -> `12_2`) & DX12 Ultimate Spoofer + `vkd3d-proton` Vulkan 1.3 Launcher for Legacy & Non-RT GPUs.**

## Supported GPUs (Auto-Detected Safely)
* **NVIDIA Maxwell 1.0 (`11_0`):** GTX 750 Ti, GTX 750, GTX 745, GTX 850M / 860M / 950M / 960M
* **NVIDIA Maxwell 2.0 (`12_1`):** GTX 950, GTX 960, GTX 970, GTX 980, GTX 980 Ti, TITAN X
* **NVIDIA Pascal & Turing (`12_1`):** GTX 1030, 1050 Ti, 1060, 1070, 1080 Ti, GTX 1650, 1660 Super / Ti
* **AMD GCN / Polaris (`12_0`):** RX 460, RX 470, RX 480, RX 550, RX 570, RX 580, RX 590, R9 Fury

## Features (v0.3)
* **Safe Native Feature-Level Probing:** Detects your GPU's exact highest supported Feature Level (`11_0`, `12_0`, or `12_1`) so older cards never get overloaded with unsupported native calls while newer cards keep full native speed.
* **Full DX12 Ultimate (`12_2`) Spoofing:** Spoofs `D3D_FEATURE_LEVEL_12_2` (`0xc200`), `ResourceBindingTier` (Tier 3), `RaytracingTier` (Tier 1.1), `MeshShaderTier` (Tier 1), and `ShaderModel` (6.6) via `ID3D12Device::CheckFeatureSupport`.
* **Suspended-Process Injection:** Launches games in `CREATE_SUSPENDED` state and hooks DirectX 12 before pre-launch hardware checks or `System32` bypasses execute.
* **Dual Backend Modes:**
  * **Mode 1 (Native Windows DX12):** Runs directly on native DX12 drivers at full GPU boost clocks.
  * **Mode 2 (`vkd3d-proton` Vulkan 1.3):** Routes DX12 calls to a bundled `vkd3d\` folder for bindless/Vulkan translation.

## How to Use
1. Download the latest compiled build from the **Actions** tab (or Releases).
2. *(Optional for Mode 2)* Place x64 `d3d12.dll` and `d3d12core.dll` from `vkd3d-proton` inside the `vkd3d\` subfolder next to `Launcher.exe`.
3. Double-click **`Launcher.exe`**, pick your game's `.exe` in File Explorer, and select **Mode 1** or **Mode 2**.
4. Check `spoofer.log` in the launcher folder to verify hooked calls.

## Tested & Verified Games (GTX 750 Ti 4GB)
* **RoboCop: Rogue City (Unreal Engine 5):** Boots and runs natively in Mode 1 (18–33 FPS at 720p Low + SSR + AMD FSR 3 Performance).
* **Uncharted: Legacy of Thieves Collection:** Boots and runs natively in Mode 1.
