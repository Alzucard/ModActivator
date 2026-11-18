// Test entry points for the menu itself: search, scrolling, dragging cards,
// groups and text.
//
// Part of activator.cpp, included at its end in the order listed
// there. Not a header of its own: it relies on the parts before it.

extern "C" __declspec(dllexport) int ModActivatorUISearchTest(){if(!folder.empty())return -1;Action a{"BodyPhysics","BodyPhysics.asi","","",""};a.cachedKeys={115};a.cachedValue="115";std::string old=UI::search;strcpy(UI::search,"f4");bool key=UI::matches(a);strcpy(UI::search,"body physics");bool name=UI::matches(a);strcpy(UI::search,"not present");bool negative=!UI::matches(a);strcpy(UI::search,old.c_str());return key&&name&&negative?0:1;}

extern "C" __declspec(dllexport) int ModActivatorScrollTest() {
  if (!folder.empty())
    return -1;
  auto *context = ImGui::CreateContext();
  auto &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(1000, 860);
  io.DeltaTime = 1.0f / 60;
  io.IniFilename = nullptr;
  ml::gui::InitStyle(1.0f);
  unsigned char *pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

  catalog.clear();
  for (int i = 0; i < 30; ++i) {
    Action action{"Fixture", "Fixture.asi", "", "", std::to_string(i)};
    action.cachedKeys = {119};
    catalog.push_back(action);
  }
  catalogChanged();
  ++layoutVersion;
  previewMode = true;
  previewTab = 0;
  ml::State::Get().menuOpen = true;
  const auto frame = [] {
    ImGui::NewFrame();
    ml::gui::Render();
    ImGui::Render();
  };
  frame();
  frame();
  frame();
  ImGuiWindow *card = nullptr;
  for (auto *window : context->Windows) {
    if (window->Active && strstr(window->Name, "/Card_") &&
        window->ParentWindow && window->ParentWindow->ScrollMax.y > 0 &&
        window->ParentWindow->InnerRect.Contains(
            ImVec2(window->Pos.x + 20, window->Pos.y + 15))) {
      card = window;
      break;
    }
  }
  int result = 1;
  if (card) {
    io.AddMousePosEvent(card->Pos.x + 20, card->Pos.y + 15);
    frame();
    result = 2;
    if (context->HoveredWindow == card) {
      const float start = card->ParentWindow->Scroll.y;
      io.AddMouseWheelEvent(0, -2);
      frame();
      frame();
      result = card->ParentWindow->Scroll.y > start ? 0 : 3;
    }
  }
  catalog.clear();
  catalogChanged();
  ++layoutVersion;
  previewMode = false;
  ImGui::DestroyContext(context);
  return result;
}

extern "C" __declspec(dllexport) int ModActivatorDragTest(const char *root) {
  if (!folder.empty() || !root)
    return -1;
  folder = root;
  auto *context = ImGui::CreateContext();
  auto &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(1000, 860);
  io.DeltaTime = 1.0f / 60;
  io.IniFilename = nullptr;
  ml::gui::InitStyle(1.0f);
  unsigned char *pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

  for (auto name : {"Alpha", "Beta", "Gamma"}) {
    Action a{name, std::string(name) + ".asi", std::string(name) + ".ini", "Menu", "Hotkey"};
    a.cachedKeys = {VK_F8};
    catalog.push_back(a);
  }
  catalogChanged();
  ++layoutVersion;
  previewMode = true;
  previewTab = 0;
  ml::State::Get().menuOpen = true;
  const auto frame = [] {
    ImGui::NewFrame();
    ml::gui::Render();
    ImGui::Render();
  };
  frame();
  frame();
  frame();
  std::vector<ImGuiWindow *> cards;
  for (auto *window : context->Windows)
    if (window->Active && strstr(window->Name, "/Card_"))
      cards.push_back(window);
  std::sort(cards.begin(), cards.end(),
            [](const auto *a, const auto *b) { return a->Pos.y < b->Pos.y; });

  int result = 1;
  if (cards.size() == 3) {
    const auto source = cards.front()->Pos;
    const auto target = cards.back()->Pos;
    const float bottom = target.y + cards.back()->Size.y - 10;
    io.AddMousePosEvent(source.x + 16, source.y + 15); // on the drag handle
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    io.AddMousePosEvent(target.x + 80, bottom);
    frame();
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
    frame();

    modLayout = {};
    ++layoutVersion;
    loadLayout();
    auto ordered = catalog;
    orderActions(ordered);
    result = ordered[0].label == "Beta" && ordered[1].label == "Gamma" &&
             ordered[2].label == "Alpha" ? 0 : 2;
    if (!result) {
      const auto alpha = identity(ordered[2]), beta = identity(ordered[0]);
      if (!reorderActions(ordered, {alpha, beta, false}))
        result = 3;
      orderActions(ordered);
      if (ordered[0].label != "Alpha")
        result = 4;
    }
  }
  catalog.clear();
  modLayout = {};
  catalogChanged();
  ++layoutVersion;
  folder.clear();
  previewMode = false;
  ml::State::Get().menuOpen = false;
  ImGui::DestroyContext(context);
  return result;
}

