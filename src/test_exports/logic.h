// Test entry points for the logic: discovery, the catalog, settings and
// migration, the editor, controller bindings, descriptions, conflicts and
// responsiveness. Called by tests/ through build/ModActivator-tests.asi only.
//
// Part of activator.cpp, included at its end in the order listed
// there. Not a header of its own: it relies on the parts before it.

extern "C" __declspec(dllexport) int ModActivatorReliabilityTest() {
  if (!folder.empty())
    return -1;
  Action a{"Fixture", "Fixture.asi", "absent.ini", "Menu", "Hotkey"};
  if (validShortcut(keysFrom(a, "F4+F5")))
    return 1;
  if (validShortcut(keysFrom(a, "Ctrl+Ctrl+F5")))
    return 2;
  if (validShortcut(keysFrom(a, "F5+Ctrl")))
    return 3;
  if (keysFrom(a, "Ctrl + 7") != std::vector<int>({17, 55}))
    return 4;
  a.file = "reshade.ini";
  if (keysFrom(a, "36,1,0,0") != std::vector<int>({17, 36}))
    return 5;
  for (auto value : {"36,1", "36,2,0,0", "36,0,0,0junk", "36,0,0,0,1"})
    if (validShortcut(keysFrom(a, value)))
      return 6;
  a.module = "RestartFixture.asi";
  {
    std::lock_guard<std::mutex> lock(catalogMutex);
    restartRequired.insert(lower(a.module));
    pendingActions.clear();
  }
  queueAction(a);
  if (!pendingActions.empty())
    return 7;
  {
    std::lock_guard<std::mutex> lock(catalogMutex);
    restartRequired.erase(lower(a.module));
  }
  Action next = a;
  next.label = "Second shortcut";
  if (!queueAction(a) || !queueAction(next) || pendingActions.size() != 2 ||
      pendingActions.front().label != a.label || pendingActions.back().label != next.label)
    return 8;
  pendingActions.clear();
  return 0;
}
extern "C" __declspec(dllexport) int
ModActivatorSettingsTest(const char *root) {
  if (!folder.empty() || !root)
    return -1;
  folder = root;
  const int result = [] {
    const int count = GetKeyboardLayoutList(0, nullptr);
    std::vector<HKL> layouts(count);
    GetKeyboardLayoutList(count, layouts.data());
    Action probe{"Keyboard", "Keyboard.asi", "absent.ini", "Menu", "Hotkey"};
    const std::string umlaut = "\xc3\xb6";
    if (usSymbolForScan(scanForLocalizedSymbol(umlaut, gameKeyboardLayout())) != ';' ||
        usSymbolForScan(scanForLocalizedSymbol("\xc3\xb1", gameKeyboardLayout())) != ';' ||
        usSymbolForScan(scanForUsSymbol(';')) != ';' ||
        usSymbolForScan(scanForUsSymbol(':')) != ';')
      return 1;
    for (auto layout : layouts) {
      const auto language = LOWORD(reinterpret_cast<ULONG_PTR>(layout));
      unsigned scan = 0;
      if (language == 0x0407) {
        const auto key = vk("]", layout);
        const auto semicolon = keysFrom(probe, "Ctrl+; ; note", layout, &scan);
        if (key != VK_OEM_PLUS || localizedKey(key, layout) != "+" ||
            vk("Y", layout) != 'Z' || vk("Z", layout) != 'Y' ||
            vk("0x59", layout) != 'Y' || scan != 0x27 ||
            semicolon.size() != 2 || localizedKey(semicolon.back(), layout) != "\xc3\x96" ||
            keysFrom(probe, umlaut, layout) != std::vector<int>{semicolon.back()} ||
            keysFrom(probe, "Ctrl+7", layout) != std::vector<int>{VK_CONTROL, '7'})
          return 1;
      }
      if (language == 0x0409 && vk(umlaut, layout) != VK_OEM_1)
        return 1;
    }

    Action first{"First", "First.asi", "First.ini", "Menu", "Hotkey"};
    Action second{"Second", "Second.asi", "Second.ini", "Menu", "Hotkey"};
    Action third{"Third", "Third.asi", "Third.ini", "Menu", "Hotkey"};
    if (encodeBinding(first, "Ctrl+Shift+F6") != "Ctrl+Shift+F6" ||
        encodeBinding(first, umlaut) != ";" ||
        replaceValue("[Menu]\nHotkey=; ; comment\n", first, "F8") !=
            "[Menu]\nHotkey=F8 ; comment\n")
      return 2;
    if (encodeBinding(second, "F6") != "0x75" ||
        encodeBinding(second, "", {VK_CONTROL, VK_F6}) != "0x75")
      return 3;

    static std::set<int> down;
    const auto isHeld = [](int key) { return down.count(key) != 0; };
    constexpr ImGuiID field = 1;
    UI::beginShortcutCapture(field);
    down = {VK_CONTROL};
    if (!UI::captureShortcut(field, isHeld).empty())
      return 11;
    down = {VK_CONTROL, VK_SHIFT, VK_MENU};
    if (!UI::captureShortcut(field, isHeld).empty())
      return 12;
    down.insert(VK_F6);
    const auto captured = UI::captureShortcut(field, isHeld);
    if (captured != std::vector<int>{VK_CONTROL, VK_SHIFT, VK_MENU, VK_F6} ||
        encodeBinding(first, "", captured) != "Ctrl+Shift+Alt+F6" || UI::recording)
      return 13;
    UI::beginShortcutCapture(field);
    down = {VK_ESCAPE};
    if (!UI::captureShortcut(field, isHeld).empty() || UI::recording)
      return 14;

    Action stance{"Combat Evolved", "CombatEvolved.asi", "CombatEvolved.ini",
                  "Controls", "StanceCycleKey"};
    Action wiki{"CD Wiki", "CDWiki.asi", "CDWiki.ini", "Input", "ToggleKey"};
    BindingComments notes;
    const auto descriptionConfig = folder + "Descriptions.ini";
    {
      std::ofstream file(descriptionConfig);
      file << "[Controls]\n; Switch between unarmed stances.\n"
              "; Grave is the default key.\nStanceCycleKey=Grave\n"
              "[Input]\nToggleKey=F8 ; Open or close the wiki.\n"
              "AnotherKey=F7\n";
    }
    notes = readBindingComments(descriptionConfig);
    if (notes.count({"input", "anotherkey"}))
      return 18;
    for (int type : {0, 1, 2, 3}) {
      if (!saveBindingType(stance, type) || !saveBindingType(wiki, type))
        return 15;
      classifyBinding(stance, notes);
      classifyBinding(wiki, notes);
      if (UI::actionName(stance) != "Stance Cycle" || UI::actionName(wiki) != "Toggle" ||
          stance.description != "Switch between unarmed stances.\nGrave is the default key." ||
          wiki.description != "Open or close the wiki." || stance.typeOverride != type ||
          wiki.typeOverride != type)
        return 16;
    }
    Action effect{"Weather", "Weather.addon64", "Weather.ini", "General",
                  "HotkeyToggleEffect"};
    if (UI::actionName(effect) != "Toggle Effect")
      return 17;

    ModLayout test;
    test.tabs[firstCustomTab] = "Test tab";
    placementFor(test, identity(second)).tab = firstCustomTab;
    placementFor(test, identity(first)).tab = firstCustomTab;
    if (!saveLayout(std::move(test)) || !saveOpener({VK_CONTROL, VK_BACK}))
      return 4;
    opener.store(defaultOpener);
    modLayout = {};
    ++layoutVersion;
    loadLayout();
    if (openerKeys() != std::vector<int>{VK_CONTROL, VK_BACK} ||
        modLayout.tabs[firstCustomTab] != "Test tab" ||
        actionTab(first) != firstCustomTab || actionTab(third) != 0)
      return 5;
    std::vector<Action> actions{first, second, third};
    orderActions(actions);
    if (actions.front().module != second.module || actions.back().module != third.module)
      return 6;

    auto moved = modLayout;
    placementFor(moved, identity(third)).tab = togglesPlacement;
    Action toggle{"Toggle", "Toggle.asi", "Toggle.ini", "Menu", "Hotkey", true};
    placementFor(moved, identity(toggle)).tab = modMenusPlacement;
    if (!saveLayout(std::move(moved)))
      return 7;
    modLayout = {};
    ++layoutVersion;
    loadLayout();
    if (actionTab(third) != 1 || third.toggle || actionTab(toggle) != 0 || !toggle.toggle)
      return 9;

    if (!saveLayout({}))
      return 10;
    loadLayout();
    if (!modLayout.tabs.empty() || !modLayout.placements.empty())
      return 8;
    return 0;
  }();
  folder.clear();
  opener.store(defaultOpener);
  modLayout = {};
  ++layoutVersion;
  refreshRequested.store(false);
  return result;
}
// The move of the menu's choices out of ModActivator.ini, and of 1.3's INIs
// out of the ModActivator folder back to bin64. root is an empty folder; each
// case writes its own ModActivator.ini.
extern "C" __declspec(dllexport) int ModActivatorMigrationTest(const char *root) {
  if (!folder.empty() || !root)
    return -1;
  folder = root;
  ensureDataFolder();
  const auto old = configPath("ModActivator.ini"), user = userConfigPath();
  const auto exists = [](const std::string &p) {
    return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES;
  };
  const Action probe{"x", "Mod.asi", "Mod.ini", "Menu", "Hotkey"};
  const int result = [&] {
    // A fresh install: only the shipped default, spelled in words. Nothing
    // is yours, so no user file appears.
    DeleteFileA(user.c_str());
    std::ofstream(old) << "[General]\nOpenShortcut=Ctrl+Shift+Backspace\n";
    migrateUserConfig(old);
    if (exists(user))
      return 1;
    loadLayout();
    if (openerKeys() != std::vector<int>{VK_CONTROL, VK_SHIFT, VK_BACK})
      return 2;
    // An older version's choices come over: the recorded opener, the binding
    // types and the layout.
    std::ofstream(old) << "[General]\nOpenShortcut=0x11+0x70\nWrapSwapChain=0\n"
                          "[BindingTypes]\nmod.ini|menu|hotkey=2\n"
                          "[ModLayout]\nT3=" + hexText("Mine") + "\nM0=3:" + hexText(identity(probe)) + "\n";
    migrateUserConfig(old);
    if (!exists(user))
      return 3;
    loadLayout();
    if (openerKeys() != std::vector<int>{VK_CONTROL, VK_F1} || readBindingType(probe) != 2 ||
        modLayout.tabs[3] != "Mine" || actionTab(probe) != 3)
      return 4;
    // Once there is a user file, an update's fresh ModActivator.ini changes
    // nothing, and the next start does not copy again.
    std::ofstream(old) << "[General]\nOpenShortcut=Ctrl+Shift+Backspace\n"
                          "[BindingTypes]\nmod.ini|menu|hotkey=1\n";
    migrateUserConfig(old);
    loadLayout();
    if (openerKeys() != std::vector<int>{VK_CONTROL, VK_F1} || readBindingType(probe) != 2 ||
        modLayout.tabs[3] != "Mine")
      return 5;
    // What the menu saves goes to the user file, never to ModActivator.ini.
    if (!saveOpener({VK_SHIFT, VK_F2}) || !saveBindingType(probe, 3) || !saveLayout({}))
      return 6;
    char shipped[64]{};
    GetPrivateProfileStringA("General", "OpenShortcut", "", shipped, sizeof shipped, old.c_str());
    if (std::string(shipped) != "Ctrl+Shift+Backspace" ||
        GetPrivateProfileIntA("BindingTypes", identity(probe).c_str(), 0, old.c_str()) != 1 ||
        readBindingType(probe) != 3)
      return 7;

    // A 1.3 install: the INIs in the ModActivator folder, where DMM never put
    // them, and a log from before 1.3 still in bin64. All of it moves.
    for (const char *name : {"ModActivator.ini", "ModActivator.User.ini"})
      DeleteFileA(configPath(name).c_str());
    const auto folderIni = dataPath("ModActivator.ini"), folderUser = dataPath("ModActivator.User.ini"),
               folderKnown = dataPath("ModActivator.KnownMods.ini"),
               known = configPath("ModActivator.KnownMods.ini");
    std::ofstream(folderIni) << "[General]\nHookXInput=0\n";
    std::ofstream(folderUser) << "[BindingTypes]\nmod.ini|menu|hotkey=2\n";
    std::ofstream(folderKnown) << "[mod.ini]\n";
    std::ofstream(folder + "ModActivator.log") << "old log\n";
    auto moved = relocateFiles();
    if (exists(folderIni) || exists(folderUser) || exists(folderKnown) || exists(known) ||
        exists(folder + "ModActivator.log") || !exists(dataPath("ModActivator.log")) ||
        GetPrivateProfileIntA("General", "HookXInput", 1, old.c_str()) != 0 ||
        readBindingType(probe) != 2 || moved.size() != 4)
      return 8;
    // An update brought fresh INIs into bin64 while the folder still has the
    // 1.3 ones: your options carry over, the shipped comments stay, your
    // bin64 choices win, and the old files are renamed or removed, once.
    std::ofstream(old) << "[General]\n; shipped comment\nHookXInput=1\nUseReShadeEvents=1\n";
    std::ofstream(folderIni) << "[General]\nHookXInput=0\nWrapSwapChain = 0 ; the old default\n";
    std::ofstream(folderUser) << "[BindingTypes]\nmod.ini|menu|hotkey=1\n";
    std::ofstream(folderKnown) << "[old.ini]\n";
    relocateFiles();
    std::ifstream fresh(old);
    const std::string text(std::istreambuf_iterator<char>(fresh), {});
    fresh.close();
    if (exists(folderIni) || !exists(folderIni + ".moved") || exists(folderUser) ||
        !exists(folderUser + ".moved") || exists(folderKnown) || readBindingType(probe) != 2 ||
        GetPrivateProfileIntA("General", "HookXInput", 1, old.c_str()) != 0 ||
        GetPrivateProfileIntA("General", "UseReShadeEvents", 0, old.c_str()) != 1 ||
        text.find("; shipped comment") == text.npos || wrapSetting(old) != -1)
      return 9;
    // WrapSwapChain: auto unless it says 1 or 0.
    for (const auto &[value, want] : std::vector<std::pair<const char *, int>>{
             {"1", 1}, {"0", 0}, {"auto", -1}, {"Auto", -1}, {"", -1}, {nullptr, -1},
             {"1 ; for frame generation", 1}, {"0 ; it crashed", 0}, {"2", 1}, {"true", 1},
             {"Off", 0}, {"maybe", -1}}) {
      WritePrivateProfileStringA("General", "WrapSwapChain", value, old.c_str());
      if (wrapSetting(old) != want)
        return 11;
    }
    WritePrivateProfileStringA("General", "HookXInput", "1", old.c_str());
    if (!relocateFiles().empty() ||
        GetPrivateProfileIntA("General", "HookXInput", 0, old.c_str()) != 1)
      return 10;
    return 0;
  }();
  folder.clear();
  opener.store(defaultOpener);
  modLayout = {};
  ++layoutVersion;
  refreshRequested.store(false);
  return result;
}
extern "C" __declspec(dllexport) int ModActivatorSelfTest() {
  if (sizeof(INPUT) != 40 || vk("F8") != 119 || vk("0x78") != 120 ||
      vk("DELETE") != 46 || vk("PageUp") != 33 || vk("None") != 0)
    return 1;
  return 0;
}
// Scans root for the given ';'-separated modules and writes one line per
// selected binding: module|file|section|key|vk,vk,...
static int discoveryReport(const char *root, const char *modules, char *out, int capacity,
                           bool toggles) {
  if (!folder.empty() || !root || !modules || !out || capacity < 1)
    return -1;
  folder = root;
  std::vector<std::string> names;
  std::string list = modules;
  size_t pos = 0;
  while (pos < list.size()) {
    auto end = list.find(';', pos);
    names.push_back(list.substr(pos, end == list.npos ? end : end - pos));
    if (end == list.npos)
      break;
    pos = end + 1;
  }
  std::string report;
  try {
    for (auto &a : selectActions(scanBindings(names), toggles)) {
      report += a.module + "|" + a.file + "|" + a.section + "|" + a.key + "|";
      for (int k : a.cachedKeys)
        report += std::to_string(k) + ",";
      report += "\n";
    }
  } catch (...) {
    folder.clear();
    return -2;
  }
  folder.clear();
  if ((int)report.size() >= capacity)
    return -3;
  memcpy(out, report.c_str(), report.size() + 1);
  return (int)report.size();
}
extern "C" __declspec(dllexport) int
ModActivatorDiscoveryTest(const char *root, const char *modules, char *out, int capacity) {
  return discoveryReport(root, modules, out, capacity, false);
}
extern "C" __declspec(dllexport) int
ModActivatorToggleDiscoveryTest(const char *root, const char *modules, char *out, int capacity) {
  return discoveryReport(root, modules, out, capacity, true);
}
// Every binding found, with what the description sources made of it:
// file|section|key|vk,..|name|mod name|source|menu rank|feature rank|description
// (newlines in descriptions written as \n).
extern "C" __declspec(dllexport) int
ModActivatorDescribeTest(const char *root, const char *modules, char *out, int capacity) {
  if (!folder.empty() || !root || !modules || !out || capacity < 1)
    return -1;
  folder = root;
  std::vector<std::string> names;
  std::stringstream list(modules);
  for (std::string name; std::getline(list, name, ';');)
    names.push_back(name);
  std::string report;
  try {
    for (const auto &a : scanBindings(names)) {
      auto description = a.description;
      for (size_t at; (at = description.find('\n')) != description.npos;)
        description.replace(at, 1, "\\n");
      report += a.file + "|" + a.section + "|" + a.key + "|";
      for (int k : a.cachedKeys)
        report += std::to_string(k) + ",";
      report += "|" + UI::actionName(a) + "|" + UI::name(a) + "|" +
                std::to_string(a.descriptionSource) + "|" + std::to_string(a.menuRank) + "|" +
                std::to_string(a.featureRank) + "|" + description + "\n";
    }
    // A mod that writes key codes in hex gets them written back in hex.
    Action hex{"Hex", "Hex.asi", "Hex.ini", "Hotkeys", "PrivateHotkey"};
    hex.hexNumbers = true;
    if (keysFrom(hex, "73") != std::vector<int>{0x73} || keysFrom(hex, "7A") != std::vector<int>{0x7A} ||
        keysFrom(hex, "A") != std::vector<int>{'A'}) {
      folder.clear();
      return -4;
    }
    // BA is the ; key's code, and F6 is still the F6 key.
    if (keysFrom(hex, "BA") != std::vector<int>{0xBA} || keysFrom(hex, "f6") != std::vector<int>{VK_F6}) {
      folder.clear();
      return -7;
    }
    std::ofstream(folder + "Hex.ini") << "[Hotkeys]\nPrivateHotkey=73\n";
    if (encodeBinding(hex, "F6") != "75" || encodeBinding(hex, "", {VK_F7}) != "76" ||
        encodeBinding(hex, "", {VK_OEM_1}) != "BA") {
      folder.clear();
      return -5;
    }
    // A modifier the mod keeps in a flag of its own: recording Ctrl+F9 saves
    // the plain key code and leaves the flag to supply the Ctrl.
    Action compass{"Compass", "Compass.asi", "Compass.ini", "Menu", "HotkeyVk"};
    compass.modifierRule = "Ctrl?HotkeyCtrl";
    std::ofstream(folder + "Compass.ini") << "[Menu]\nHotkeyCtrl=1\nHotkeyVk=118\n";
    if (keysFrom(compass, "118") != std::vector<int>{VK_CONTROL, VK_F7} ||
        encodeBinding(compass, "", {VK_CONTROL, VK_F9}) != "120") {
      folder.clear();
      return -6;
    }
  } catch (...) {
    folder.clear();
    return -2;
  }
  folder.clear();
  if ((int)report.size() >= capacity)
    return -3;
  memcpy(out, report.c_str(), report.size() + 1);
  return (int)report.size();
}
// What the menu is built from, for the given modules under root: the cards
//   card|file|section|key
// and every binding with its conflicts and controller buttons
//   binding|file|section|key|severity|who (joined with ;)|controller label
extern "C" __declspec(dllexport) int
ModActivatorCatalogTest(const char *root, const char *modules, char *out, int capacity) {
  if (!folder.empty() || !root || !modules || !out || capacity < 1)
    return -1;
  folder = root;
  std::vector<std::string> names;
  std::stringstream list(modules);
  for (std::string name; std::getline(list, name, ';');)
    names.push_back(name);
  std::string report;
  try {
    auto all = scanBindings(names);
    for (const auto &a : chooseCards(all))
      report += "card|" + a.file + "|" + a.section + "|" + a.key + "\n";
    markConflicts(all, loadReservedKeys());
    for (const auto &a : all) {
      auto who = a.overlapNames;
      std::replace(who.begin(), who.end(), '\n', ';');
      report += "binding|" + a.file + "|" + a.section + "|" + a.key + "|" +
                std::to_string(a.severity) + "|" + who + "|" + padLabel(a.padButtons) + "\n";
    }
  } catch (...) {
    folder.clear();
    return -2;
  }
  folder.clear();
  if ((int)report.size() >= capacity)
    return -3;
  memcpy(out, report.c_str(), report.size() + 1);
  return (int)report.size();
}

