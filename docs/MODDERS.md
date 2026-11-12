# ModActivator for mod authors

ModActivator is an in-game panel for Crimson Desert that lists every loaded mod's shortcuts, opens their menus, sends their toggles, edits their keys and warns about conflicts. It reads your mod's config file; your mod needs no changes to work with it. This guide is about making it understand your mod well, in three steps, each optional:

1. **Write your config file so ModActivator reads it on its own.** Most mods already do.
2. **Export one function** that names and describes your shortcuts.
3. **Get into the known-mods list** that ships with ModActivator.

The kit holds `include/ModActivatorBindings.h` (the export), `examples/ExampleMod` (a small mod doing 1 and 2) and this guide. All of it is MIT-0: copy what you like, no attribution needed.

## 1. A config file ModActivator reads on its own

### Where it looks

ModActivator scans `bin64` and the folders below it, two levels deep, for `.ini` files, and gives each to a loaded mod (`.asi` or `.addon64`):

- a file named after the mod: `MyMod.ini` for `MyMod.asi`, or
- any file in a folder named after the mod: `MyMod\settings.ini`.

Folders called `reshade-shaders`, `cache`, `icons`, `shapes` and `community` are skipped. A config for a mod that is not loaded is ignored.

### Which settings are shortcuts

A setting is read as a shortcut when its name is `Hotkey` or `Key`, contains `Hotkey`, or ends in `Key` or `KeyVK`: `MenuKey`, `ToggleHotkey`, `ActivationKeyVK`. Names containing `Controller`, `Pad`, `Require`, `Modifier` or `Options`, or starting with `_`, never are. A name like `OpenMenu` is not found; call it `OpenMenuKey`.

### How to write the value

Any of these:

| Write | Means |
|---|---|
| `F7`, `Ctrl+F7`, `Ctrl+Shift+Alt+F7` | A key with modifiers, joined with `+`. Up to three modifiers, then one main key. |
| `F1` to `F24`, `A` to `Z`, `Insert`, `Delete`, `Home`, `End`, `PageUp`, `PageDown`, `Up`, `Down`, `Left`, `Right`, `Space`, `Tab`, `Enter`, `Backspace`, `Pause`, `ScrollLock`, `CapsLock`, `NumLock`, `PrintScreen`, `Escape` | Named keys. Spellings such as `Del`, `PgUp`, `VK_F5`, `LCtrl` work too. |
| `Num0` to `Num9`, `NumPad0` to `NumPad9`, `Add`, `Subtract`, `Multiply`, `Divide`, `Decimal` | The number pad. |
| `;`, `'`, `[`, `]`, `\`, `,`, `.`, `/`, `-`, `=`, `Grave` | Symbol keys, by their position on a US keyboard. ModActivator shows the user's own layout's label. |
| `118`, `0x76` | A Windows virtual-key code, decimal or with `0x`. |
| `0`, `None` or nothing | Off. |

A single digit `1` to `9` is the digit key, except in a setting whose name ends in `VK` or `KeyCode`, where it is a key code. Bare hexadecimal without `0x` (`76`) is read as decimal; if your mod writes codes that way, ask for a known-mods entry (step 3), which tells ModActivator.

Modifiers ModActivator understands:

- in the value itself: `Ctrl+F7`, which is the clearest;
- `RequireCtrl=1`, `RequireShift=1`, `RequireAlt=1` in the same section, beside a plain key.

Any other way of storing a modifier (a flag such as `HotkeyCtrl=1`, a second setting naming the modifier) needs a known-mods entry, or ModActivator sends the key without it. Windows-key shortcuts (`RequireWin=1`) are skipped. Left and right modifiers are not told apart.

### What it does: comments

The comment directly above a setting, or after it on the same line, is shown as its description:

```ini
[Menu]
; Opens or closes the My Mod window.
MenuKey=Ctrl+F7
DebugKey=F9 ; Writes a report to MyMod.log.
```

Several comment lines in a row are kept together; a blank line ends them. UTF-8 (with or without a BOM), UTF-16 and the system code page all work.

A readme next to the config (`MyMod readme.txt`, `MyMod.md`) is used when there is no comment: the line naming the setting, such as `MenuKey = Ctrl+F7 - opens the window`. In `bin64` itself only a readme named after the mod counts; in the mod's own folder any `readme*` does.

### Menu, toggle or action

ModActivator puts each mod's menu shortcut on its **Mod menus** tab, toggles on **Toggles**, and everything on **Hotkeys**. It decides from:

- the name: `MenuKey`, `SettingsWindowHotkey`, `UIHotkey` or `WindowHotkey` open a menu, and so do `Hotkey`, `Key`, `ToggleKey` or `OpenKey` in a section called `Menu`, `UI`, `Interface`, `Window` or `SettingsWindow`, and `ActivationKeyVK` under `General` or `Settings`;
- `ToggleKey`, `ToggleHotkey`, `HotkeyToggleEffect` and `ToggleEffectKey` elsewhere are toggles;
- the comment: "Opens the menu", "Show the settings window", "Toggles the overlay" and the like make it a menu;
- names with `Stance`, `Mode`, `Cycle`, `Capture`, `Screenshot` and similar are gameplay actions.

When the guess is wrong, the export (step 2) or the known-mods list (step 3) settles it, and users can correct it in Settings.

### Controller buttons

ModActivator shows a shortcut's controller buttons beside it and checks them for conflicts. It finds them

- after the key, in the same value: `MenuKey=F8|LeftStick,RightStick,A`, or
- in a setting next to it, named after the shortcut's action word (the name without `Key` or `Hotkey`): for `StanceCycleKey`, any of `StanceCycleButtons`, `StanceCycleButton`, `StanceCycleControllerButton`, `StanceCyclePad`, `PadStanceCycle`, `ControllerStanceCycle`.

Buttons are joined with `+` or `,`, in any case: `A B X Y`, `LB RB LT RT`, `LS RS` (or `L3 R3`, `LeftStick`), `Back Start` (or `View Menu`), `DPadUp` (or `Up`, `dpad_up`), and the PlayStation names `Cross Circle Square Triangle L1 R1 L2 R2 Share Options`. XInput's button bits work as a number too: `0x0040` or `0040` for the left stick.

ModActivator shows controller buttons and warns about clashes; it does not edit them or press them.

### How ModActivator presses your keys

It sends them with `SendInput`, tagged with `dwExtraInfo = 0x4D414354`, and only while the game is in front. Symbol and letter keys are sent by their physical scan code, other keys by virtual-key code. Reading keys with `GetAsyncKeyState`, `GetKeyState` or `WM_KEYDOWN` sees them; raw input or DirectInput may not see the scan code of keys sent by virtual-key code. If your mod ignores injected input, ModActivator's buttons will do nothing for it.

ModActivator waits until Ctrl, Shift, Alt, the Windows keys, the left mouse button and the key itself are released, presses the modifiers, then the main key, and releases them about 130 ms later. For a menu it closes its own panel first; toggles and plain actions keep it open.

## 2. Export your shortcuts

One function, and ModActivator shows your names and descriptions and puts each shortcut on the right tab:

```cpp
#include "ModActivatorBindings.h"

