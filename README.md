# ModActivator

An in-game mod menu and hotkey manager for Crimson Desert.

**Open the panel: Ctrl+Shift+Backspace.**

## Features

- **Mod UIs:** one automatically detected menu shortcut per loaded mod.
- **Toggles:** controls for toggle-only mods. The panel stays open when sending a toggle. State is labeled **Unreported** where a mod has no live-state interface.
- **Hotkeys:** edit recognized keyboard bindings with config backups. Exact conflicts are red; shared base keys with different modifiers are amber.
- **On-demand discovery:** scans when the panel opens, when you click **Refresh mods**, and after saving an edit. No periodic five-second scan.
- Config reads and conflict calculations are cached outside normal menu rendering.

## Install

Close Crimson Desert. Copy `ModActivator.asi` and `ModActivator.ini` into the game's `bin64` folder, beside `CrimsonDesert.exe`. An ASI loader is required.

Launch the game and press **Ctrl+Shift+Backspace**. Keep your existing INI when updating.

If the overlay is invisible with frame generation, close the game and try `WrapSwapChain=1` in `ModActivator.ini`.

## Hotkey edits

Select a binding in **Hotkeys**, enter a supported new shortcut, review conflicts, then save. The original config is backed up beside it as `<config>.modactivator.bak` before its first edit. Restart the game after editing unless the target mod supports live reload. Until restart, configured conflicts may differ from the running mods' active bindings.

ReShade bindings are read-only here; edit them through ReShade.

## Discovery and compatibility

Discovery matches loaded ASI/addon64 modules to INI filenames or matching parent folders, up to two subfolder levels deep. ReShade's INI is associated with loaded `dxgi.dll`. Missing, disabled, hardcoded, non-INI, or unrecognized bindings may be omitted. There is no fixed mod list or guessed shortcut fallback.

Mods whose menus are controlled by ModHub are marked accordingly and must be opened through ModHub. The plugin sends shortcuts; it does not load or unload other ASIs. A dispatched shortcut is not proof that the destination mod acted on it. Toggle states cannot be universally inferred.

Conflict detection covers recognized keyboard settings, including secondary gameplay keys. Game controls, Steam shortcuts, controller conflicts, and unknown bindings are not comprehensively covered. Existing overlay combinations may need testing.

## Build (Windows x64)

Requires Python 3.10+ and [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw). The tested toolchain is `llvm-mingw-20261104-ucrt-x86_64`. Dependencies are vendored at the revisions in `DEPENDENCIES.md`; no download is required during compilation.

```powershell
python scripts/build.py --toolchain C:\Tools\llvm-mingw-20261104-ucrt-x86_64
python tests/test_discovery.py
python tests/test_editor.py
python tests/test_responsiveness.py
```

Run from the repository root. Build output is in `dist/`. Close the game before replacing an installed ASI; do not hot-unload it.

Automated checks cover parsing, discovery, config editing and preservation, backups, conflict classification, and headless menu-rendering behavior. They do not establish in-game compatibility or verify another mod's response to injected input.

## Uninstall

With the game closed, remove `ModActivator.asi` and `ModActivator.ini`. Restore edited configs from their backups if needed; uninstalling does not undo previous hotkey edits.

## Credits

- [Trinity by XeTrinityz](https://github.com/XeTrinityz/Trinity): DirectX 12 hooks, HDR composite shader code, and XInput hooks, adapted through Master Looter (MIT).
- [Master Looter by Seth](https://github.com/shin2344234/master-looter): adapted overlay foundation, window input handling, and modified Dear ImGui DirectX 12 backend (MIT).
- [Dear ImGui by Omar Cornut](https://github.com/ocornut/imgui): the in-game interface (MIT).
- [MinHook by Tsuda Kageyu](https://github.com/TsudaKageyu/minhook): function hooks and bundled HDE disassembler. See its included license texts.

ModActivator-specific discovery, activation, editor and conflict features were developed with AI assistance. See `THIRD_PARTY_NOTICES.md` and `licenses/` for the retained licenses.
