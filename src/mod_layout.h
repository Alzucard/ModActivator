#pragma once

struct ModPlacement {
  std::string binding;
  int tab = 0;
};
struct ModLayout {
  std::map<int, std::string> tabs;
  std::vector<ModPlacement> placements;
  // Every tab left to right, as you dragged them; empty for the default.
  std::vector<int> order;
};
static ModLayout modLayout;
// Bumped whenever modLayout changes, so the menu re-sorts its cards then and
// not every frame.
static std::atomic<unsigned> layoutVersion{1};
static constexpr int firstCustomTab = 3;
// Zero means automatic in existing configs. Use distinct IDs for overrides.
static constexpr int modMenusPlacement = -1;
static constexpr int togglesPlacement = -2;

// hexText and unhexText are in descriptions.h.

// A whole profile section, as the double-null-terminated list of key=value
// lines the profile API returns, however long it is.
static std::vector<char> readProfileSection(const char *name, const std::string &path) {
  std::vector<char> section(32768);
  while (true) {
    const DWORD size = GetPrivateProfileSectionA(name, section.data(),
                                                 (DWORD)section.size(), path.c_str());
    if (size < section.size() - 2) {
      section.resize(size + 2); // keep the two terminating nulls
      return section;
    }
    section.resize(section.size() * 2);
  }
}

static bool fileExists(const std::string &path) {
  return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Versions before ModActivator.User.ini kept the menu's choices in
// ModActivator.ini (old), which an update replaces. Copies them over once,
// when there is no user file yet, and only when there is something to copy.
// Returns whether it did. old is left as it is.
static bool migrateUserConfig(const std::string &old) {
  const auto user = userConfigPath();
  if (fileExists(user) || !fileExists(old))
    return false;
  bool copied = false;
  for (const char *name : {"BindingTypes", "ModLayout"}) {
    const auto section = readProfileSection(name, old);
    if (section[0] && WritePrivateProfileSectionA(name, section.data(), user.c_str()))
      copied = true;
  }
  // Saved by the menu as hex codes (0x11+0x08); the shipped default spelled
  // out in words is no choice of yours, so it stays behind.
  char opener[128]{};
  GetPrivateProfileStringA("General", "OpenShortcut", "", opener, sizeof opener, old.c_str());
  if (!_strnicmp(opener, "0x", 2) &&
      WritePrivateProfileStringA("General", "OpenShortcut", opener, user.c_str()))
    copied = true;
  return copied;
}

// Puts files older versions left in the wrong place where this one looks.
// 1.3.2 to 1.4.3 kept their INIs in bin64 beside the plugin, and they move
// into the ModActivator folder beside it now, in bin64 or bin64\scripts;
// before 1.3 the log sat in bin64, and it moves into the folder too. Runs
// before the log is opened, since the log is one of the files, so it returns
// what it did for the log instead of writing it.
static std::vector<std::string> relocateFiles() {
  std::vector<std::string> done;
  const auto move = [&](const std::string &from, const std::string &to) {
    return fileExists(from) && !fileExists(to) &&
           MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_COPY_ALLOWED);
  };
  for (const char *name : {"ModActivator.log.old", "ModActivator.log"})
    if (fileExists(folder + name)) {
      ensureDataFolder();
      if (move(folder + name, dataPath(name)))
        done.push_back(std::string("Moved ") + name + " into the " + dataFolderName + " folder.");
    }

  // Your choices. Should the folder already hold a user file, that one is
  // kept and bin64's renamed, so neither is lost and this happens once.
  const auto user = userConfigPath(), oldUser = folder + "ModActivator.User.ini";
  if (fileExists(oldUser))
    ensureDataFolder();
  if (move(oldUser, user))
    done.push_back("Moved ModActivator.User.ini from bin64 into the ModActivator folder.");
  else if (fileExists(oldUser) &&
           MoveFileExA(oldUser.c_str(), (oldUser + ".moved").c_str(), MOVEFILE_REPLACE_EXISTING))
    done.push_back("Kept the ModActivator folder's ModActivator.User.ini and renamed the one in "
                   "bin64 ModActivator.User.ini.moved. It can be deleted.");

  // The settings file. Alone in bin64 it is yours and moves as it is; beside
  // a fresh one in the folder, the options you set carry over into that and
  // the old one is renamed, so this happens once and later edits stand.
  const auto ini = dataPath("ModActivator.ini"), oldIni = folder + "ModActivator.ini";
  if (fileExists(oldIni))
    ensureDataFolder();
  if (move(oldIni, ini)) {
    done.push_back("Moved ModActivator.ini from bin64 into the ModActivator folder.");
  } else if (fileExists(oldIni)) {
    const auto general = readProfileSection("General", oldIni);
    for (const char *line = general.data(); *line; line += strlen(line) + 1) {
      const std::string entry = line;
      const auto eq = entry.find('=');
      if (eq == entry.npos || !eq)
        continue;
      WritePrivateProfileStringA("General", entry.substr(0, eq).c_str(),
                                 entry.substr(eq + 1).c_str(), ini.c_str());
    }
    if (MoveFileExA(oldIni.c_str(), (oldIni + ".moved").c_str(), MOVEFILE_REPLACE_EXISTING))
      done.push_back("Carried your [General] options from bin64\\ModActivator.ini into the "
                     "ModActivator folder's ModActivator.ini, and renamed the old one "
                     "ModActivator.ini.moved. It can be deleted.");
  }

  // Before 1.3, the menu's choices were in ModActivator.ini itself.
  if (migrateUserConfig(ini))
    done.push_back("Moved your menu settings from ModActivator.ini to ModActivator.User.ini, "
                   "where updates no longer replace them.");
  return done;
}