// Controller spellings, the reload key lookup and the panel's controller
// shortcut. root is an empty folder.
extern "C" __declspec(dllexport) int ModActivatorControllerTest(const char *root) {
  if (!folder.empty() || !root)
    return -1;
  constexpr unsigned lb = XINPUT_GAMEPAD_LEFT_SHOULDER, rb = XINPUT_GAMEPAD_RIGHT_SHOULDER;
  struct Case {
    const char *text;
    unsigned bits;
  };
  const Case cases[] = {
      {"LeftStick,RightStick,A", XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB | XINPUT_GAMEPAD_A},
      {"BACK+LS", XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_LEFT_THUMB},
      {"L1+Down", lb | XINPUT_GAMEPAD_DPAD_DOWN},
      {"dpad_down+a", XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_A},
      {"Cross", XINPUT_GAMEPAD_A},
      {"LT + RB", padLeftTrigger | rb},
      {"0040", XINPUT_GAMEPAD_LEFT_THUMB},  // XInput bits, four hex digits
      {"0x0001", XINPUT_GAMEPAD_DPAD_UP},
      {"32768", XINPUT_GAMEPAD_Y},          // decimal
      {"1", 0},                             // an on/off flag, not a button
      {"0", 0}, {"None", 0}, {"", 0},
      {"A+Banana", 0},                      // one unknown word: none of it
  };
  for (const auto &c : cases)
    if (padButtonsFrom(c.text) != c.bits)
      return 1;
  if (padLabel(lb | rb | XINPUT_GAMEPAD_BACK) != "LB + RB + Back" ||
      padButtonsFrom(padLabel(lb | rb | XINPUT_GAMEPAD_BACK)) != (lb | rb | XINPUT_GAMEPAD_BACK))
    return 2;
  if (padConflict(lb | XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_A) != 1 ||
      padConflict(XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_A) != 2 ||
      padConflict(XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B) != 0 || padConflict(0, XINPUT_GAMEPAD_A))
    return 3;

  // The reload key a mod names, in the same config file.
  Action edited{"x", "Mod.asi", "Mod.ini", "Hotkeys", "PrivateHotkey"};
  edited.reload = "ReloadKey";
  const std::vector<Action> bindings{
      {"other", "Other.asi", "Other.ini", "Hotkeys", "ReloadKey"},
      {"reload", "Mod.asi", "Mod.ini", "Hotkeys", "ReloadKey"}};
  const Action *reloader = reloadBinding(edited, bindings);
  if (!reloader || reloader->label != "reload")
    return 4;
  edited.reload = "Other/ReloadKey";
  if (reloadBinding(edited, bindings))
    return 5;
  edited.reload = "live";
  if (reloadBinding(edited, bindings))
    return 6;

  // The panel's controller shortcut: yours, then ModActivator.ini's, then the
  // default; None turns it off, and a single button is not accepted.
  folder = root;
  const int result = [&] {
    ensureDataFolder();
    const auto user = userConfigPath(), shipped = configPath("ModActivator.ini");
    const auto loaded = [&](const char *mine, const char *theirs) {
      WritePrivateProfileStringA("General", "PadOpenShortcut", mine, user.c_str());
      WritePrivateProfileStringA("General", "PadOpenShortcut", theirs, shipped.c_str());
      loadLayout();
      return padOpener.load();
    };
    if (loaded(nullptr, nullptr) != defaultPadOpener)
      return 7;
    if (loaded(nullptr, "Back+Start") != (XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_START))
      return 8;
    if (loaded("LB+Y", "Back+Start") != (lb | XINPUT_GAMEPAD_Y))
      return 9;
    if (loaded("None", nullptr) != 0 || ml::State::Get().padShortcut.load() != 0)
      return 10;
    if (loaded("A", nullptr) != defaultPadOpener)
      return 11;
    // What Settings saves goes to your file, not ModActivator.ini, and loads.
    WritePrivateProfileStringA("General", "PadOpenShortcut", "Back+Start", shipped.c_str());
    if (!savePadOpener(lb | XINPUT_GAMEPAD_X))
      return 12;
    padOpener.store(0);
    loadLayout();
    char kept[64]{};
    GetPrivateProfileStringA("General", "PadOpenShortcut", "", kept, sizeof kept, shipped.c_str());
    if (padOpener.load() != (lb | XINPUT_GAMEPAD_X) || std::string(kept) != "Back+Start" ||
        ml::State::Get().padShortcut.load() != (lb | XINPUT_GAMEPAD_X))
      return 13;
    return 0;
  }();
  folder.clear();
  padOpener.store(defaultPadOpener);
  ml::State::Get().padShortcut.store(0);
  opener.store(defaultOpener);
  modLayout = {};
  ++layoutVersion;
  refreshRequested.store(false);
  return result;
}

