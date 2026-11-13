#pragma once

struct ModPlacement {
  std::string binding;
  int tab = 0;
};
struct ModLayout {
  std::map<int, std::string> tabs;
  std::vector<ModPlacement> placements;
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
// 1.3 kept its INIs in the ModActivator folder, which DMM never installs, so
// they move back beside the plugin; before 1.3 the log sat in bin64, and it
// moves into the folder. Runs before the log is opened, since the log is one
// of the files, so it returns what it did for the log instead of writing it.
static std::vector<std::string> relocateFiles() {
  std::vector<std::string> done;
  const auto move = [&](const std::string &from, const std::string &to) {
    return fileExists(from) && !fileExists(to) &&
           MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_COPY_ALLOWED);
  };
  for (const char *name : {"ModActivator.log.old", "ModActivator.log"})
    if (fileExists(configPath(name))) {
      ensureDataFolder();
      if (move(configPath(name), dataPath(name)))
        done.push_back(std::string("Moved ") + name + " into the " + dataFolderName + " folder.");
    }

  // Your choices. Should bin64 already hold a user file, that one is kept and
  // the folder's copy renamed, so neither is lost and this happens once.
  const auto user = userConfigPath(), oldUser = dataPath("ModActivator.User.ini");
  if (move(oldUser, user))
    done.push_back("Moved ModActivator.User.ini from the ModActivator folder back to bin64.");
  else if (fileExists(oldUser) &&
           MoveFileExA(oldUser.c_str(), (oldUser + ".moved").c_str(), MOVEFILE_REPLACE_EXISTING))
    done.push_back("Kept bin64\\ModActivator.User.ini and renamed the one in the ModActivator "
                   "folder ModActivator.User.ini.moved. It can be deleted.");

  // 1.3's copy of the known-mods list, which is built into the plugin now.
  const auto oldKnown = dataPath("ModActivator.KnownMods.ini");
  if (fileExists(oldKnown) && DeleteFileA(oldKnown.c_str()))
    done.push_back("Removed the old ModActivator.KnownMods.ini from the ModActivator folder.");