// --- the radial menu's settings -----------------------------------------------
// Up to eight pinned cards, by identity, in ring order (clockwise from the
// top): [Radial] S0 to S7 of ModActivator.User.ini, hex-encoded like the
// layout. The hold key, the controller combination and what happens to the
// game while the ring is open are [General] settings: yours from the menu
// first, then ModActivator.ini's, then the defaults.
static constexpr size_t radialSlotLimit = 8;
static std::vector<std::string> radialSlots; // render thread, after loadLayout
static std::string radialKeyText = "Grave";   // render thread
enum RadialPause { radialPauseOff = 0, radialPauseSlow = 1, radialPauseFreeze = 2 };
static std::atomic<int> radialPause{radialPauseOff};
static const char *const radialPauseNames[] = {"Off", "Slow", "Freeze"};

static std::string generalSetting(const char *key) {
  char value[128]{};
  GetPrivateProfileStringA("General", key, "", value, sizeof value, userConfigPath().c_str());
  if (!value[0])
    GetPrivateProfileStringA("General", key, "", value, sizeof value,
                             dataPath("ModActivator.ini").c_str());
  return trimConfigLine(value);
}
// The hold key for the current keyboard layout, as a shortcutCode, published
// to radialCode for the conflict checks. Grave, the default, is the key left
// of 1 wherever it sits on the layout; None turns the key off.
static unsigned radialKeyCode() {
  static HKL layout = nullptr;
  static std::string text;
  static unsigned code = 0;
  const HKL now = gameKeyboardLayout();
  if (now != layout || text != radialKeyText) {
    layout = now;
    text = radialKeyText;
    code = text.empty() || !_stricmp(text.c_str(), "none") ? 0 : shortcutCode(parseShortcut(text, now));
    radialCode.store(code);
  }
  return code;
}
static void loadRadial() {
  const auto key = generalSetting("RadialKey");
  radialKeyText = key.empty() ? "Grave" : key;
  radialKeyCode();
  const auto pad = generalSetting("RadialPad");
  const unsigned bits = padButtonsFrom(pad);
  const bool off = !_stricmp(pad.c_str(), "none");
  padRadial.store(off ? 0 : pad.empty() || __builtin_popcount(bits) < 2 ? defaultPadRadial : bits);
  ml::State::Get().padRadial.store(padRadial.load());
  const auto pause = lower(generalSetting("RadialPause"));
  radialPause.store(pause == "freeze" ? radialPauseFreeze : pause == "slow" ? radialPauseSlow : radialPauseOff);
  radialSlots.clear();
  const auto path = userConfigPath();
  for (size_t i = 0; i < radialSlotLimit; ++i) {
    char value[1024]{};
    GetPrivateProfileStringA("Radial", ("S" + std::to_string(i)).c_str(), "", value, sizeof value,
                             path.c_str());
    auto id = unhexText(value);
    if (!id.empty() && std::find(radialSlots.begin(), radialSlots.end(), id) == radialSlots.end())
      radialSlots.push_back(std::move(id));
  }
}
static bool saveRadialSlots(std::vector<std::string> next) {
  if (next.size() > radialSlotLimit)
    return false;
  std::string section;
  for (size_t i = 0; i < next.size(); ++i)
    section += "S" + std::to_string(i) + "=" + hexText(next[i]) + '\0';
  if (section.empty())
    section += '\0';
  if (!WritePrivateProfileSectionA("Radial", section.c_str(), writableUserConfig().c_str()))
    return false;
  radialSlots = std::move(next);
  return true;
}
static bool radialPinned(const std::string &id) {
  return std::find(radialSlots.begin(), radialSlots.end(), id) != radialSlots.end();
}
// Pins a card to the end of the ring, or takes it off. False when the ring is
// full or the file cannot be written.
static bool toggleRadialPin(const std::string &id) {
  auto next = radialSlots;
  if (const auto at = std::find(next.begin(), next.end(), id); at != next.end())
    next.erase(at);
  else if (next.size() < radialSlotLimit)
    next.push_back(id);
  else
    return false;
  return saveRadialSlots(std::move(next));
}
static bool saveRadialKey(const std::string &text) {
  if (!WritePrivateProfileStringA("General", "RadialKey", text.c_str(), writableUserConfig().c_str()))
    return false;
  radialKeyText = text;
  radialKeyCode();
  refreshRequested.store(true); // conflicts are worked out again
  return true;
}
static bool saveRadialPad(unsigned bits) {
  if (!WritePrivateProfileStringA("General", "RadialPad", bits ? padLabel(bits).c_str() : "None",
                                  writableUserConfig().c_str()))
    return false;
  padRadial.store(bits);
  ml::State::Get().padRadial.store(bits);
  refreshRequested.store(true);
  return true;
}
static bool saveRadialPause(int mode) {
  if (mode < radialPauseOff || mode > radialPauseFreeze ||
      !WritePrivateProfileStringA("General", "RadialPause", radialPauseNames[mode],
                                  writableUserConfig().c_str()))
    return false;
  radialPause.store(mode);
  return true;
}
// The panel's size on top of the one the resolution gives it, in percent:
// [General] UISize, from Settings -> Interface.
static constexpr int uiSizes[] = {80, 90, 100, 110, 125, 150};
static std::atomic<int> uiSize{100};
static void loadUiSize() {
  const int percent = atoi(generalSetting("UISize").c_str());
  uiSize.store(percent >= 50 && percent <= 200 ? percent : 100);
}
static bool saveUiSize(int percent) {
  if (!WritePrivateProfileStringA("General", "UISize", std::to_string(percent).c_str(),
                                  writableUserConfig().c_str()))
    return false;
  uiSize.store(percent);
  return true;
}

// Keys written as the menu records them: hex codes joined with +, like the
// panel's own shortcut.
static std::string keyCodesText(const std::vector<int> &keys) {
  std::string value;
  for (int key : keys) {
    char hex[8]{};
    snprintf(hex, sizeof hex, "0x%02X", key);
    value += (value.empty() ? "" : "+") + std::string(hex);
  }
  return value;
}

