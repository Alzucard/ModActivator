# Changelog

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