  // The settings file. Alone in the folder it is yours and moves as it is;
  // beside a fresh one in bin64, the options you set carry over into that
  // and the old one is renamed, so this happens once and later edits stand.
  const auto ini = configPath("ModActivator.ini"), oldIni = dataPath("ModActivator.ini");
  if (move(oldIni, ini)) {
    done.push_back("Moved ModActivator.ini from the ModActivator folder back to bin64.");
  } else if (fileExists(oldIni)) {
    const auto general = readProfileSection("General", oldIni);
    for (const char *line = general.data(); *line; line += strlen(line) + 1) {
      const std::string entry = line;
      const auto eq = entry.find('=');
      if (eq == entry.npos || !eq)
        continue;
      // 1.3 shipped WrapSwapChain=0, which was no choice of yours: auto,
      // the new default, stays.
      if (!_stricmp(trimConfigLine(entry.substr(0, eq)).c_str(), "WrapSwapChain") &&
          wrapValue(entry.c_str() + eq + 1) == 0)
        continue;
      WritePrivateProfileStringA("General", entry.substr(0, eq).c_str(),
                                 entry.substr(eq + 1).c_str(), ini.c_str());
    }
    if (MoveFileExA(oldIni.c_str(), (oldIni + ".moved").c_str(), MOVEFILE_REPLACE_EXISTING))
      done.push_back("Carried your [General] options from ModActivator\\ModActivator.ini into "
                     "bin64\\ModActivator.ini, and renamed the old one ModActivator.ini.moved. "
                     "It can be deleted.");
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
                             configPath("ModActivator.ini").c_str());
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
                             configPath("ModActivator.ini").c_str());
  const auto code = shortcutCode(parseShortcut(value));
  opener.store(code && (code & 0xff) != VK_ESCAPE ? code : defaultOpener);
  // The controller shortcut, the same way. None turns it off; a single button
  // is refused, so one press can never open the panel.
  char pad[128]{};
  GetPrivateProfileStringA("General", "PadOpenShortcut", "", pad, sizeof pad, path.c_str());
  if (!pad[0])
    GetPrivateProfileStringA("General", "PadOpenShortcut", "", pad, sizeof pad,
                             configPath("ModActivator.ini").c_str());
  const unsigned padBits = padButtonsFrom(pad);
  const bool off = !_stricmp(trimConfigLine(pad).c_str(), "none");
  padOpener.store(off ? 0 : __builtin_popcount(padBits) >= 2 ? padBits : defaultPadOpener);
  ml::State::Get().padShortcut.store(padOpener.load());
  loadRadial();
  const auto section = readProfileSection("ModLayout", path);
  ModLayout loaded;
  std::map<unsigned long, ModPlacement> ordered;
  for (const char *p = section.data(); *p; p += strlen(p) + 1) {
    const std::string line = p;
    const auto eq = line.find('=');
    if (eq < 2 || eq == line.npos)
      continue;
    const auto index = strtoul(line.c_str() + 1, nullptr, 10);
    if (line[0] == 'T' && index >= firstCustomTab && index < 65536) {
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

// The controller recording is for the radial menu's combination, not the
// panel's, and what it took.
static bool padRecordingRadial = false;
static unsigned radialPadProposed = 0;

// Settings' part for the radial menu: its keys, its eight slots in order,
// and what happens to the game while it is open.
static void drawRadialSettings(const std::vector<Action> &actions, std::string &status) {
  ImGui::TextColored(UI::gold, "Radial menu");
  ImGui::TextDisabled("Hold the key, aim with the mouse or the right stick, let go to open.");
  static std::vector<int> proposed;
  static bool acknowledge = false;
  const unsigned code = radialKeyCode();
  ImGui::Text("Hold key: %s", code ? shortcutLabel(codeKeys(code)).c_str() : "none");
  if (auto keys = UI::recordShortcut("Record radial key"); !keys.empty()) {
    proposed = std::move(keys);
    acknowledge = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Default##RadialKey")) {
    status = saveRadialKey("Grave") ? "Radial key reset to the key left of 1." : "Could not save the radial key.";
    proposed.clear();
  }
  ImGui::SameLine();
  if (ImGui::Button("Off##RadialKey")) {
    status = saveRadialKey("None") ? "Radial key off." : "Could not save the radial key.";
    proposed.clear();
  }
  if (!proposed.empty()) {
    ImGui::Text("New radial key: %s", shortcutLabel(proposed).c_str());
    bool clashes = conflict(proposed, openerKeys()) != 0;
    if (clashes)
      ImGui::TextWrapped("Overlaps ModActivator's own shortcut.");
    for (const auto &a : bindingsSnapshot())
      if (conflict(proposed, a.cachedKeys)) {
        clashes = true;
        ImGui::TextWrapped("Overlaps %s / %s", UI::name(a).c_str(), UI::actionName(a).c_str());
      }
    for (const auto &[severity, who] : reservedClashes(proposed)) {
      clashes = true;
      ImGui::TextWrapped("%s %s", severity == 2 ? "Already used by" : "Overlaps", displayText(who).c_str());
    }
    ImGui::TextDisabled("The game sees this key too: pick one it does not use.");
    if (clashes)
      ImGui::Checkbox("Save despite overlap##Radial", &acknowledge);
    ImGui::BeginDisabled((clashes && !acknowledge) || proposed.back() == VK_ESCAPE);
    if (ImGui::Button("Save radial key")) {
      status = saveRadialKey(keyCodesText(proposed)) ? "Radial key updated." : "Could not save the radial key.";
      proposed.clear();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Discard##RadialKey"))
      proposed.clear();
  }

  const unsigned pad = padRadial.load();
  ImGui::Text("Hold on the controller: %s", pad ? padLabel(pad).c_str() : "none");
  if (ImGui::Button(padShortcutRecording && padRecordingRadial ? "Press and release a combination...##RadialPad"
                                                               : "Record##RadialPad")) {
    const bool stop = padShortcutRecording && padRecordingRadial;
    padShortcutRecording = !stop;
    padRecordingRadial = !stop;
    radialPadProposed = 0;
  }
  ImGui::SameLine();
  if (ImGui::Button("Default##RadialPad"))
    radialPadProposed = defaultPadRadial;
  ImGui::SameLine();
  if (ImGui::Button("Off##RadialPad"))
    status = saveRadialPad(0) ? "Radial controller combination off." : "Could not save it.";
  if (radialPadProposed) {
    ImGui::Text("New combination: %s", padLabel(radialPadProposed).c_str());
    if (padConflict(radialPadProposed, padOpener.load()))
      ImGui::TextWrapped("Overlaps the panel's controller shortcut (%s).", padLabel(padOpener.load()).c_str());
    for (const auto &a : bindingsSnapshot())
      if (padConflict(radialPadProposed, a.padButtons))
        ImGui::TextWrapped("Overlaps %s / %s (%s)", UI::name(a).c_str(), UI::actionName(a).c_str(),
                           padLabel(a.padButtons).c_str());
    if (ImGui::Button("Save combination##RadialPad")) {
      status = saveRadialPad(radialPadProposed) ? "Radial controller combination updated." : "Could not save it.";
      radialPadProposed = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard##RadialPad"))
      radialPadProposed = 0;
  }

  int pause = radialPause.load();
  static const char *pauseLabels[] = {"Keeps running", "Slow motion", "Freezes"};
  ImGui::SetNextItemWidth(220 * UI::scale);
  if (ImGui::Combo("The game while the ring is open", &pause, pauseLabels, IM_ARRAYSIZE(pauseLabels))) {
    status = saveRadialPause(pause) ? "Saved." : "Could not save it.";
    if (pause)
      gameSpeed::prepare();
  }
  if (radialPause.load())
    ImGui::TextDisabled("Slowing and freezing: %s", gameSpeed::statusText());

  ImGui::Spacing();
  ImGui::TextUnformatted("On the ring, clockwise from the top");
  if (radialSlots.empty())
    ImGui::TextDisabled("Nothing yet. Use the ring button on a card in Mod menus or Toggles.");
  for (size_t i = 0; i < radialSlots.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    const auto &id = radialSlots[i];
    const auto found = std::find_if(actions.begin(), actions.end(),
                                    [&](const Action &a) { return cachedIdentity(a) == id; });
    const std::string label = found == actions.end() ? id + " (not loaded)"
                                                     : UI::name(*found) + " / " + UI::actionName(*found);
    ImGui::Text("%zu.", i + 1);
    ImGui::SameLine();
    ImGui::TextUnformatted(label.c_str());
    ImGui::SameLine();
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
}

// cardsStamp changes whenever the menu rebuilds `actions`.
static void drawSettings(const std::vector<Action> &actions, unsigned cardsStamp) {
  static char tabName[64]{};
  static int editingTab = 0;
  static std::string status;
  static std::vector<int> proposed;
  static bool acknowledge = false;
  ImGui::BeginChild("SettingsScroll", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
  ImGui::Text("ModActivator shortcut: %s", shortcutLabel(openerKeys()).c_str());
  if (auto keys = UI::recordShortcut("Record opener"); !keys.empty()) {
    proposed = std::move(keys);
    acknowledge = false;
    status.clear();
  }
  ImGui::SameLine();
  if (ImGui::Button("Use default")) {
    proposed = {VK_CONTROL, VK_SHIFT, VK_BACK};
    UI::recording = 0;
    acknowledge = false;
  }
  // The same for the controller: open and close the panel with a button
  // combination. Two buttons or more, so a single press never opens it.
  {
    static unsigned padProposed = 0;
    auto &padRecording = padShortcutRecording;
    const unsigned current = padOpener.load();
    ImGui::Text("Controller shortcut: %s", current ? padLabel(current).c_str() : "none");
    if (ImGui::Button(padRecording && !padRecordingRadial ? "Press and release a combination...##Pad"
                                                          : "Record controller shortcut")) {
      padRecording = !padRecording || padRecordingRadial;
      padRecordingRadial = false;
      padProposed = 0;
    }
    // One recording at a time, for the panel's combination or the radial
    // menu's (Settings further down).
    if (const unsigned taken = recordPadShortcut(padRecording)) {
      const bool enough = __builtin_popcount(taken) >= 2;
      status = enough ? "" : "Use two buttons or more, such as LB + RB + Back.";
      (padRecordingRadial ? radialPadProposed : padProposed) = enough ? taken : 0;
      padRecordingRadial = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Controller off")) {
      padRecording = false;
      padProposed = 0;
      status = savePadOpener(0) ? "Controller shortcut off." : "Could not save the controller shortcut.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Default##Pad")) {
      padRecording = false;
      padProposed = defaultPadOpener;
    }
    if (padProposed) {
      ImGui::Text("New controller shortcut: %s", padLabel(padProposed).c_str());
      for (const auto &a : bindingsSnapshot())
        if (padConflict(padProposed, a.padButtons))
          ImGui::TextWrapped("Overlaps %s / %s (%s)", UI::name(a).c_str(),
                             UI::actionName(a).c_str(), padLabel(a.padButtons).c_str());
      if (ImGui::Button("Save controller shortcut")) {
        status = savePadOpener(padProposed) ? "Controller shortcut updated."
                                            : "Could not save the controller shortcut.";
        padProposed = 0;
      }
      ImGui::SameLine();
      if (ImGui::Button("Discard##Pad"))
        padProposed = 0;
    }
    if (!ml::Settings::Get().hookXInput)
      ImGui::TextDisabled("HookXInput=0: the game also sees the buttons you press for it.");
    ImGui::Separator();
  }
  if (!proposed.empty()) {
    ImGui::Text("New shortcut: %s", shortcutLabel(proposed).c_str());
    bool clashes = false;
    for (const auto &a : bindingsSnapshot())
      if (conflict(proposed, a.cachedKeys)) {
        clashes = true;
        ImGui::TextWrapped("Overlaps %s / %s", UI::name(a).c_str(),
                           UI::actionName(a).c_str());
      }
    for (const auto &[severity, who] : reservedClashes(proposed)) {
      clashes = true;
      ImGui::TextWrapped("%s %s", severity == 2 ? "Already used by" : "Overlaps",
                         displayText(who).c_str());
    }
    if (clashes)
      ImGui::Checkbox("Save opener despite overlap", &acknowledge);
    ImGui::BeginDisabled(clashes && !acknowledge);
    if (ImGui::Button("Save opener")) {
      const bool saved = saveOpener(proposed);
      status = saved ? "Shortcut updated." : "Could not save the shortcut.";
      if (saved)
        proposed.clear();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Discard shortcut"))
      proposed.clear();
  }

  ImGui::Separator();
  drawRadialSettings(actions, status);
  ImGui::Separator();
  ImGui::TextUnformatted("Custom tabs");
  if (ImGui::BeginCombo("##TabToEdit", editingTab && modLayout.tabs.count(editingTab)
                                           ? modLayout.tabs.at(editingTab).c_str()
                                           : "New tab")) {
    if (ImGui::Selectable("New tab", !editingTab)) {
      editingTab = 0;
      tabName[0] = 0;
    }
    for (const auto &[id, name] : modLayout.tabs) {
      ImGui::PushID(id);
      if (ImGui::Selectable(name.c_str(), editingTab == id)) {
        editingTab = id;
        snprintf(tabName, sizeof tabName, "%s", name.c_str());
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  ImGui::InputText("Tab name", tabName, sizeof tabName);
  if (ImGui::Button(editingTab ? "Rename tab" : "Add tab")) {
    const auto name = trim(tabName);
    auto next = modLayout;
    bool used = name.empty() || name.find("##") != name.npos ||
                lower(name) == "mod menus" || lower(name) == "toggles" ||
                lower(name) == "hotkeys" || lower(name) == "settings";
    for (const auto &[id, existing] : next.tabs)
      used |= id != editingTab && lower(existing) == lower(name);
    if (used) {
      status = "Enter a unique tab name.";
    } else {
      int id = editingTab ? editingTab : firstCustomTab;
      while (!editingTab && next.tabs.count(id))
        ++id;
      next.tabs[id] = name;
      if (saveLayout(std::move(next))) {
        editingTab = id;
        status = "Tab saved.";
      } else {
        status = "Could not save the tab.";
      }
    }
  }
  if (editingTab) {
    ImGui::SameLine();
    if (ImGui::Button("Delete tab")) {
      auto next = modLayout;
      next.tabs.erase(editingTab);
      for (auto &place : next.placements)
        if (place.tab == editingTab)
          place.tab = 0;
      if (saveLayout(std::move(next))) {
        editingTab = 0;
        tabName[0] = 0;
        status = "Tab deleted. Its mods returned to their default tabs.";
      } else {
        status = "Could not delete the tab.";
      }
    }
  }
  if (!status.empty())
    ImGui::TextWrapped("%s", status.c_str());

  ImGui::Separator();
  ImGui::TextUnformatted("Mod placement");
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
  if (ImGui::BeginTable("Placement", 4, ImGuiTableFlags_RowBg |
                                           ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch, 1.1f);
    ImGui::TableSetupColumn("What it does", ImGuiTableColumnFlags_WidthStretch, 1.5f);
    ImGui::TableSetupColumn("Tab");
    ImGui::TableSetupColumn("Show as");
    ImGui::TableHeadersRow();
    const char *types[] = {"Automatic", "Menu", "Toggle", "Hotkeys only"};
    UI::groupedBindings(bindings, [&](const Action &action) {
      const auto &binding = cachedIdentity(action);
      ImGui::PushID(binding.c_str());
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      const auto name = UI::actionName(action);
      ImGui::TextUnformatted(name.c_str());
      if (ImGui::IsItemHovered())
        UI::bindingTooltip(action);
      ImGui::TableSetColumnIndex(1);
      UI::descriptionCell(action);
      ImGui::TableSetColumnIndex(2);
      if (!visible.count(binding)) {
        ImGui::TextDisabled("No card");
      } else {
        const int current = assignedTab(binding);
        const std::string automatic = action.toggle ? "Automatic (Toggles)"
                                                    : "Automatic (Mod menus)";
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
            status = saveLayout(std::move(next)) ? "Placement saved."
                                                  : "Could not save placement.";
          }
        }
      }
      ImGui::TableSetColumnIndex(3);
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
  ImGui::EndChild();
}
