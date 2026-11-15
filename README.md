# ModActivator 1.3.5

An in-game mod menu and hotkey manager for Crimson Desert.

**Default shortcut: Ctrl+Shift+Backspace**, or on a controller hold **LB + RB** and press **Back**. Change either under **Settings**.

## Features

The panel uses a charcoal-and-gold theme with larger text, searchable mod cards, readable shortcut labels, and a scrollable hotkey table with conflict badges.

- **Mod UIs:** each loaded mod's detected menu shortcut, plus a card for every shortcut you, the mod or the known-mods list marks as a menu.
- **Toggles:** a card for every toggle, in the same way. The panel stays open when sending a toggle. State is labeled **State unavailable** where a mod has no live-state interface.
- **Hotkeys:** the button beside **Settings**. Expand a mod to see its shortcuts, their controller buttons, and what each one does, with your own names and notes. **Send** presses any shortcut right away. Capture new key combinations in a single field, or let **Pick a free key** find one, with config backups. Exact conflicts are red; shared base keys with different modifiers are amber, and keys Steam or Windows already use count too. **Placement** chooses each mod's tab and corrects its binding type.
- **Radial menu:** pin up to eight mod menus and toggles, then hold a key, aim and let go to open one, without opening the panel. See below.
- **Controller:** open the panel with a button combination, move with the d-pad or left stick, press with A, back out with B, and change tab with LB or RB on their own.
- **Settings:** the button beside **Refresh**. The tabs stay above Hotkeys and Settings; click one to go back. The panel and radial menu shortcuts, what the game does while the ring is open, and the UI size.
- **On-demand discovery:** scans when the panel opens, when you click **Refresh**, and after saving an edit. No periodic five-second scan.
- Config reads and conflict calculations are cached outside normal menu rendering.

## Install

Close Crimson Desert. Extract `ModActivator-<version>.zip` into the game's folder (the one that holds `bin64`), or install it with a mod manager such as DMM. That puts `ModActivator.asi` and `ModActivator.ini` in `bin64`, beside `CrimsonDesert.exe`. An ASI loader is required.

```
bin64\
  ModActivator.asi                 the plugin; the ASI loader only looks here
  ModActivator.ini                 options
  ModActivator.User.ini            your menu choices, created when you first save one
  ModActivator\
    ModActivator.log               this session's log (and .log.old, the last one)
    ModActivator.ModCache.bin      what each mod's file said, safe to delete
    README.md, LICENSE, ...        documentation and licences (manual installs)
```

The INI files sit in `bin64` itself because DMM installs a release's files there but not its subfolders. ModActivator creates the `ModActivator` folder for its log if it is missing.

Updating from 1.3.0 or 1.3.1, which kept the INI files in the `ModActivator` folder: install as above. The first start moves your `ModActivator.User.ini` back to `bin64`, carries the `[General]` options you set into the new `ModActivator.ini`, renames the old one `ModActivator.ini.moved`, and removes the old known-mods list. The log says what was moved.

Launch the game and press **Ctrl+Shift+Backspace**.

Everything you set in the menu (the panel shortcut, binding types, tabs, card order, and your names and notes) is saved in `ModActivator.User.ini`. No release includes that file, so updating, by hand or with a mod manager, never replaces it, and the new `ModActivator.ini` can simply overwrite the old one. Versions before this kept those choices in `ModActivator.ini`. The first start of this version copies them over once and says so in the log.

With ReShade 6.8.0 or newer, ModActivator draws through ReShade's add-on callbacks, after ReShade's effects and overlay. With an older ReShade it falls back to the legacy DirectX path automatically. Set `UseReShadeEvents=0` under `[General]` to always use the legacy path. On the legacy path, `WrapSwapChain=auto` (the default) draws the menu before DLSS frame generation whenever the game loads it, `1` always does that and `0` never does. Alongside Crimson Route, which breaks when the swapchain is wrapped, it never is. Set `HookXInput=0` if the controller should keep reaching the game while the menu is open.

