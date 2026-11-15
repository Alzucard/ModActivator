# ModActivator UI guide

The rules every screen of the panel follows, so it stays consistent as it
grows. They come from Nielsen Norman's usability heuristics, Refactoring UI,
Microsoft's Windows settings pattern, the Game Accessibility Guidelines and
Dear ImGui's own conventions, cut down to what this panel needs.

## Size

The panel is a desktop tool on a PC monitor, sized like ReShade or OptiScaler,
not a TV menu.

- Text is 14 px at 1080 lines (`baseFontSize`). Everything scales with the
  resolution (1080 lines = 1, from 0.8 up to 1.7), times the UI size from
  Settings → Interface (80 to 150 %).
- Write every size in code as a 1080p value times `UI::scale` (`u`). Never
  write a bare pixel size.
- Spacing comes from the style: window padding 10, frame padding 8 × 4, item
  spacing 8 × 6. Use `ImGui::Spacing()` or the style values, not your own gaps.
- The panel opens at 640 × 520 and can be resized down to 460 × 340.
- One mod per line. A row is one frame high plus a little padding; details go
  in the tooltip.

## Layout

- **Header:** one line with the name, version, search, Settings, Refresh and
  close.
- **Tabs:** Mod menus, Toggles, Hotkeys, your own tabs, then a **+** to add
  one. Hotkeys switches between Bindings and Placement (which tab each card
  goes on). Drag a tab to move it; the order is saved. Right-click one of your tabs to rename or delete it,
  in a small popup, never a Settings page.
- **Settings:** the button beside Refresh shows it in place of the tabs, until
  pressed again. A list of pages on the left, one topic per page: Shortcuts,
  Radial menu, Interface; LB and RB step through them. A setting belongs to exactly one
  page; if you have to explain where to find it, it is on the wrong page.
- **Setting rows:** the label in a fixed-width column on the left, then the
  current value, then the buttons. Every shortcut row looks the same:
  `Label   [current value]   Record   Default   Off`.
- Group rows on a page under `ImGui::SeparatorText` headings, not under
  free-standing coloured text.
- Show details only when they apply, under the row they belong to: a recorded
  key's conflicts, Save and Discard.

## Words

- Buttons are verbs, short: Record, Default, Off, Save, Discard, Open, Toggle.
  The row already says what they act on.
- Labels name the thing, not the action: "Open the panel", not "Record
  opener".
- Hints are one line, muted, and only where the control is not obvious. A
  longer explanation goes in a tooltip.
- Results of an action ("Saved.", "Could not save ...") appear at the bottom of
  the page they came from, and are cleared when you change page.

## Colour

- **Gold** (`UI::gold`) means selected, chosen or pinned: the current tab,
  a pinned ring button, the chosen wedge. Never use it for plain headings.
- **Muted** (`UI::muted`, `TextDisabled`) is for hints, shortcuts on cards and
  anything secondary.
- **Red** is for a conflict, **gold** for a possible overlap, **green** for
  clear. Each always comes with its word, so colour is never the only signal.

## Controller

- Everything can be reached with the D-pad and A. Child windows that hold
  controls use `ImGuiChildFlags_NavFlattened`.
- LB and RB step through the tabs.
- Recording a key or a combination stops when you change tab or page.
- Never change a list (`modLayout.tabs`, the radial slots) while looping over
  it: note the change in the loop and save it after.

## Checking a change

Run `python scripts/render_preview.py`, which renders every tab and the
radial menu to `dist/screenshots`, and look at the images before shipping a UI
change.