// Dragging a tab moves it and saves the order; a new tab then joins at the
// end, before the +.
extern "C" __declspec(dllexport) int ModActivatorTabDragTest(const char *root) {
  if (!folder.empty() || !root)
    return -1;
  folder = root;
  auto *context = ImGui::CreateContext();
  auto &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(1000, 860);
  io.DeltaTime = 1.0f / 60;
  io.IniFilename = nullptr;
  ml::gui::InitStyle(1.0f);
  unsigned char *pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  modLayout = {};
  modLayout.tabs[3] = "Mine";
  ++layoutVersion;
  previewMode = true;
  previewTab = 0;
  ml::State::Get().menuOpen = true;
  const auto frame = [] {
    ImGui::NewFrame();
    ml::gui::Render();
    ImGui::Render();
  };
  const auto findBar = [&]() -> ImGuiTabBar * {
    for (int i = 0; i < context->TabBars.GetMapSize(); ++i)
      if (auto *bar = context->TabBars.TryGetMapData(i); bar && bar->Tabs.Size)
        return bar;
    return nullptr;
  };
  // Where the tab at `index` (in the bar's order) is drawn, at its height.
  const auto tabAt = [&](ImGuiTabBar *bar, int index, float along) {
    const auto &tab = bar->Tabs[index];
    return ImVec2(bar->BarRect.Min.x + tab.Offset + tab.Width * along - bar->ScrollingAnim,
                  bar->BarRect.GetCenter().y);
  };
  frame();
  frame();
  int result = 1;
  if (auto *bar = findBar(); bar && bar->Tabs.Size == 4) {
    // Mine (third) to the far left, a few pixels at a time as a hand does.
    const ImVec2 from = tabAt(bar, 2, 0.5f), to = tabAt(bar, 0, 0.1f);
    io.AddMousePosEvent(from.x, from.y);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    for (int step = 1; step <= 20; ++step) {
      io.AddMousePosEvent(from.x + (to.x - from.x) * step / 20, from.y);
      frame();
    }
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
    frame();
    modLayout = {};
    ++layoutVersion;
    loadLayout();
    const auto tabs = orderedTabs(modLayout);
    result = tabs.size() == 3 && tabs[0].first == 3 && tabs[1].first == 0 && tabs[2].first == 1 ? 0 : 2;
    if (!result) {
      // A new tab: after Mine, before the +.
      modLayout.tabs[4] = "New";
      ++layoutVersion;
      frame();
      frame();
      frame();
      bar = findBar();
      const auto last = [&](int fromEnd) { return bar->Tabs[bar->Tabs.Size - 1 - fromEnd]; };
      if (!bar || bar->Tabs.Size != 5 || !(last(0).Flags & ImGuiTabItemFlags_Button) ||
          last(1).ID != ImGui::GetIDWithSeed("###Category4", nullptr, bar->ID))
        result = 3;
    }
    // Hotkeys and Settings keep the tabs above them; clicking one goes back.
    previewMode = false;
    for (const int view : {viewHotkeys, viewSettings}) {
      if (result)
        break;
      panelView = view;
      frame();
      frame();
      bar = findBar();
      if (!bar || bar->Tabs.Size != 5 || panelView != view) {
        result = 4;
        break;
      }
      const ImVec2 at = tabAt(bar, 1, 0.5f);
      io.AddMousePosEvent(at.x, at.y);
      frame();
      io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
      frame();
      io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
      frame();
      frame();
      if (panelView != viewTabs)
        result = 5;
    }
  }
  panelView = viewTabs;
  modLayout = {};
  ++layoutVersion;
  folder.clear();
  previewMode = false;
  ml::State::Get().menuOpen = false;
  ImGui::DestroyContext(context);
  return result;
}