`ModActivator\ModActivator.log` (in the folder) is started fresh each launch, and the previous one is kept as `ModActivator.log.old`. Set `DebugInput=1` to add cursor and raw input diagnostics each time the menu opens.

## Binding types

ModActivator uses shortcut names and nearby INI comments to identify menu openers and feature toggles. To correct a guess, open **Hotkeys → Placement** and set **Show as** to **Menu**, **Toggle**, or **Hotkeys only**. **Automatic** clears the override. These choices are saved in ModActivator.User.ini and apply immediately. Hotkeys shows a description preview when one is available (see below). Hover over a binding, here or in Placement, to see its exact key and the full description.

## What a shortcut does

Each binding's name and description come from the most trustworthy source that has one. Hover over a binding to see which:

1. **Your own name and note.** Select a binding in **Hotkeys**, fill in **Name** and **Note**, and click **Save name and note**. They take effect at once and are saved in `ModActivator.User.ini`, so updating ModActivator or the mod never replaces them. **Clear mine** goes back to the other sources.
2. **The mod itself**, for mods that describe their shortcuts to ModActivator (see below).
3. **The known-mods list**, built into `ModActivator.asi` (its source is `data/ModActivator.KnownMods.ini`): what the shortcuts of about 50 popular Crimson Desert mods do, written from their Nexus Mods pages and config files. It also tells ModActivator about settings whose names do not look like shortcuts (and ones that only look like shortcuts), about mods that write key codes in hexadecimal, and about modifiers a mod keeps in a setting of its own, such as Gilded Compass's `HotkeyCtrl=1`, so the full combination is sent. It is built into the plugin because DMM does not install it as a file of its own, and it is replaced with every update; put your own changes in your notes instead. A `ModActivator.KnownMods.ini` beside the plugin is still read, but only adds config files the built-in list does not know, and reserved keys.
4. **The comment** beside the setting in the mod's config file, or **the mod's own file** (`.asi` or `.addon64`), when it carries the default config it writes or a settings schema with a label and help text for the setting. ModActivator only reads the file, it never runs anything from it, and this works for mods it has never heard of. The file's version information also names the mod when nothing else does. What a mod's file said is kept in `ModActivator\ModActivator.ModCache.bin`, so each file is read once and again only when the mod is new or updated (its size or date changed) or ModActivator itself is updated.
5. **A readme** next to the mod's config file: a `.txt` or `.md` file named `readme…` or after the mod.
6. **A sentence in the mod's own file** that names the setting, such as a log message. Error messages are skipped.

Shown as **From the mod's file**. When a mod has a single shortcut and nothing else says what it is, the text near the code that reads the keyboard or controller decides: "enabled"/"disabled" makes it a toggle, "opened"/"closed" or "open the window" a menu.

Without any of these, the name is worked out from the setting's key, such as `StanceCycleKey` → "Stance Cycle". A source that says whether a shortcut opens a menu, toggles something or is a plain action also decides its tab, unless you chose **Show as** yourself.

**For mod authors:** `ModActivator-<version>-ModderKit.zip` (and `docs/MODDERS.md` here) explains how ModActivator finds, names and sorts your shortcuts, how to export `ModActivatorBindingsV1` so it shows your own names and descriptions (`include/ModActivatorBindings.h`), and how to get into the known-mods list, with a small example mod that does all of it. Your mod works the same without ModActivator.

## Hotkey edits

Select a binding in **Hotkeys**, click the **New shortcut** field, then hold any modifiers and press the main key, or click **Pick a free key** for a function key (with Ctrl, Shift or Alt if need be) that nothing else uses and the mod's setting can hold. Press Esc to cancel capture. Supported formats vary by mod. Numeric settings keep their existing separate modifier flags. Review conflicts, then save. The original config is backed up beside it as `<config>.modactivator.bak` before its first edit. ModActivator disables an edited mod's buttons until the game restarts, unless the known-mods list says the mod rereads its config: then the new shortcut works at once, and for a mod with a reload key (Private Storage Anywhere, Waypoint Route, DesertLink Speedster) ModActivator presses that key for it right after saving. Until restart, configured conflicts may differ from the running mods' active bindings.

