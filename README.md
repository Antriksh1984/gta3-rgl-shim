# GTA III RGL Shim

Play your **modded, downgraded GTA III 1.0** through the **Rockstar Games Launcher** and keep **cloud saves** working.

Most GTA III mods need the original 1.0 executable, but the Rockstar Games Launcher (RGL) ships its own build. If you start a downgraded copy directly, the launcher isn't involved and your saves never sync. This shim sits in the RGL game folder. When you click **Play**, it quietly starts your modded 1.0 game instead. The launcher sees a normal session, so it syncs your saves before you play and uploads them after you quit.

> Unofficial fan project, not affiliated with or endorsed by Rockstar Games or Take-Two. It contains no game files and never modifies `gta3.exe`.

## Download and install

1. Download `GTA3-Shim-v<version>.zip` from [Releases](../../releases) and extract it.
2. Quit the game, double-click **`Install.bat`**, and accept the administrator prompt.
3. Confirm the GTA III folder the installer found (or browse/type it), then pick your modded 1.0 `gta3.exe`.
4. Launch GTA III from the Rockstar Games Launcher.

The zip's `README.txt` covers everything for players: Shift bypass, uninstall, troubleshooting. You can also skip the prompts by passing the paths:

```bat
Install.bat "D:\Games\Grand Theft Auto III" "D:\Games\GTA3 Modded\gta3.exe"
```

### Requirements

- GTA III installed through the Rockstar Games Launcher, with cloud saves enabled.
- A modded or downgraded 1.0 copy of GTA III in a **separate** folder. Both versions read and write the same `Documents\GTA3 User Files` saves, and the formats are compatible.
- Windows 10/11.

## How it works

1. **Proxy DLL.** The RGL `gta3.exe` imports `dinput8.dll`, and Windows looks in the game folder first. The shim is a `dinput8.dll` that forwards every DirectInput export to the real system DLL, loaded from `SysWOW64` on first use.
2. **Entry-point patch.** In `DllMain`, the shim replaces the first 5 bytes of the exe's entry point with a `JMP` to its own code. The patch exists in memory only. The RGL entry point is a protection stub (section `.rkstr`), so the RGL game never decrypts or initializes: no window, no audio, no asset loading.
3. **Hand-off.** When the loader reaches the entry point, the shim:
   - logs the session and backs up the save folder;
   - clears the inherited `__COMPAT_LAYER`. The RGL exe runs under a `DWM8And16BitMitigation` compatibility layer, which the modded game would otherwise inherit.
   - starts the modded 1.0 `gta3.exe` in its own folder, inside a Job object, so the modded game dies if the launcher kills the official process;
   - waits for it to exit **while pumping window messages**. A plain blocking wait freezes the modded game's save menu, because the game ends up waiting on its parent to answer a message.
   - exits with the same exit code.
4. **What the launcher sees.** One normal `gta3.exe` session, so it syncs saves down before launch and up after exit.

**Fail-safe:** if anything goes wrong (missing or bad `shim.ini`, a wrong path, `CreateProcess` failing), the shim logs the reason, restores the original entry bytes, and jumps to them. You get the original RGL game instead of a crash. Hold **Shift** while clicking Play to force this on purpose.

More detail: [docs/TECHNICAL_NOTES.md](docs/TECHNICAL_NOTES.md).

## shim.ini

```ini
[Shim]
Enabled=1
ModdedExe=D:\Games\GTA3 Modded\gta3.exe
ExtraArgs=
; Empty = default Documents\GTA3 User Files
SaveFolder=
BackupSaves=1
KeepBackups=10
DebugChild=0
```

| Key | Meaning |
|---|---|
| `Enabled` | `0` = always start the original RGL game |
| `ModdedExe` | Full path to the modded 1.0 `gta3.exe` (`%ENV%` variables allowed) |
| `ExtraArgs` | Extra command-line arguments for the modded game |
| `SaveFolder` | Save folder to back up; empty = `Documents\GTA3 User Files` (resolves OneDrive-redirected Documents) |
| `BackupSaves` / `KeepBackups` | Copy saves to `%LOCALAPPDATA%\GTA3Shim\backups\<timestamp>\` before each launch and keep the newest N (`0` = all). These copies are never uploaded. |
| `DebugChild` | Diagnostics only. Runs the game under a built-in debugger that logs exceptions, debug output and quit calls with stack traces. It dumps all thread stacks when the game window stops responding, or on **Ctrl+Shift+F12**. |

`shim.ini` lives in the RGL game folder, which is read-only for normal users, so you need an editor running as administrator to change it.

## Logs

`%LOCALAPPDATA%\GTA3Shim\shim.log`. If the game folder is writable, the log goes next to the DLL instead. It rotates to `shim.log.old` past 1 MB. The modded 1.0 exe returns arbitrary exit codes on a normal quit (0, 13, 14286, …); that's expected, and the launcher treats them all as a normal exit.

## Building

Requirements: Visual Studio 2022 (Build Tools is enough) and CMake 3.20+. The DLL **must** be x86, because GTA III is a 32-bit process.

```powershell
cmake -S . -B build -A Win32
cmake --build build --config Release
```

The output is `build\Release\dinput8.dll`. The CRT is linked statically (`/MT`), so no redistributable is needed.

To build the release zip:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package.ps1
```

This writes `dist\GTA3-Shim-v<version>\`, `dist\GTA3-Shim-v<version>.zip` and a `.sha256.txt`. The version comes from `project(... VERSION x.y.z ...)` in `CMakeLists.txt`.

### Manual install from source

From an elevated PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\install.ps1 -GameDir "D:\Games\Grand Theft Auto III" -ModdedExe "D:\Games\GTA3 Modded\gta3.exe"
```

```powershell
powershell -ExecutionPolicy Bypass -File scripts\uninstall.ps1 -GameDir "D:\Games\Grand Theft Auto III"
```

The installer refuses to overwrite a `dinput8.dll` that isn't the shim, keeps an existing `shim.ini` when updating, and never touches `gta3.exe` or any save file.

## Repository layout

```
src/            the shim DLL (proxy, entry-point hook, hand-off, backups, logging, debug mode)
scripts/        install / uninstall / package scripts (PowerShell)
packaging/      Install.bat, Uninstall.bat and the end-user README.txt for the zip
tools/          inspect_pe.py, a read-only PE inspector used for the analysis
docs/           technical notes
TESTING.md      manual test checklist
```

## After a launcher update or "Verify files"

The launcher may remove `dinput8.dll` / `shim.ini`. If the original game starts without you holding Shift, run `Install.bat` again; your settings are kept.

## License

[MIT](LICENSE)