// Send in Hotkeys takes the click: the row's selectable spans the Send
// column, and without AllowOverlap it took every click there.
extern "C" __declspec(dllexport) int ModActivatorSendTest() {
  if (!folder.empty())
    return -1;
  auto *context = ImGui::CreateContext();
  auto &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(1000, 860);
  io.DeltaTime = 1.0f / 60;
  io.IniFilename = nullptr;
  ml::gui::InitStyle(1.0f);
  unsigned char *pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  {
    std::lock_guard<std::mutex> lock(catalogMutex);
    Action a{"Weather", "Weather.asi", "Weather.ini", "Input", "ToggleKey"};
    a.cachedKeys = {VK_F9};
    a.toggle = true;
    catalog = {a};
    allBindings = {a};
    pendingActions.clear();
  }
  catalogChanged();
  ++layoutVersion;
  previewMode = true;
  previewTab = 2;
  ml::State::Get().menuOpen = true;
  const auto frame = [] {
    ImGui::NewFrame();
    ml::gui::Render();
    ImGui::Render();
  };
  frame();
  frame();
  frame();
  int result = 1;
  if (lastSendButton.GetWidth() > 0) {
    const ImVec2 centre = lastSendButton.GetCenter();
    io.AddMousePosEvent(centre.x, centre.y);
    for (int i = 0; i < 4; ++i) // the mouse rests on it a moment, as a hand does
      frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
    std::lock_guard<std::mutex> lock(catalogMutex);
    result = pendingActions.size() == 1 && pendingActions.front().key == "ToggleKey" ? 0 : 2;
  }
  {
    std::lock_guard<std::mutex> lock(catalogMutex);
    catalog.clear();
    allBindings.clear();
    pendingActions.clear();
  }
  catalogChanged();
  ++layoutVersion;
  previewMode = false;
  ImGui::DestroyContext(context);
  return result;
}

// Settings > Interface > Reread mod configs asks for the mods' configs to be
// read again, as Refresh does.
extern "C" __declspec(dllexport) int ModActivatorRereadTest() {
  if (!folder.empty())
    return -1;
  auto *context = ImGui::CreateContext();
  auto &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(900, 600);
  io.DeltaTime = 1.0f / 60;
  io.IniFilename = nullptr;
  unsigned char *pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  const auto frame = [] {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(900, 600));
    ImGui::Begin("Reread");
    drawInterfaceSettings();
    ImGui::End();
    ImGui::Render();
  };
  refreshRequested.store(false);
  lastRereadButton = {};
  frame();
  frame();
  int result = 1;
  if (lastRereadButton.GetWidth() > 0) {
    result = refreshRequested.load() ? 2 : 0;
    const ImVec2 centre = lastRereadButton.GetCenter();
    io.AddMousePosEvent(centre.x, centre.y);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
    if (!result && !refreshRequested.exchange(false))
      result = 3;
  }
  settingsStatus.clear();
  ImGui::DestroyContext(context);
  return result;
}