Symbolic shortcuts map by physical key position and display the key label for the active Windows keyboard layout. US key names are the default config spelling. For example, US Y and German Z share a position, as do US semicolon and German Ö. Standard German letter aliases and Spanish Ñ can also be read when the game uses an English layout. Other localized symbols are resolved through the active or installed layouts when Windows can identify their key. Explicit Windows virtual-key codes and ReShade bindings retain their original values. ReShade bindings are read-only here. Edit them through ReShade.

## Custom tabs and panel shortcut

Click **+** after the tabs, enter a name and press Enter. Right-click one of your tabs to **Rename** or **Delete** it. Drag any tab, built-in ones too, left or right to put the tabs in your order. Use the **Tab** column in **Hotkeys → Placement** to move a mod to Mod menus, Toggles, or any custom tab. The **Show as** column controls how the binding is classified. Its action stays the same. Drag the handle on a mod card to reorder it within its tab. Deleting a tab returns its mods to their default tabs. Changes are saved in ModActivator.User.ini.

In **Settings → Shortcuts**, click **Record** beside **Keyboard**, press the new keys, check what they overlap and click **Save**: the shortcut changes at once. **Default** proposes Ctrl+Shift+Backspace again.

**Settings → Interface → UI size** makes the whole panel smaller or larger (80 to 150 %, `UISize`), on top of the size your resolution gives it.

## Discovery and compatibility

Discovery matches loaded ASI/addon64 modules to INI filenames or matching parent folders, up to two subfolder levels deep. ReShade's INI is listed only while ReShade is loaded, whatever its DLL is called; it is recognised by its add-on export. Missing, disabled, hardcoded, non-INI, or unrecognized bindings may be omitted. Shortcuts are only ever read from the mods' own config files: the known-mods list describes them, it never invents one.

Mods whose menus are controlled by ModHub are marked accordingly and must be opened through ModHub. The plugin sends shortcuts; it does not load or unload other ASIs. A dispatched shortcut is not proof that the destination mod acted on it. Toggle states cannot be universally inferred.

Conflict detection covers recognized keyboard settings, including secondary gameplay keys, the controller combinations mods keep beside them, ModActivator's own shortcuts, and keys reserved outside the mods: Steam's default screenshot and overlay keys and Windows' Alt+Tab, Alt+F4, Ctrl+Shift+Esc and Print Screen. The game's own controls are not known to ModActivator; add the ones you care about under `[ReservedKeys]` in `ModActivator.User.ini`, one per line, such as `I = Game: inventory`. Unknown bindings are not covered.

Controller buttons are read wherever mods keep them: after the key in the same value (`F8|LeftStick,RightStick,A`), or in a setting beside it (`PadToggle=BACK+LS` for `ToggleKey`, `StanceCycleButtons=L1+Down` for `StanceCycleKey`), as XInput or PlayStation names or as XInput's button bits. They are shown and checked, not edited; change them in the mod's own config.

## Radial menu

Every card in **Mod menus** and **Toggles** has a ring button. Click it to pin that card to the radial menu, up to eight; click again to take it off. **Settings → Radial menu** lists them in ring order, clockwise from the top, with **Up**, **Down** and **Remove**.