static void loadLayout() {
  const auto path = userConfigPath();
  // Yours from the menu first; then ModActivator.ini, for anyone who sets it
  // by hand there.
  char value[128]{};
  GetPrivateProfileStringA("General", "OpenShortcut", "", value, sizeof value, path.c_str());
  if (!value[0])
    GetPrivateProfileStringA("General", "OpenShortcut", "", value, sizeof value,
                             dataPath("ModActivator.ini").c_str());
  const auto code = shortcutCode(parseShortcut(value));
  opener.store(code && (code & 0xff) != VK_ESCAPE ? code : defaultOpener);
  // The controller shortcut, the same way. None turns it off; a single button
  // is refused, so one press can never open the panel.
  char pad[128]{};
  GetPrivateProfileStringA("General", "PadOpenShortcut", "", pad, sizeof pad, path.c_str());
  if (!pad[0])
    GetPrivateProfileStringA("General", "PadOpenShortcut", "", pad, sizeof pad,
                             dataPath("ModActivator.ini").c_str());
  const unsigned padBits = padButtonsFrom(pad);
  const bool off = !_stricmp(trimConfigLine(pad).c_str(), "none");
  padOpener.store(off ? 0 : __builtin_popcount(padBits) >= 2 ? padBits : defaultPadOpener);
  ml::State::Get().padShortcut.store(padOpener.load());
  loadRadial();
  loadUiSize();
  const auto section = readProfileSection("ModLayout", path);
  ModLayout loaded;
  std::map<unsigned long, ModPlacement> ordered;
  for (const char *p = section.data(); *p; p += strlen(p) + 1) {
    const std::string line = p;
    const auto eq = line.find('=');
    if (eq < 2 || eq == line.npos)
      continue;
    const auto index = strtoul(line.c_str() + 1, nullptr, 10);
    if (!line.compare(0, eq, "Order")) {
      for (const char *at = line.c_str() + eq + 1; *at;) {
        char *end = nullptr;
        const long id = strtol(at, &end, 10);
        if (end == at)
          break;
        loaded.order.push_back((int)id);
        at = *end == ',' ? end + 1 : end;
      }
    } else if (line[0] == 'T' && index >= firstCustomTab && index < 65536) {
      auto name = unhexText(line.substr(eq + 1));
      if (!name.empty())
        loaded.tabs[(int)index] = std::move(name);
    } else if (line[0] == 'M') {
      const auto sep = line.find(':', eq + 1);
      if (sep == line.npos)
        continue;
      auto binding = unhexText(line.substr(sep + 1));
      if (!binding.empty())
        ordered[index] = {std::move(binding), atoi(line.c_str() + eq + 1)};
    }
  }
  for (auto &[index, place] : ordered)
    loaded.placements.push_back(std::move(place));
  modLayout = std::move(loaded);
  ++layoutVersion;
}
static bool saveLayout(ModLayout next) {
  std::string section;
  for (const auto &[id, name] : next.tabs)
    section += "T" + std::to_string(id) + "=" + hexText(name) + '\0';
  if (!next.order.empty()) {
    section += "Order=";
    for (size_t i = 0; i < next.order.size(); ++i)
      section += (i ? "," : "") + std::to_string(next.order[i]);
    section += ' ';
  }
  size_t index = 0;
  for (const auto &place : next.placements)
    section += "M" + std::to_string(index++) + "=" + std::to_string(place.tab) +
               ":" + hexText(place.binding) + '\0';
  if (section.empty())
    section += '\0';
  if (!WritePrivateProfileSectionA("ModLayout", section.c_str(), writableUserConfig().c_str()))
    return false;
  modLayout = std::move(next);
  ++layoutVersion;
  return true;
}
// The tabs left to right: Mod menus, Toggles and yours, in the order you
// dragged them, with any tab missing from it (a new one) last. Hotkeys is a
// button beside Settings, not a tab; an order saved while it was one still
// lists 2, which is passed over.
static std::vector<std::pair<int, std::string>> orderedTabs(const ModLayout &layout) {
  std::map<int, std::string> all{{0, "Mod menus"}, {1, "Toggles"}};
  all.insert(layout.tabs.begin(), layout.tabs.end());
  std::vector<std::pair<int, std::string>> result;
  for (const int id : layout.order) {
    const auto found = all.find(id);
    if (found != all.end()) {
      result.emplace_back(*found);
      all.erase(found);
    }
  }
  result.insert(result.end(), all.begin(), all.end());
  return result;
}
static int assignedTab(const std::string &binding) {
  for (const auto &place : modLayout.placements)
    if (place.binding == binding &&
        (place.tab == modMenusPlacement || place.tab == togglesPlacement ||
         modLayout.tabs.count(place.tab)))
      return place.tab;
  return 0;
}
static int actionTab(const Action &action) {
  const int assigned = assignedTab(cachedIdentity(action));
  if (assigned == modMenusPlacement)
    return 0;
  if (assigned == togglesPlacement)
    return 1;
  return assigned ? assigned : action.toggle ? 1 : 0;
}
static void orderActions(std::vector<Action> &actions) {
  if (modLayout.placements.empty())
    return;
  std::map<std::string, size_t> rank;
  for (size_t i = 0; i < modLayout.placements.size(); ++i)
    rank.emplace(modLayout.placements[i].binding, i);
  const auto position = [&](const Action &action) {
    const auto it = rank.find(cachedIdentity(action));
    return it == rank.end() ? rank.size() : it->second;
  };
  std::stable_sort(actions.begin(), actions.end(), [&](const Action &a, const Action &b) {
    return position(a) < position(b);
  });
}
static ModPlacement &placementFor(ModLayout &layout, const std::string &id) {
  for (auto &place : layout.placements)
    if (place.binding == id)
      return place;
  return layout.placements.emplace_back(ModPlacement{id, 0});
}
struct PlacementDrop {
  std::string source, target;
  bool after = false;
};
static bool reorderActions(const std::vector<Action> &actions, const PlacementDrop &drop) {
  const auto findAction = [&](const std::string &id) {
    return std::find_if(actions.begin(), actions.end(),
                        [&](const Action &a) { return identity(a) == id; });
  };
  const auto sourceAction = findAction(drop.source);
  const auto targetAction = findAction(drop.target);
  if (sourceAction == actions.end() || targetAction == actions.end() ||
      sourceAction == targetAction || actionTab(*sourceAction) != actionTab(*targetAction))
    return true;
  auto next = modLayout;
  for (const auto &a : actions)
    placementFor(next, identity(a));
  const auto findPlacement = [&](const std::string &id) {
    return std::find_if(next.placements.begin(), next.placements.end(),
                        [&](const ModPlacement &place) { return place.binding == id; });
  };
  const auto source = findPlacement(drop.source);
  const auto target = findPlacement(drop.target) + (drop.after ? 1 : 0);
  if (target == source || target == source + 1)
    return true;
  if (source < target)
    std::rotate(source, source + 1, target);
  else
    std::rotate(target, source, source + 1);
  return saveLayout(std::move(next));
}