extern "C" __declspec(dllexport) int ModActivatorGroupsTest() {
  if (!folder.empty())
    return -1;
  auto *context = ImGui::CreateContext();
  auto &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(900, 600);
  io.DeltaTime = 1.0f / 60;
  io.IniFilename = nullptr;
  unsigned char *pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  const std::string previousSearch = UI::search;

  Action first{"Cycle", "CombatEvolved.asi", "CombatEvolved.ini", "Combat", "StanceCycleKey"};
  first.description = "Switch unarmed stances.\nUse with fists drawn.";
  Action second{"Mark", "COMBATEVOLVED.ASI", "CombatEvolved.ini", "Combat", "MarkKey"};
  Action third{"Weather", "Weather.addon64", "Weather.ini", "Input", "ToggleKey"};
  const std::vector<Action> bindings{first, second, third};
  const auto frame = [&](const char *search, bool openCombat) {
    strcpy(UI::search, search);
    int drawn = 0;
    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(600, 430));
    ImGui::Begin("Groups");
    if (ImGui::BeginTable("Group fixtures", 2)) {
      if (openCombat) {
        ImGui::PushID("combatevolved.asi");
        ImGui::GetStateStorage()->SetInt(ImGui::GetID("###ModGroup"), 1);
        ImGui::PopID();
      }
      UI::groupedBindings(bindings, [&](const Action &binding) {
        ++drawn;
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(UI::actionName(binding).c_str());
        ImGui::TableSetColumnIndex(1);
        UI::descriptionCell(binding);
      });
      ImGui::EndTable();
    }
    ImGui::End();
    ImGui::Render();
    return drawn;
  };
  int result = 0;
  if (frame("", false) != 0 || frame("", false) != 0)
    result = 1;
  else if (frame("fists drawn", false) != 1 || frame("", false) != 0)
    result = 2;
  else if (frame("", true) != 2)
    result = 3;
  strcpy(UI::search, previousSearch.c_str());
  ImGui::DestroyContext(context);
  return result;
}