// The test build describes one of its own fixture bindings, so the mod
// export is read end to end: tests/test_descriptions.py puts a config named
// after this DLL beside the fixtures.
// With ModActivatorBadExportTest(1), it answers like a broken mod instead:
// one good entry, then strings and an entry that point at nothing.
static bool badExport = false;
extern "C" __declspec(dllexport) const ModActivatorBindingV1 *ModActivatorBindingsV1(unsigned *count) {
  static const ModActivatorBindingV1 bindings[] = {
      {"ModActivator-tests.ini", "Hotkeys", "SecretKey", "Exported name",
       "Described by the mod itself.", ModActivatorKindToggle},
      {nullptr, nullptr, "", "ignored", "an entry without a key is skipped", ModActivatorKindMenu},
  };
  static const ModActivatorBindingV1 broken[] = {
      {"ModActivator-tests.ini", "Hotkeys", "SecretKey", "Exported name",
       "Described by the mod itself.", ModActivatorKindToggle},
      {nullptr, nullptr, "OtherKey", reinterpret_cast<const char *>(0x10), "bad name",
       ModActivatorKindMenu},
  };
  if (badExport) {
    *count = 1000; // more entries than the array holds: the rest is not there
    return broken;
  }
  *count = sizeof bindings / sizeof bindings[0];
  return bindings;
}
extern "C" __declspec(dllexport) void ModActivatorBadExportTest(int bad) { badExport = bad != 0; }

