# Changelog

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
