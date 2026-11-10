# Changelog

## 0.8
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