// Reading another module's memory: strings that run into a page that cannot
// be read, and pointers to nothing, are refused instead of read.
extern "C" __declspec(dllexport) int ModActivatorForeignReadTest() {
  SYSTEM_INFO system{};
  GetSystemInfo(&system);
  const size_t page = system.dwPageSize;
  auto *memory = static_cast<char *>(VirtualAlloc(nullptr, page * 2, MEM_COMMIT | MEM_RESERVE,
                                                  PAGE_READWRITE));
  if (!memory)
    return -1;
  DWORD old = 0;
  VirtualProtect(memory + page, page, PAGE_NOACCESS, &old);
  char *unterminated = memory + page - 3; // "abc" and then the no-access page
  memcpy(unterminated, "abc", 3);
  char *terminated = memory + page - 4;
  terminated[0] = 'x';
  char target[64];
  int result = 0;
  if (copyForeign(unterminated, target, sizeof target))
    result = 1;
  else if (copyForeign(reinterpret_cast<const char *>(0x10), target, sizeof target))
    result = 2;
  else if (!copyForeign(nullptr, target, sizeof target) || target[0])
    result = 3;
  else if (!copyForeign("fine", target, sizeof target) || strcmp(target, "fine"))
    result = 4;
  else if (!copyForeign("truncated", target, 4) || strcmp(target, "tru"))
    result = 5;
  ModActivatorBindingV1 entry{};
  if (!result && readForeignEntry(reinterpret_cast<const ModActivatorBindingV1 *>(
                                      memory + page - sizeof entry / 2),
                                  &entry))
    result = 6;
  if (!result && !readableRange(memory, page))
    result = 7;
  VirtualFree(memory, 0, MEM_RELEASE);
  return result;
}
extern "C" __declspec(dllexport) int
ModActivatorEditorTest(const char *root, const char *file, const char *sec,
                       const char *field, const char *input,
                       const char *expected, char *error, int size) {
  if (!folder.empty())
    return -1;
  folder = root;
  try {
    Action a{"test", "Fixture.asi", file, sec, field};
    auto value = encodeBinding(a, input);
    // A null expected is the file as it is now. A C string cannot carry
    // contents with zero bytes in them, such as a UTF-16 config.
    saveBinding(a, expected ? std::string(expected) : readBytes(folder + file), value);
    folder.clear();
    return 0;
  } catch (const std::exception &e) {
    folder.clear();
    if (size > 0)
      snprintf(error, size, "%s", e.what());
    return 1;
  }
}
// The known-mods list built into the plugin, and a file beside it that only
// adds: config files the built-in list lacks, and reserved keys. root is an
// empty folder.
extern "C" __declspec(dllexport) int ModActivatorKnownModsTest(const char *root) {
  if (!folder.empty() || !root)
    return -1;
  folder = root;
  const auto has = [](const KnownMods &mods, const char *file) {
    return std::find_if(mods.begin(), mods.end(),
                        [&](const KnownMod &m) { return m.file == file; });
  };
  const auto reserved = [](const KnownMods &mods, const char *label) {
    for (const auto &m : mods)
      for (const auto &[keys, who] : m.reserved)
        if (who == label)
          return true;
    return false;
  };
  const int result = [&] {
    const auto path = configPath("ModActivator.KnownMods.ini");
    DeleteFileA(path.c_str());
    // No file at all, as DMM installs it: the built-in list is all there.
    auto mods = loadKnownMods();
    auto route = has(mods, "crimsonroute.ini");
    if (route == mods.end() || route->name != "Crimson Route" || !reserved(mods, "Steam: screenshot (default key)"))
      return 1;
    const auto count = mods.size();
    // An older copy cannot replace a built-in entry; a new config file and a
    // reserved key are added.
    std::ofstream(path) << "[CrimsonRoute.ini]\n@mod = Stale name | old\n"
                           "[TryOut.ini]\n@mod = Try Out | an entry being tried\nHotkey = menu | Menu | Opens it.\n"
                           "[@reserved]\nF9 = Mine: overlay\n";
    mods = loadKnownMods();
    route = has(mods, "crimsonroute.ini");
    const auto tryOut = has(mods, "tryout.ini");
    if (route == mods.end() || route->name != "Crimson Route" || tryOut == mods.end() ||
        tryOut->name != "Try Out" || tryOut->bindings.size() != 1 || mods.size() != count + 1 ||
        !reserved(mods, "Mine: overlay") || !reserved(mods, "Steam: screenshot (default key)"))
      return 2;
    DeleteFileA(path.c_str());
    return 0;
  }();
  folder.clear();
  return result;
}
extern "C" __declspec(dllexport) int ModActivatorConflictTest() {
  return conflict({17, 119}, {119, 17}) == 2 &&
                 conflict({17, 119}, {16, 119}) == 1 &&
                 conflict({119}, {120}) == 0
             ? 0
             : 1;
}