static constexpr char placementPayload[] = "ModActivatorPlacement";
static void dragHandle(const Action &action, float size) {
  ImGui::Button("##Drag", ImVec2(size, size));
  const auto min = ImGui::GetItemRectMin();
  const auto max = ImGui::GetItemRectMax();
  const ImVec2 center((min.x + max.x) * .5f, (min.y + max.y) * .5f);
  auto *draw = ImGui::GetWindowDrawList();
  const auto color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
  for (int y = -1; y <= 1; ++y)
    for (int x : {-1, 1})
      draw->AddCircleFilled(ImVec2(center.x + x * size * .085f,
                                  center.y + y * size * .16f), size * .045f, color);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Drag to reorder");
  if (ImGui::BeginDragDropSource()) {
    const auto binding = identity(action);
    ImGui::SetDragDropPayload(placementPayload, binding.data(), binding.size());
    ImGui::TextUnformatted(UI::name(action).c_str());
    ImGui::EndDragDropSource();
  }
}
static void dropOnCard(const Action &action, PlacementDrop &drop) {
  if (!ImGui::BeginDragDropTarget())
    return;
  const auto min = ImGui::GetItemRectMin();
  const auto max = ImGui::GetItemRectMax();
  const bool after = ImGui::GetMousePos().y >= (min.y + max.y) * .5f;
  if (const auto *payload = ImGui::AcceptDragDropPayload(
          placementPayload, ImGuiDragDropFlags_AcceptBeforeDelivery |
                            ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
    const std::string source(static_cast<const char *>(payload->Data), payload->DataSize);
    const auto target = identity(action);
    if (source != target) {
      const float y = after ? max.y : min.y;
      ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, y), ImVec2(max.x, y),
                                         ImGui::GetColorU32(UI::gold), 2.0f);
      if (payload->IsDelivery())
        drop = {source, target, after};
    }
  }
  ImGui::EndDragDropTarget();
}

static bool saveOpener(const std::vector<int> &keys) {
  const auto code = shortcutCode(keys);
  if (!code || (code & 0xff) == VK_ESCAPE)
    return false;
  const auto value = keyCodesText(keys);
  if (!WritePrivateProfileStringA("General", "OpenShortcut", value.c_str(),
                                  writableUserConfig().c_str()))
    return false;
  opener.store(code);
  refreshRequested.store(true);
  return true;
}
// The panel's controller shortcut, saved in ModActivator.User.ini like the
// keyboard one, as button names ("LB + RB + Back"), or None for none.
static bool savePadOpener(unsigned bits) {
  if (!WritePrivateProfileStringA("General", "PadOpenShortcut",
                                  bits ? padLabel(bits).c_str() : "None",
                                  writableUserConfig().c_str()))
    return false;
  padOpener.store(bits);
  ml::State::Get().padShortcut.store(bits);
  refreshRequested.store(true);
  return true;
}
// While Settings records a controller shortcut, its buttons do not toggle
// the panel.
static bool padShortcutRecording = false;
// Records a controller combination: every button held at once, taken when
// they are all let go again. Only from a controller with nothing held: the
// A press that started the recording is not the first button of it.
// Render thread; the menu is open while it runs.
static unsigned recordPadShortcut(bool &recording) {
  static unsigned most = 0;
  static bool released = false;
  if (!recording) {
    most = 0;
    released = false;
    return 0;
  }
  XINPUT_GAMEPAD pad{};
  if (!ml::hooks::ReadPadRaw(&pad))
    return 0;
  const unsigned held = padButtonsHeld(pad);
  if (!released) {
    released = !held;
    return 0;
  }
  if (held) {
    if (__builtin_popcount(held) >= __builtin_popcount(most))
      most = held;
    return 0;
  }
  const unsigned taken = most;
  most = 0;
  if (taken)
    recording = false;
  return taken;
}

// --- Settings -------------------------------------------------------------------
// A list of pages on the left, one topic each, and rows on the right that all
// look alike: the label in a column, the current value, then the buttons
// (docs/UI-GUIDE.md).

// The controller recording is for the radial menu's combination, not the
// panel's.
static bool padRecordingRadial = false;

// Stops any key or combination being recorded: on a change of tab or page.
static void stopRecording() {
  UI::recording = 0;
  padShortcutRecording = false;
  padRecordingRadial = false;
}

// What the last change in Settings did, at the bottom of its page.
static std::string settingsStatus;

static float settingsLabelWidth() { return 120 * UI::scale; }
static float settingsValueWidth() { return 190 * UI::scale; }