Hold the key left of 1 (`` ` ``, `^` on German keyboards) and the ring appears in the middle of the screen. Move the mouse towards an entry and let go of the key to open it. Letting go with the mouse in the middle, or pressing **Escape**, closes the ring without doing anything. No cursor is shown: only the direction counts. On a controller, hold **LB + RB**, press **D-pad Up**, aim with the right stick and let go; the choice stays when the stick springs back.

While the ring is open the camera stays still and the controller's buttons are kept from the game. **Settings → Radial menu** changes the key (`RadialKey`, `None` for off) and the combination (`RadialPad`), and what the game does meanwhile (`RadialPause`): it keeps running (`Off`), runs at a quarter speed (`Slow`), or stands still (`Freeze`). Slowing and freezing use the game's own fixed-timestep switch, found by its bytes the way Trinity finds it, and give it back as it was when the ring closes. If a game update moved that code, the log and Settings say so, and the ring works without it. If the game or another mod already uses that switch, ModActivator leaves it alone.

The game sees the radial key too, as it sees every keyboard key: pick one it does not use. Like any other shortcut, it is checked for conflicts with your mods' keys.

## Controller

Hold **LB + RB**, then press **Back**, to open or close the panel. The buttons of that combination are kept from the game while you hold them, so the final press does not also act in the game. Change it or switch it off under **Settings → Shortcuts → Controller** (two buttons or more), or with `PadOpenShortcut` in `ModActivator.ini` (`None` for off). With `HookXInput=0` the shortcut still works, but the game sees those buttons too.

In the panel, the d-pad or left stick moves between items, A presses, B backs out of a field or a list, and LB or RB pressed on its own goes to the previous or next tab. Mod menus and toggles are still opened with their keyboard shortcuts, which ModActivator sends for you.

## Build (Windows x64)

Requires Python 3.10+ and [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw). The tested toolchain is `llvm-mingw-20261104-ucrt-x86_64`. Dependencies are vendored at the revisions in `DEPENDENCIES.md`; no download is required during compilation.

```powershell
python scripts/build.py --toolchain C:\Tools\llvm-mingw-20261104-ucrt-x86_64
python tests/test_discovery.py
python tests/test_descriptions.py
python tests/test_editor.py
python tests/test_features.py
python tests/test_responsiveness.py
python tests/test_text.py
```

Run from the repository root. The plugin is built to `dist/`. The tests load `build/ModActivator-tests.asi`, the same plugin with the test entry points compiled in; the shipped `dist/ModActivator.asi` contains none of them. Close the game before replacing an installed ASI; do not hot-unload it.

Automated checks cover parsing, discovery, config editing and preservation, backups, conflict classification, and headless menu-rendering behavior. They do not establish in-game compatibility or verify another mod's response to injected input.

## Uninstall

With the game closed, remove `ModActivator.asi`, `ModActivator.ini` and the `ModActivator` folder from `bin64` (and a `ModActivator.KnownMods.ini`, which versions 1.3.2 to 1.3.4 put there). Keep `ModActivator.User.ini` if you might reinstall and want your menu settings back. Restore edited configs from their backups if needed; uninstalling does not undo previous hotkey edits.

## Credits

- [Trinity by XeTrinityz](https://github.com/XeTrinityz/Trinity): DirectX 12 hooks, HDR composite shader code, and XInput hooks, adapted through Master Looter (MIT).
- [Master Looter by Seth](https://github.com/shin2344234/master-looter): adapted overlay foundation, window input handling, and modified Dear ImGui DirectX 12 backend (MIT).
- [Dear ImGui by Omar Cornut](https://github.com/ocornut/imgui): the in-game interface (MIT).
- [MinHook by Tsuda Kageyu](https://github.com/TsudaKageyu/minhook): function hooks and bundled HDE disassembler. See its included license texts.

ModActivator-specific discovery, activation, editor and conflict features were developed with AI assistance. See `THIRD_PARTY_NOTICES.md` and `licenses/` for the retained licenses.

## License

Original ModActivator contributions are licensed under [MIT-0](LICENSE). You may use, modify, redistribute and sell those contributions without crediting ModActivator or its contributors.

Borrowed code and libraries retain their original licenses. Their copyright, license and disclaimer notices must still accompany distributions that include them; MIT-0 does not waive third-party requirements. See `THIRD_PARTY_NOTICES.md`.