extern "C" __declspec(dllexport) int ModActivatorResponsivenessTest() {
  if (!folder.empty())
    return -1;
  auto &s = ml::State::Get();
  s.menuOpen = true;
  Action toggle{"Toggle", "", "", "", "", true};
  queueAction(toggle);
  if (!s.menuOpen)
    return 1;
  Action ui{"UI", "", "", "", "", false};
  queueAction(ui);
  if (s.menuOpen)
    return 2;
  pendingActions.clear();
  std::vector<Action> old;
  {
    std::lock_guard<std::mutex> lock(catalogMutex);
    old = allBindings;
    allBindings.clear();
    for (int i = 0; i < 100; ++i) {
      Action a{"Fixture " + std::to_string(i), "Fixture.asi", "never-read.ini",
               "Menu", "Hotkey"};
      a.cachedValue = "F8";
      a.cachedKeys = {119};
      allBindings.push_back(a);
    }
  }
  catalogChanged();
  ++layoutVersion;
  auto *context = ImGui::CreateContext();
  auto &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(1280, 720);
  io.DeltaTime = 1.0f / 60;
  io.IniFilename = nullptr;
  unsigned char *pixels;
  int w, h;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
  configReads.store(0);
  for (int n = 0; n < 120; ++n) {
    ImGui::NewFrame();
    ImGui::Begin("Test");
    drawHotkeys();
    ImGui::End();
    ImGui::Render();
  }
  auto reads = configReads.load();
  ImGui::DestroyContext(context);
  {
    std::lock_guard<std::mutex> lock(catalogMutex);
    allBindings = std::move(old);
  }
  catalogChanged();
  ++layoutVersion;
  return reads ? 3 : 0;
}
