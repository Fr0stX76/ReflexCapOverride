# ReflexCapOverride

A `version.dll` mod that turns NVIDIA Reflex Low Latency off and puts you in control of the Reflex frame limit in Direct3D 11 and 12 games.

Not affiliated with NVIDIA.

> [!WARNING]
> **Don't use ReflexCapOverride in online games with anti-cheat.** Anti-cheat software can flag DLL mods that hook game functions, and your account could be banned.

## Why

With G-SYNC, VSync and DLSS Frame Generation on, games force Reflex on, following NVIDIA's guidelines. Reflex then caps the frame rate a few FPS below the refresh rate. With some engines and displays, a cap below the refresh rate causes stutter, even with G-SYNC. ReflexCapOverride removes that cap, or replaces it with your own value, for those cases.

It's meant for single-player games, often played with a controller. Turning Reflex Low Latency off adds input latency. For competitive or mouse-driven games, keep Reflex as is.

## Features

- Forces Reflex Low Latency off, whatever the game asks for.
- Replaces the game's Reflex frame limit with your own value, or removes it so your usual frame limiter takes over.
- Works as `version.dll` or as an ASI plugin, alongside ReShade and other mods.

## Requirements

- Windows 10 or Windows 11, 64-bit.
- An NVIDIA GPU and a 64-bit Direct3D 11 or 12 game that uses NVIDIA Reflex. Vulkan is not supported.

## Usage

Download the zip from the Releases page, extract it and copy `version.dll` and `ReflexCapOverride.ini` next to the game's EXE. If the game keeps its main EXE in a subfolder, copy them next to that one.

### Configuration

```ini
[ReflexCapOverride]
TargetFPS=0
```

| `TargetFPS` | Result |
| --- | --- |
| `0` | No Reflex frame limit. Limiters that don't use Reflex (game engine, RTSS, VSync) keep working. |
| `1` to `1000` | Reflex frame limit at this rate. Decimals take a dot or a comma, for example `119.877`. |

- Reflex Low Latency is always off.
- Limiters built on Reflex, such as some in-game caps or RTSS in Reflex mode, are replaced by this value.
- The file is read at launch. Restart the game after changing it.

To check that it works, set `TargetFPS=60`: the game should stay at 60 FPS. The game's menu may still show Reflex as on. That's expected.

### ReShade and other mods

Only one file in the game folder can be named `version.dll`.

- **ReShade** usually installs as `dxgi.dll`, `d3d11.dll` or `d3d12.dll`. It works alongside this mod as is.
- **Another mod already uses `version.dll`**, often an ASI loader: rename this DLL to `ReflexCapOverride.asi` and put it where the loader picks up plugins, such as the game folder or its `plugins` or `scripts` folder.
- **An ASI loader is installed under another name**, such as `winmm.dll` or `dinput8.dll`: the same `.asi` works there too.

Wherever the DLL is, `ReflexCapOverride.ini` stays next to the game's EXE. Install only one copy per game.

### Uninstall

Delete `version.dll` (or `ReflexCapOverride.asi`) and `ReflexCapOverride.ini`.

## Build

MSVC x64 and a Windows 11 SDK, from an x64 Native Tools Command Prompt:

```
ml64 /c VersionExports.asm
rc ReflexCapOverride.rc
cl /std:c17 /O2 /MT /LD /DUNICODE /I minhook\include /Feversion.dll ReflexCapOverride.c minhook\src\buffer.c minhook\src\hook.c minhook\src\trampoline.c minhook\src\hde\hde64.c VersionExports.obj ReflexCapOverride.res /link /DEF:VersionExports.def /MANIFEST:NO
```

MinHook is included in `minhook\` ([TsudaKageyu/minhook](https://github.com/TsudaKageyu/minhook), BSD 2-Clause license).

## How it works

The DLL forwards every `version.dll` function to the real one in System32. Once the game loads NVAPI, it hooks two functions with MinHook:

| Function | ID | Change |
| --- | --- | --- |
| `NvAPI_D3D_SetSleepMode` | `0xAC1CA9E0` | Whatever the game asks for, the driver receives Low Latency, Boost, marker optimizations and minimum queue time off, and the frame interval from the INI. |
| `NvAPI_D3D_Sleep` | `0x852CD1D2` | Sends the same settings once per device, in case the game set up Reflex before the hooks were in place. |

Everything else passes through untouched. The DLL writes no files, doesn't touch the registry and doesn't use the network.

## AI disclosure

This app was built with the help of AI (Claude, by Anthropic) under the direction of the author.

## License

MIT, see [LICENSE](LICENSE). MinHook is distributed under its BSD 2-Clause license and the NVAPI definitions under NVIDIA's MIT license, see [NOTICE.txt](NOTICE.txt).

NVIDIA, NVIDIA Reflex and DLSS are trademarks or registered trademarks of NVIDIA Corporation.
