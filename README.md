# ModActivator 1.2.3.1

An in-game mod menu and hotkey manager for Crimson Desert.

**Default shortcut: Ctrl+Shift+Backspace.** Change it under **Settings**.

## Features

The panel uses a charcoal-and-gold theme with larger text, searchable mod cards, readable shortcut labels, and a scrollable hotkey table with conflict badges.

- **Mod UIs:** one automatically detected menu shortcut per loaded mod.
- **Toggles:** controls for toggle-only mods. The panel stays open when sending a toggle. State is labeled **Unreported** where a mod has no live-state interface.
- **Hotkeys:** expand a mod to see its shortcuts and INI descriptions. Capture new key combinations in a single field, with config backups. Exact conflicts are red; shared base keys with different modifiers are amber.
- **Settings:** change the panel shortcut, manage tabs, choose each mod’s tab, and correct binding types.
- **On-demand discovery:** scans when the panel opens, when you click **Refresh mods**, and after saving an edit. No periodic five-second scan.
- Config reads and conflict calculations are cached outside normal menu rendering.

## Install

Close Crimson Desert. Copy `ModActivator.asi` and `ModActivator.ini` into the game's `bin64` folder, beside `CrimsonDesert.exe`. An ASI loader is required.

Launch the game and press **Ctrl+Shift+Backspace**. Keep your existing INI when updating.

With ReShade 6.8.0 or newer, ModActivator draws through ReShade's add-on callbacks, after ReShade's effects and overlay. With an older ReShade it falls back to the legacy DirectX path automatically. Set `UseReShadeEvents=0` under `[General]` to always use the legacy path. On the legacy path, try `WrapSwapChain=1` if the overlay is invisible with frame generation.

`ModActivator.log` is started fresh each launch, and the previous one is kept as `ModActivator.log.old`. Set `DebugInput=1` to add cursor and raw input diagnostics each time the menu opens.

## Binding types

ModActivator uses shortcut names and nearby INI comments to identify menu openers and feature toggles. To correct a guess, open **Settings → Mod placement** and set **Show as** to **Menu**, **Toggle**, or **Hotkeys only**. **Automatic** clears the override. These choices are saved in ModActivator.ini and apply immediately. The action label comes from the original INI key. Hotkeys and Mod placement show an INI description preview when available. Hover over a binding to see its exact key and the full comment. The binding must first be detectable by the existing keyboard shortcut scan.

## Hotkey edits

Select a binding in **Hotkeys**, click the **New shortcut** field, then hold any modifiers and press the main key. Press Esc to cancel capture. Supported formats vary by mod. Numeric settings keep their existing separate modifier flags. Review conflicts, then save. The original config is backed up beside it as `<config>.modactivator.bak` before its first edit. ModActivator disables activation buttons for edited mods until the game restarts, since it cannot verify live reload support. Until restart, configured conflicts may differ from the running mods' active bindings.

Symbolic shortcuts map by physical key position and display the key label for the active Windows keyboard layout. US key names are the default config spelling. For example, US Y and German Z share a position, as do US semicolon and German Ö. Standard German letter aliases and Spanish Ñ can also be read when the game uses an English layout. Other localized symbols are resolved through the active or installed layouts when Windows can identify their key. Explicit Windows virtual-key codes and ReShade bindings retain their original values. ReShade bindings are read-only here. Edit them through ReShade.

## Custom tabs and panel shortcut

In **Settings**, choose **New tab**, enter a name, and click **Add tab**. Use the **Tab** column in **Mod placement** to move a mod to Mod menus, Toggles, or any custom tab. The **Show as** column controls how the binding is classified. Its action stays the same. Drag the handle on a mod card to reorder it within its tab. Deleting a tab returns its mods to their default tabs. Changes are saved in ModActivator.ini.

Use **Record opener** and **Save opener** to change ModActivator’s shortcut immediately. **Use default** restores Ctrl+Shift+Backspace when saved.

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

## License

Original ModActivator contributions are licensed under [MIT-0](LICENSE). You may use, modify, redistribute and sell those contributions without crediting ModActivator or its contributors.

Borrowed code and libraries retain their original licenses. Their copyright, license and disclaimer notices must still accompany distributions that include them; MIT-0 does not waive third-party requirements. See `THIRD_PARTY_NOTICES.md`.