// A row's label, in the column on the left; its controls follow on the line.
static void settingLabel(const char *label, const char *tooltip = nullptr) {
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  if (tooltip && ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", tooltip);
  ImGui::SameLine(settingsLabelWidth());
}

// The current value, framed like a field so the values of a page line up.
static void settingValue(const std::string &text, bool active, bool off) {
  const auto &style = ImGui::GetStyle();
  const ImVec2 min = ImGui::GetCursorScreenPos();
  const ImVec2 max(min.x + settingsValueWidth(), min.y + ImGui::GetFrameHeight());
  ImGui::Dummy(ImVec2(max.x - min.x, max.y - min.y));
  auto *draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(min, max, ImGui::GetColorU32(active ? ImGuiCol_FrameBgActive : ImGuiCol_FrameBg),
                      style.FrameRounding);
  draw->PushClipRect(min, ImVec2(max.x - style.FramePadding.x, max.y), true);
  draw->AddText(ImVec2(min.x + style.FramePadding.x, min.y + style.FramePadding.y),
                ImGui::GetColorU32(off && !active ? ImGuiCol_TextDisabled : ImGuiCol_Text), text.c_str());
  draw->PopClipRect();
  ImGui::SameLine();
}

// Details under a row (a recorded key, what it overlaps, Save), lined up
// with the values.
static void beginRowDetails() { ImGui::Indent(settingsLabelWidth()); }
static void endRowDetails() { ImGui::Unindent(settingsLabelWidth()); }

enum class RowAction { None, Save, Default, Off };

// A keyboard shortcut's row: Label [keys] Record Default Off. A recorded key
// shows under it with what it overlaps, and Save and Discard.
struct KeyEdit {
  std::vector<int> proposed;
  bool acknowledge = false;
};
static RowAction keyRow(const char *label, const char *id, const std::string &current, KeyEdit &row,
                        bool canTurnOff, const std::vector<int> &ownKeys, const char *ownName,
                        const char *note = nullptr) {
  ImGui::PushID(id);
  RowAction result = RowAction::None;
  const ImGuiID recordId = ImGui::GetID("Record");
  const bool recording = UI::recording == recordId;
  settingLabel(label);
  std::string shown = current;
  if (recording) {
    shown = shortcutLabel(UI::captureModifiers());
    shown += shown.empty() ? "Press the keys..." : " + ...";
  }
  settingValue(shown, recording, current == "Off");
  if (ImGui::Button(recording ? "Cancel" : "Record")) {
    if (recording)
      UI::recording = 0;
    else
      UI::beginShortcutCapture(recordId);
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", recording ? "Or press Escape." : "Press the new keys after clicking.");
  if (auto keys = UI::captureShortcut(recordId); !keys.empty()) {
    row.proposed = std::move(keys);
    row.acknowledge = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Default"))
    result = RowAction::Default;
  if (canTurnOff) {
    ImGui::SameLine();
    if (ImGui::Button("Off"))
      result = RowAction::Off;
  }
  if (!row.proposed.empty()) {
    beginRowDetails();
    ImGui::Text("New: %s", shortcutLabel(row.proposed).c_str());
    const ImVec4 warn(1, .6f, .2f, 1);
    bool clashes = false;
    if (!ownKeys.empty() && conflict(row.proposed, ownKeys)) {
      clashes = true;
      ImGui::TextColored(warn, "Overlaps %s (%s)", ownName, shortcutLabel(ownKeys).c_str());
    }
    for (const auto &a : bindingsSnapshot())
      if (conflict(row.proposed, a.cachedKeys)) {
        clashes = true;
        ImGui::TextColored(warn, "Overlaps %s / %s", UI::name(a).c_str(), UI::actionName(a).c_str());
      }
    for (const auto &[severity, who] : reservedClashes(row.proposed)) {
      clashes = true;
      ImGui::TextColored(warn, "%s %s", severity == 2 ? "Already used by" : "Overlaps", displayText(who).c_str());
    }
    if (note)
      ImGui::TextDisabled("%s", note);
    if (clashes)
      ImGui::Checkbox("Save anyway", &row.acknowledge);
    ImGui::BeginDisabled((clashes && !row.acknowledge) || row.proposed.back() == VK_ESCAPE);
    if (ImGui::Button("Save"))
      result = RowAction::Save;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Discard"))
      row.proposed.clear();
    endRowDetails();
  }
  ImGui::PopID();
  return result;
}

// A controller combination's row, the same way. One recording at a time,
// for the panel's combination or the radial menu's.
struct PadEdit {
  unsigned proposed = 0;
};
static RowAction padRow(const char *label, const char *id, unsigned current, PadEdit &row, bool radial,
                        unsigned other, const char *otherName) {
  ImGui::PushID(id);
  RowAction result = RowAction::None;
  const bool recording = padShortcutRecording && padRecordingRadial == radial;
  settingLabel(label);
  settingValue(recording ? "Press and release the buttons..." : current ? padLabel(current) : "Off", recording,
               !current);
  if (ImGui::Button(recording ? "Cancel" : "Record")) {
    padShortcutRecording = !recording;
    padRecordingRadial = radial;
    row.proposed = 0;
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Hold two buttons or more together, then let go.");
  ImGui::SameLine();
  if (ImGui::Button("Default"))
    result = RowAction::Default;
  ImGui::SameLine();
  if (ImGui::Button("Off"))
    result = RowAction::Off;
  if (row.proposed) {
    beginRowDetails();
    ImGui::Text("New: %s", padLabel(row.proposed).c_str());
    const ImVec4 warn(1, .6f, .2f, 1);
    if (padConflict(row.proposed, other))
      ImGui::TextColored(warn, "Overlaps %s (%s)", otherName, padLabel(other).c_str());
    for (const auto &a : bindingsSnapshot())
      if (padConflict(row.proposed, a.padButtons))
        ImGui::TextColored(warn, "Overlaps %s / %s (%s)", UI::name(a).c_str(), UI::actionName(a).c_str(),
                           padLabel(a.padButtons).c_str());
    if (ImGui::Button("Save"))
      result = RowAction::Save;
    ImGui::SameLine();
    if (ImGui::Button("Discard"))
      row.proposed = 0;
    endRowDetails();
  }
  ImGui::PopID();
  return result;
}

static KeyEdit openerRow, radialKeyRow;
static PadEdit padOpenerRow, padRadialRow;

// Settings -> Shortcuts: opening and closing the panel.
static void drawShortcutSettings() {
  auto &status = settingsStatus;
  ImGui::SeparatorText("Open and close the panel");
  switch (keyRow("Keyboard", "Opener", shortcutLabel(openerKeys()), openerRow, false,
                 radialKeyCode() ? codeKeys(radialKeyCode()) : std::vector<int>{}, "the radial menu's key")) {
  case RowAction::Save:
    if (saveOpener(openerRow.proposed)) {
      status = "Shortcut saved.";
      openerRow.proposed.clear();
    } else {
      status = "Could not save the shortcut.";
    }
    break;
  case RowAction::Default:
    openerRow.proposed = {VK_CONTROL, VK_SHIFT, VK_BACK};
    openerRow.acknowledge = false;
    UI::recording = 0;
    break;
  default:
    break;
  }
  switch (padRow("Controller", "PadOpener", padOpener.load(), padOpenerRow, false, padRadial.load(),
                 "the radial menu's combination")) {
  case RowAction::Save:
    status = savePadOpener(padOpenerRow.proposed) ? "Controller shortcut saved."
                                                  : "Could not save the controller shortcut.";
    padOpenerRow.proposed = 0;
    break;
  case RowAction::Default:
    padShortcutRecording = false;
    padOpenerRow.proposed = defaultPadOpener;
    break;
  case RowAction::Off:
    padShortcutRecording = false;
    padOpenerRow.proposed = 0;
    status = savePadOpener(0) ? "Controller shortcut off." : "Could not save the controller shortcut.";
    break;
  default:
    break;
  }
  if (!ml::Settings::Get().hookXInput)
    ImGui::TextDisabled("HookXInput=0: the game also sees the buttons you press for the panel.");
}

// Settings -> Radial menu: its keys, what the game does while it is open, and
// its eight entries in order.
static void drawRadialSettings(const std::vector<Action> &actions) {
  auto &status = settingsStatus;
  ImGui::SeparatorText("Open the ring");
  ImGui::TextDisabled("Hold, aim with the mouse or the right stick, let go to open.");
  const unsigned code = radialKeyCode();
  switch (keyRow("Keyboard", "RadialKey", code ? shortcutLabel(codeKeys(code)) : "Off", radialKeyRow, true,
                 openerKeys(), "the panel's shortcut", "The game sees this key too: pick one it does not use.")) {
  case RowAction::Save:
    status = saveRadialKey(keyCodesText(radialKeyRow.proposed)) ? "Radial key saved." : "Could not save the radial key.";
    radialKeyRow.proposed.clear();
    break;
  case RowAction::Default:
    status = saveRadialKey("Grave") ? "Radial key reset to the key left of 1." : "Could not save the radial key.";
    radialKeyRow.proposed.clear();
    break;
  case RowAction::Off:
    status = saveRadialKey("None") ? "Radial key off." : "Could not save the radial key.";
    radialKeyRow.proposed.clear();
    break;
  default:
    break;
  }
  switch (padRow("Controller", "RadialPad", padRadial.load(), padRadialRow, true, padOpener.load(),
                 "the panel's controller shortcut")) {
  case RowAction::Save:
    status = saveRadialPad(padRadialRow.proposed) ? "Radial combination saved." : "Could not save it.";
    padRadialRow.proposed = 0;
    break;
  case RowAction::Default:
    padShortcutRecording = false;
    padRadialRow.proposed = defaultPadRadial;
    break;
  case RowAction::Off:
    padShortcutRecording = false;
    padRadialRow.proposed = 0;
    status = saveRadialPad(0) ? "Radial combination off." : "Could not save it.";
    break;
  default:
    break;
  }

  ImGui::Spacing();
  ImGui::SeparatorText("While the ring is open");
  int pause = radialPause.load();
  static const char *pauseLabels[] = {"Keeps running", "Slow motion", "Freezes"};
  settingLabel("The game", "Slow motion and freezing use the game's own speed control.");
  ImGui::SetNextItemWidth(settingsValueWidth());
  if (ImGui::Combo("##Pause", &pause, pauseLabels, IM_ARRAYSIZE(pauseLabels))) {
    status = saveRadialPause(pause) ? "Saved." : "Could not save it.";
    if (pause)
      gameSpeed::prepare();
  }
  if (radialPause.load()) {
    beginRowDetails();
    ImGui::TextDisabled("Speed control: %s", gameSpeed::statusText());
    endRowDetails();
  }

  ImGui::Spacing();
  ImGui::SeparatorText("Entries, clockwise from the top");
  if (radialSlots.empty()) {
    ImGui::TextDisabled("None yet. Pin a mod with the ring button on its card in Mod menus or Toggles.");
    return;
  }
  if (!ImGui::BeginTable("RadialSlots", 3, ImGuiTableFlags_RowBg))
    return;
  ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
  ImGui::TableSetupColumn("Entry", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn("##Move", ImGuiTableColumnFlags_WidthFixed);
  for (size_t i = 0; i < radialSlots.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    const auto &id = radialSlots[i];
    const auto found = std::find_if(actions.begin(), actions.end(),
                                    [&](const Action &a) { return cachedIdentity(a) == id; });
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%zu", i + 1);
    ImGui::TableSetColumnIndex(1);
    ImGui::AlignTextToFramePadding();
    if (found == actions.end())
      ImGui::TextDisabled("%s (not loaded)", id.c_str());
    else
      ImGui::Text("%s / %s", UI::name(*found).c_str(), UI::actionName(*found).c_str());
    ImGui::TableSetColumnIndex(2);
    auto next = radialSlots;
    bool changed = false;
    ImGui::BeginDisabled(i == 0);
    if (ImGui::SmallButton("Up")) {
      std::swap(next[i], next[i - 1]);
      changed = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(i + 1 == radialSlots.size());
    if (ImGui::SmallButton("Down")) {
      std::swap(next[i], next[i + 1]);
      changed = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton("Remove")) {
      next.erase(next.begin() + i);
      changed = true;
    }
    ImGui::PopID();
    if (changed) {
      status = saveRadialSlots(std::move(next)) ? "Radial menu saved." : "Could not save the radial menu.";
      break; // the list changed under the loop
    }
  }
  ImGui::EndTable();
}

// Whether a custom tab may take this name: not empty, not a built-in tab's,
// not another custom tab's.
static bool tabNameFree(const std::string &name, int except) {
  const auto folded = lower(name);
  if (name.empty() || name.find("##") != name.npos || folded == "mod menus" || folded == "toggles" ||
      folded == "hotkeys" || folded == "settings")
    return false;
  for (const auto &[id, existing] : modLayout.tabs)
    if (id != except && lower(existing) == folded)
      return false;
  return true;
}

// --- your own tabs ---------------------------------------------------------------
// A + after them adds one; a right click on one offers Rename and Delete. The
// popups are opened and drawn at the window's level, after the tab bar, from
// requests the tab bar leaves here.
static const char *tabPopupRequest = nullptr;
static int tabMenuFor = 0;      // the tab the right click was on
static int tabEditing = 0;      // the tab being renamed; 0 for a new one
static int tabToSelect = 0;     // a tab just added, selected on the next frame; 0 for none
                                // (a custom tab's id is never 0, Mod menus' is)
static char tabNameText[64]{};
static std::string tabNameError;

// After a custom tab's BeginTabItem, while it is the last item.
static void tabContextClick(int tab) {
  if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
    tabMenuFor = tab;
    tabPopupRequest = "TabMenu";
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    ImGui::SetTooltip("Right-click to rename or delete");
}

// The + after your tabs.
static void newTabButton() {
  if (ImGui::TabItemButton("+", ImGuiTabItemFlags_NoTooltip)) {
    tabEditing = 0;
    tabNameText[0] = 0;
    tabNameError.clear();
    tabPopupRequest = "TabName";
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    ImGui::SetTooltip("New tab");
}

// The right-click menu and the name popup. Saving replaces modLayout, which
// is why none of this runs inside the tab bar's loop over the tabs.
static void drawTabPopups() {
  if (tabPopupRequest) {
    ImGui::OpenPopup(tabPopupRequest);
    tabPopupRequest = nullptr;
  }
  int remove = 0;
  if (ImGui::BeginPopup("TabMenu")) {
    const auto found = modLayout.tabs.find(tabMenuFor);
    if (found == modLayout.tabs.end()) {
      ImGui::CloseCurrentPopup();
    } else {
      ImGui::TextDisabled("%s", found->second.c_str());
      ImGui::Separator();
      if (ImGui::Selectable("Rename")) {
        tabEditing = tabMenuFor;
        snprintf(tabNameText, sizeof tabNameText, "%s", found->second.c_str());
        tabNameError.clear();
        tabPopupRequest = "TabName";
      }
      if (ImGui::Selectable("Delete"))
        remove = tabMenuFor;
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Its mods go back to Mod menus or Toggles.");
    }
    ImGui::EndPopup();
  }
  if (remove) {
    auto next = modLayout;
    next.tabs.erase(remove);
    next.order.erase(std::remove(next.order.begin(), next.order.end(), remove), next.order.end());
    for (auto &place : next.placements)
      if (place.tab == remove)
        place.tab = 0;
    if (!saveLayout(std::move(next)))
      LOG_ERR("[menu] could not save ModActivator.User.ini after deleting a tab.");
  }
  if (ImGui::BeginPopup("TabName")) {
    ImGui::TextUnformatted(tabEditing ? "Rename tab" : "New tab");
    if (ImGui::IsWindowAppearing())
      ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(200 * UI::scale);
    const bool entered = ImGui::InputTextWithHint("##TabName", "Name", tabNameText, sizeof tabNameText,
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
    if (!tabNameError.empty())
      ImGui::TextColored(ImVec4(1, .6f, .2f, 1), "%s", tabNameError.c_str());
    if (ImGui::Button(tabEditing ? "Save" : "Add") || entered) {
      const auto text = trim(tabNameText);
      if (!tabNameFree(text, tabEditing)) {
        tabNameError = text.empty() ? "Enter a name." : "Another tab has that name.";
      } else {
        auto next = modLayout;
        int id = tabEditing;
        if (!id) {
          id = firstCustomTab;
          while (next.tabs.count(id))
            ++id;
        }
        next.tabs[id] = text;
        if (saveLayout(std::move(next))) {
          UI::noteGlyphs(text);
          if (!tabEditing)
            tabToSelect = id;
          ImGui::CloseCurrentPopup();
        } else {
          tabNameError = "Could not save the tab.";
        }
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

// Hotkeys -> Placement: which tab each mod's card is on, and whether a
// binding is a menu, a toggle or a hotkey only. cardsStamp changes whenever
// the menu rebuilds `actions`.
static void drawPlacementSettings(const std::vector<Action> &actions, unsigned cardsStamp) {
  auto &status = settingsStatus;
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("Where each mod's card goes, and what it is shown as. Hover a binding for what it does. "
                     "Add your own tabs with the + after the tabs; right-click one to rename or delete it.");
  ImGui::PopStyleColor();
  // The cards, then every other binding, rebuilt only when either the
  // catalog or the card order changes.
  // Keyed on the copies themselves, not the shared counter, so a refresh
  // landing between the two copies is picked up on the next frame.
  static std::vector<Action> bindings;
  static std::set<std::string> visible;
  static unsigned builtCards = 0, builtBindings = 0;
  const auto &all = bindingsSnapshot();
  if (builtCards != cardsStamp || builtBindings != bindingsSnapshotVersion) {
    builtCards = cardsStamp;
    builtBindings = bindingsSnapshotVersion;
    bindings = actions;
    visible.clear();
    for (const auto &action : actions)
      visible.insert(cachedIdentity(action));
    for (const auto &binding : all)
      if (!visible.count(cachedIdentity(binding)))
        bindings.push_back(binding);
  }
  if (ImGui::BeginTable("Placement", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn("Tab", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Show as", ImGuiTableColumnFlags_WidthStretch, 0.7f);
    ImGui::TableHeadersRow();
    const char *types[] = {"Automatic", "Menu", "Toggle", "Hotkeys only"};
    UI::groupedBindings(bindings, [&](const Action &action) {
      const auto &binding = cachedIdentity(action);
      ImGui::PushID(binding.c_str());
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      const auto name = UI::actionName(action);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(name.c_str());
      if (ImGui::IsItemHovered())
        UI::bindingTooltip(action);
      ImGui::TableSetColumnIndex(1);
      if (!visible.count(binding)) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("No card");
      } else {
        const int current = assignedTab(binding);
        const std::string automatic = action.toggle ? "Automatic (Toggles)" : "Automatic (Mod menus)";
        const auto label = current == modMenusPlacement ? "Mod menus"
                         : current == togglesPlacement ? "Toggles"
                         : current ? modLayout.tabs.at(current) : automatic;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##Destination", label.c_str())) {
          int destination = current;
          if (ImGui::Selectable(automatic.c_str(), current == 0))
            destination = 0;
          if (ImGui::Selectable("Mod menus", current == modMenusPlacement))
            destination = modMenusPlacement;
          if (ImGui::Selectable("Toggles", current == togglesPlacement))
            destination = togglesPlacement;
          for (const auto &[id, name] : modLayout.tabs)
            if (ImGui::Selectable((name + "###Destination" + std::to_string(id)).c_str(), current == id))
              destination = id;
          ImGui::EndCombo();
          if (destination != current) {
            auto next = modLayout;
            placementFor(next, binding).tab = destination;
            status = saveLayout(std::move(next)) ? "Placement saved." : "Could not save placement.";
          }
        }
      }
      ImGui::TableSetColumnIndex(2);
      int type = action.typeOverride;
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::Combo("##BindingType", &type, types, IM_ARRAYSIZE(types))) {
        if (saveBindingType(action, type)) {
          refreshRequested.store(true);
          status = "Binding type saved.";
        } else {
          status = "Could not save the binding type.";
        }
      }
      ImGui::PopID();
    });
    ImGui::EndTable();
  }
}

// Settings -> Interface: the panel's size, and reading the mods' configs again.
static ImRect lastRereadButton; // for the tests
static void drawInterfaceSettings() {
  ImGui::SeparatorText("Size");
  settingLabel("UI size", "On top of the size your screen resolution gives the panel.");
  const int current = uiSize.load();
  ImGui::SetNextItemWidth(settingsValueWidth());
  if (ImGui::BeginCombo("##UISize", (std::to_string(current) + " %").c_str())) {
    for (const int percent : uiSizes)
      if (ImGui::Selectable((std::to_string(percent) + " %").c_str(), percent == current) && percent != current)
        settingsStatus = saveUiSize(percent) ? "Saved." : "Could not save the UI size.";
    ImGui::EndCombo();
  }

  ImGui::SeparatorText("Mod configs");
  if (ImGui::Button("Reread mod configs")) {
    refreshRequested.store(true);
    settingsStatus = "Reading the mods' configs again: the lists update when it is done.";
  }
  lastRereadButton = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
  ImGui::TextWrapped("Reads every mod's INI and .dat files again, as opening this panel does, so a "
                     "shortcut you changed in a mod's own menu shows here. A change made in a mod's "
                     "own menu usually works in that mod at once. A config file edited any other "
                     "way may need a restart of the game before the change takes effect.");
}

static int settingsPage = 0;
static const char *const settingsPages[] = {"Shortcuts", "Radial menu", "Interface"};
static constexpr int settingsPageCount = IM_ARRAYSIZE(settingsPages);

static void drawSettings(const std::vector<Action> &actions) {
  const float u = UI::scale;
  // A controller combination is taken whichever page is showing.
  if (const unsigned taken = recordPadShortcut(padShortcutRecording)) {
    const bool enough = __builtin_popcount(taken) >= 2;
    if (!enough)
      settingsStatus = "Use two buttons or more, such as LB + RB + Back.";
    (padRecordingRadial ? padRadialRow : padOpenerRow).proposed = enough ? taken : 0;
    padRecordingRadial = false;
  }
  if (previewMode && previewTab >= settingsPreview && previewTab < placementPreview)
    settingsPage = previewTab - settingsPreview;

  ImGui::BeginChild("SettingsPages", ImVec2(120 * u, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
  for (int i = 0; i < settingsPageCount; ++i)
    if (ImGui::Selectable(settingsPages[i], settingsPage == i) && settingsPage != i) {
      settingsPage = i;
      stopRecording();
      settingsStatus.clear();
    }
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("SettingsPage", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
  switch (settingsPage) {
  case 0:
    drawShortcutSettings();
    break;
  case 1:
    drawRadialSettings(actions);
    break;
  default:
    drawInterfaceSettings();
    break;
  }
  if (!settingsStatus.empty()) {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextWrapped("%s", settingsStatus.c_str());
  }
  ImGui::EndChild();
}

// The Hotkeys tab: every binding with its shortcut, or where each card goes.
static int hotkeysView = 0; // 0 bindings, 1 placement

// One button of a two-way switch, filled when it is the one showing.
static bool viewButton(const char *label, bool chosen) {
  if (chosen)
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
  const bool pressed = ImGui::Button(label);
  if (chosen)
    ImGui::PopStyleColor();
  return pressed && !chosen;
}

static void drawHotkeysTab(const std::vector<Action> &actions, unsigned cardsStamp) {
  if (previewMode)
    hotkeysView = previewTab == placementPreview ? 1 : 0;
  ImGui::Spacing();
  const auto pick = [](int view) {
    hotkeysView = view;
    stopRecording();
    settingsStatus.clear();
  };
  if (viewButton("Bindings", hotkeysView == 0))
    pick(0);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Every binding, its keys, conflicts and Send.");
  ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
  if (viewButton("Placement", hotkeysView == 1))
    pick(1);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Which tab each mod's card is on, and whether it is a menu, a toggle or a hotkey.");
  if (hotkeysView == 0) {
    drawHotkeys();
    return;
  }
  const float footer = settingsStatus.empty() ? 0.0f : ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
  ImGui::BeginChild("PlacementView", ImVec2(0, -footer), ImGuiChildFlags_NavFlattened);
  drawPlacementSettings(actions, cardsStamp);
  ImGui::EndChild();
  if (!settingsStatus.empty())
    ImGui::TextDisabled("%s", settingsStatus.c_str());
}
