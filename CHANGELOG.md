# Changelog

## Unreleased
- Ship a modder kit beside the mod (`ModActivator-<version>-ModderKit.zip`): a guide to how ModActivator reads mods' config files, the `ModActivatorBindingsV1` header, a small example mod that builds, and the known-mods list for reference. The build makes both zips.
- Give every shortcut that you (with **Show as**), the mod or the known-mods list marks as a menu or a toggle a card of its own, so a mod is no longer limited to one of each. Cards of a mod with several are titled with their action.
- Add **Send** to every row in the Hotkeys tab, to press any shortcut right away. Menus close the panel; anything else leaves it open.
- Count keys reserved outside the mods as conflicts: Steam's default screenshot (F12) and overlay (Shift+Tab) keys and Windows' Alt+Tab, Alt+F4, Ctrl+Shift+Esc and Print Screen, from a new `[@reserved]` section of the known-mods list, plus your own under `[ReservedKeys]` in `ModActivator.User.ini`. F4 alone is not taken for Alt+F4.
- Add **Pick a free key** to the shortcut editor: the first function key, alone or with Ctrl, Shift, Alt or Ctrl+Shift, that nothing uses and the mod's setting can hold.
- Keep a mod's buttons working after an edit when the known-mods list says it rereads its config (`@reload = live`, Crimson Collection), and press its reload key after saving when it has one (`@reload = ReloadKey`: Private Storage Anywhere, Waypoint Route, DesertLink Speedster).
- Read the controller buttons mods keep with their shortcuts (`F8|LeftStick,RightStick,A`, `PadToggle=BACK+LS`, `StanceCycleButtons=L1+Down`, XInput bits such as `0040`), show them in a Controller column, and check them for conflicts with each other and with ModActivator's own.
- Open and close the panel with a controller: LB + RB + Back by default, kept from the game while held, set in Settings or with `PadOpenShortcut`. Navigate the panel with the d-pad or left stick, A and B, and change tab with LB or RB.
- Keep every ModActivator file except the plugin in a `ModActivator` folder inside `bin64`: the settings, your menu choices, the known-mods list, the log and the documentation. `ModActivator.asi` stays in `bin64`, where the ASI loader looks. The first start moves an older version's files into the folder, carries your `[General]` options into the new `ModActivator.ini`, and renames the old one `ModActivator.ini.moved`.
- Save everything set in the menu (panel shortcut, binding types, tabs, card order, names and notes) in `ModActivator.User.ini` instead of `ModActivator.ini`. No release ships that file, so an update, which replaces `ModActivator.ini`, no longer resets them. The first start copies an older `ModActivator.ini`'s choices over once.
- Name and describe any shortcut yourself in the Hotkeys tab. Your words win over every other source.
- Ship `ModActivator.KnownMods.ini`, which says what the shortcuts of about 60 popular mods do, from their Nexus Mods pages and config files. It also makes settings such as Dynamic Scalify's `OpenMenu` discoverable.
- Read and write Private Storage Anywhere's key codes in hexadecimal, as the mod does. `PrivateHotkey=73` is F4, not I.
- Send the modifiers a mod keeps in a setting of its own: Gilded Compass's menu is Ctrl+F7 when `HotkeyCtrl=1` and its minimap key always takes Ctrl, and DesertLink's second keys take the modifier named in `OpenKey2WithVk`. Gilded Compass's `HotkeyCtrl` flag is no longer listed as a shortcut for the 1 key.
- Add EEA HUD Resizer, Gilded Compass and SwiftQuestFollow to the known mods, and Drop Control's keys to its summary.
- Check every pointer a mod's `ModActivatorBindingsV1` export hands over before reading it, so a broken export is skipped instead of crashing the game. The `__try` guards this relied on do nothing in the MinGW build.
- In `bin64` itself, use only readmes named after the mod, not any `readme*` that could belong to another mod or tool.
- Name settings called just `Hotkey` or `HotkeyVk` "Hotkey" instead of "Hot", and `MapHotkeyVk` "Map".
- Read and write hex key codes from A0 to FE, such as `BA` for the `;` key, for mods that write codes in hex.
- Show the selected binding's new name and description in the Hotkeys editor right after saving a note.
- Use a readme next to a mod's config file as a description when nothing better says what a shortcut does.
- Let mods describe their own shortcuts through an optional `ModActivatorBindingsV1` export (`include/ModActivatorBindings.h`).
- Show where each description came from, and the mod's summary on hover.