static const ModActivatorBindingV1 bindings[] = {
    {"MyMod.ini", "Menu", "MenuKey", "Settings window",
     "Opens or closes the My Mod window.", ModActivatorKindMenu},
    {nullptr, nullptr, "ToggleKey", "My effect", "Turns the effect on or off.",
     ModActivatorKindToggle},
};
extern "C" __declspec(dllexport) const ModActivatorBindingV1 *
ModActivatorBindingsV1(unsigned *count) {
  *count = sizeof bindings / sizeof bindings[0];
  return bindings;
}
```

- `key` is the setting's name in your config file. `file` (file name only) and `section` narrow it down and may be null for any.
- `name` and `description` are UTF-8; keep the name short. `kind` is `ModActivatorKindMenu`, `Toggle`, `Action`, or `Unspecified` to let ModActivator decide.
- An exported setting is read as a shortcut whatever it is called.
- Keep the array and strings in static storage. ModActivator reads them on a background thread each time its menu refreshes, never from `DllMain`, and checks every pointer before reading it.
- Your user's own names and notes in ModActivator still win over yours.

`examples/ExampleMod` is a complete small mod doing this, with the config file from step 1.

## 3. The known-mods list

The known-mods list is built into `ModActivator.asi` and describes popular mods that do not export anything. Its source, `data/ModActivator.KnownMods.ini` in the repository (and `reference/ModActivator.KnownMods.ini` in this kit), explains the format in its header; one entry looks like:

```ini
[MyMod.ini]
@mod = My Mod | One line about the mod.
@reload = ReloadKey
Menu/MenuKey = menu | Settings window | Opens the My Mod window.
Effect/ToggleKey = toggle | My effect | Turns the effect on or off.
```

It can also say things your config file cannot:

- `@numbers = hex`: key codes written in hex without `0x`;
- `@reload = live` or `@reload = ReloadKey`: your mod rereads its config while the game runs, so an edited shortcut works without a restart (ModActivator presses the reload key for you);
- after the description, modifiers your mod keeps elsewhere: `| Ctrl?HotkeyCtrl` (Ctrl while that flag is 1), `| =OpenKey2WithVk` (the modifier that setting names);
- after those, the setting that holds the controller buttons, when its name does not give it away;
- `Key = ignore` for a setting that only looks like a shortcut.

To try an entry before sending it in, put it in a `ModActivator.KnownMods.ini` beside `ModActivator.asi` in `bin64`. That file is read on top of the built-in list, but only for config files the built-in list does not know (and for `[@reserved]` keys), so it cannot change an existing entry.

To be added, or to correct an entry, open an issue or a pull request at https://github.com/Alzucard/ModActivator with your mod's Nexus link and config file.