// Text encodings, glyph coverage, live UI scale and the menu's per-change
// caching. root is an empty folder for config fixtures.
extern "C" __declspec(dllexport) int ModActivatorTextTest(const char *root) {
  if (!folder.empty() || !root)
    return -1;
  const std::string dir = root;
  const auto write = [&](const char *name, const std::string &bytes) {
    std::ofstream(dir + name, std::ios::binary) << bytes;
    return std::filesystem::path(dir + name);
  };
  const bool westernAnsi = GetACP() == 1252;
  const std::string chinese = "\xe6\x89\x93\xe5\xbc\x80\xe8\x8f\x9c\xe5\x8d\x95"; // 打开菜单

  // UTF-16 comments are decoded instead of turned into spaces, and their
  // section and field names still match what the profile API reports.
  std::wstring wide = L"\xfeff[Men\u00fc]\r\n; \u6253\u5f00\u83dc\u5355\r\nHotkey=F6\r\n";
  auto comments = readBindingComments(
      write("utf16.ini", std::string(reinterpret_cast<const char *>(wide.data()), wide.size() * 2)));
  const std::string section = westernAnsi ? "men\xfc" : lower(convertText("Men\xc3\xbc", CP_UTF8, CP_ACP));
  const auto utf16 = comments.find({section, "hotkey"});
  if (utf16 == comments.end() || utf16->second.description != chinese)
    return 1;
  // UTF-8 without a BOM is kept as it is.
  comments = readBindingComments(write("utf8.ini", "[Menu]\n; \xc3\x96" "ffnet das Men\xc3\xbc\nHotkey=F6\n"));
  if (comments[{"menu", "hotkey"}].description != "\xc3\x96" "ffnet das Men\xc3\xbc")
    return 2;
  // Anything else is the ANSI code page, converted for display.
  if (westernAnsi) {
    comments = readBindingComments(write("ansi.ini", "[Menu]\n; \xd6" "ffnet das Men\xfc\nHotkey=F6\n"));
    if (comments[{"menu", "hotkey"}].description != "\xc3\x96" "ffnet das Men\xc3\xbc")
      return 3;
    Action ansiName{"x", "Men\xfc" "Mod.asi", "x.ini", "Menu", "Hotkey"};
    if (UI::name(ansiName) != "Men\xc3\xbc Mod")
      return 4;
  }

  auto *context = ImGui::CreateContext();
  auto &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(1280, 720);
  io.DeltaTime = 1.0f / 60;
  int result = 0;
  UI::extraGlyphs.clear();
  ml::gui::InitStyle(1.0f);
  // Latin text needs nothing new; Chinese text schedules a rebuild that
  // merges a Windows font holding those characters.
  UI::noteGlyphs(std::string("Men\xc3\xbc \xc3\x96" "ffnen"));
  if (ml::gui::FontsNeedRebuild())
    result = 5;
  UI::noteGlyphs(chinese);
  if (!result && (!ml::gui::FontsNeedRebuild() || UI::extraGlyphs.size() != 4))
    result = 6;
  if (!result) {
    ml::gui::RebuildFonts();
    unsigned char *pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    if (ml::gui::FontsNeedRebuild() || io.Fonts->ConfigData.Size < 2 ||
        !io.Fonts->Fonts[0]->FindGlyphNoFallback(0x6253))
      result = 7;
  }
  // The UI scale follows the target height, from the default style each
  // time, so scaling up and back down returns exactly to the start.
  const float padding = ImGui::GetStyle().WindowPadding.x;
  if (!result) {
    ml::gui::SetDisplayHeight(2160);
    if (!ml::gui::FontsNeedRebuild())
      result = 8;
    ml::gui::RebuildFonts();
    ml::gui::SetDisplayHeight(2160);
    if (!result && (UI::scale != 1.7f || ImGui::GetStyle().WindowPadding.x != floorf(padding * 1.7f) ||
                    io.Fonts->ConfigData[0].SizePixels != ml::gui::baseFontSize * 1.7f || ml::gui::FontsNeedRebuild()))
      result = 9;
    ml::gui::SetDisplayHeight(1080);
    ml::gui::RebuildFonts();
    if (!result && (UI::scale != 1.0f || ImGui::GetStyle().WindowPadding.x != padding))
      result = 10;
  }
  // The menu copies the catalog once per change, not once per frame.
  if (!result) {
    unsigned char *pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    {
      std::lock_guard<std::mutex> lock(catalogMutex);
      catalog.clear();
      for (int i = 0; i < 3; ++i) {
        Action a{"Card", "Card" + std::to_string(i) + ".asi", "Card.ini", "Menu", std::to_string(i)};
        a.cachedKeys = {VK_F8};
        catalog.push_back(a);
      }
    }
    catalogChanged();
    ml::State::Get().menuOpen = true;
    const auto frame = [] {
      ImGui::NewFrame();
      ml::gui::Render();
      ImGui::Render();
    };
    frame();
    const unsigned stamp = ml::gui::menuSnapshot().stamp;
    for (int i = 0; i < 10; ++i)
      frame();
    const auto &snap = ml::gui::menuSnapshot();
    if (snap.stamp != stamp || snap.actions.size() != 3 || snap.actions[0].ui.name.empty())
      result = 11;
    {
      std::lock_guard<std::mutex> lock(catalogMutex);
      catalog.push_back(catalog.front());
    }
    frame();
    if (!result && ml::gui::menuSnapshot().actions.size() != 3)
      result = 12; // copied without a change being announced
    catalogChanged();
    frame();
    if (!result && ml::gui::menuSnapshot().actions.size() != 4)
      result = 13;
  }
  {
    std::lock_guard<std::mutex> lock(catalogMutex);
    catalog.clear();
  }
  catalogChanged();
  ml::State::Get().menuOpen = false;
  UI::extraGlyphs.clear();
  UI::glyphsChanged = false;
  ImGui::DestroyContext(context);
  return result;
}