## 1.2.4
- Fall back to the legacy DirectX path when ReShade is older than 6.8.0 or lacks the add-on event export, instead of never starting (#1).
- Read the swap chain's colour space from ReShade, so the menu is composited correctly in HDR on the ReShade path (#2).
- Draw the menu after ReShade's effects and overlay rather than under them (#3).
- Detect ReShade by its add-on export rather than by any loaded `dxgi.dll`, hide a leftover `ReShade.ini` when ReShade is not loaded, and title its card "ReShade" (#4).
- Start a fresh `ModActivator.log` each launch, keeping the previous one as `ModActivator.log.old`, add timestamps, and only write per-open cursor diagnostics with `DebugInput=1` (#5).
- Add `UseReShadeEvents` and `DebugInput` to the shipped `ModActivator.ini`.
- Name the previous release 1.2.3.1, matching the earlier hotfixes, so later versions sort above it (#6).
- Add the HLSL sources of the precompiled ImGui and HDR composite shaders under `shaders/` (#7).
- Skip and log folders and configs whose names fall outside the system code page instead of aborting discovery, and use the 8.3 short name when the install folder itself has such a name.
- Read more key spellings, such as `Control`, `LCtrl`, `Del`, `VK_F5`, `Num5` and the numpad operators, and log a binding that is skipped because its value cannot be read.
- Accept a binding that types a modifier that its section also requires with `RequireCtrl`, `RequireShift` or `RequireAlt`.
- Build releases with assertions off, so a failed Dear ImGui check no longer closes the game.
- Log MinHook and hook installation failures, and write the swapchain creation report when no frame arrives within a minute.
- Recognise this plugin's own module by its real file name in hook diagnostics.
- Rebuild the overlay's render targets on the next drawn frame after a resize, with every step checked, instead of inside the game's resize call; a failed rebuild is retried rather than drawn through.
- Draw through the legacy present hook only on the game's own swapchain, not on other windows' swapchains.
- Install the window procedure safely against messages arriving during the swap, and call the original through the matching Unicode API.
- Replace a temporary file left by an interrupted hotkey save instead of refusing every later save.
- Show non-Latin text: INI comments are read as UTF-16, UTF-8 or the system code page, names are converted for display, and missing characters are added to the font from a Windows font that has them.
- Follow resolution changes with the UI scale instead of keeping the first one.
- Copy the mod list and work out each card's state once per change rather than every frame.
- Ship the plugin without its test entry points and without any exported symbols. The tests now load a separate `build/ModActivator-tests.asi`.
- Read `HookXInput` from `ModActivator.ini`, as the log already told users to, and list it in the shipped INI.
- Stop loading `xinput9_1_0.dll` into the game; nothing used it.
- Remove code left over from the upstream overlay: the throwaway-device vtable reader, unused teardown and pad-chord functions, most of the detour helper, unused state and log declarations, and comments describing its loot features.
- Start the discovery and editor tests from empty fixture folders.

## 1.2.3.1
- Queue Windows input for the render thread to prevent a crash when typing or pressing hotkeys in the overlay.
- Preserve event order for keyboard, text, mouse buttons, and wheel input.

## 1.2.3
- Group bindings by mod in Hotkeys and Mod placement, with collapsible headings.
- Show INI descriptions in a What it does column and retain complete comment blocks in tooltips.

## 1.2.2.3
- Keep binding labels tied to their INI action names when changing Show as.
- Show the original INI field and nearby comment in binding tooltips.

## 1.2.2.2
- Use ReShade's presentation callbacks on ReShade installations, avoiding a second set of DXGI hooks.

## 1.2.2.1
- Capture the keys pressed in the shortcut field, including Ctrl, Shift, and Alt.
- Move Show as into the Mod placement table.

## 1.2.2
- Drag mod cards to reorder them within their current tab.
- Move mod cards to either built-in mod tab or to any custom tab from Settings.
- Manage binding types in Settings and edit hotkeys through a single text field.

## 1.2.1
- Wait until the first menu open to allocate DirectX overlay resources and acquire swapchain buffers.
- Read the optional EnableDred setting for GPU crash diagnostics.

## 1.2.0
- Queue mod actions in click order and keep dispatched shortcuts from opening ModActivator or entering its recorder.
- Scan configs once per refresh and avoid false conflicts from duplicate entries.
- Preserve inline comments when editing and avoid marking unchanged bindings for restart.
- Reset overlay input on close and skip frames whose DirectX allocator is still in use.
- Map symbolic letters, digits, and punctuation across keyboard layouts.
- Recognize German letter aliases in either direction, including German Ö and US semicolon.
- Preserve the physical key position when sending a shortcut or saving a recorded named binding.

## 1.1.4
- Record Ctrl, Shift, and Alt combinations in the Hotkeys editor.
- Display and send punctuation bindings using their physical key position on the active keyboard layout, including Pywel Casino on German keyboards.
- Add, rename, and delete custom tabs. Move mods between tabs and reorder their entries in Settings.
- Change the ModActivator shortcut in Settings without restarting.

## 1.1.3
- Recognize menu shortcuts described in INI comments, including CDWiki.
- Set a detected binding to Menu, Toggle, or Hotkeys only from the Hotkeys tab.

## 1.1.2
- Scroll mod lists with the mouse wheel while hovering over an entry.
- Use shorter mod cards and single-line Hotkeys rows.

## 1.1.1
- Redesigned the interface with a charcoal-and-gold theme, larger system font, searchable cards, readable shortcut labels and a conflict table.
- Added search by formatted mod name and readable shortcut.

## 1.1
- Reject malformed shortcuts with multiple main keys, repeated modifiers, or invalid ReShade arrays.
- Accept whitespace in named combinations; reject ambiguous numeric edits to named settings.
- Validate the same shortcut snapshot that is sent, and log partial input failures correctly.
- Use atomic shared flags between the render, input and worker threads.
- Disable activation of edited mods until restart, avoiding new-config/old-runtime mismatches.
- Adapt the scrollable Hotkeys list to available window height.
- Skip symbolic links during config discovery.
- Expand regression tests for malformed input and post-edit activation.

## 0.7
- Changed the menu shortcut to Ctrl+Shift+Backspace.
- Replaced periodic scans with on-demand discovery and a Refresh mods button.

## 0.6
- Toggle controls keep the window open.
- Cached hotkey values and conflict results to remove per-frame config reads.

## 0.5
- Added the Hotkeys editor with conflict indicators and backups.

## 0.4
- Added the Toggles tab; unsupported live states remain Unreported.

## 0.3
- Removed fixed mod entries and fallback shortcuts; select one discovered UI binding per mod.
