// Screenshots of the panel without the game, and the Nexus thumbnail and
// header: sample mods drawn through the real menu, exported as Dear ImGui draw
// lists for scripts/render_preview.py.
//
// Part of activator.cpp, included at its end in the order listed
// there. Not a header of its own: it relies on the parts before it.

// Set by ModActivatorThumbnail (1) and ModActivatorHeader (2) for one
// ModActivatorPreview call: which Nexus image to draw around the panel.
static int sceneMode = 0;
extern "C" __declspec(dllexport) int ModActivatorPreview(const char *path,
                                                         int tab) {
  if (!folder.empty())
    return -1;
  previewMode = true;
  previewTab = tab;
  ml::State::Get().menuOpen = true;
  catalog.clear();
  allBindings.clear();
  restartRequired.clear();
  // A typical setup, as 1.3 shows it: names and descriptions from the
  // known-mods list, controller buttons, several cards for one mod, and a
  // conflict with Steam's screenshot key. Texts follow the shipped list.
  enum Kind { menu, toggle, action };
  struct Sample {
    const char *module, *modName, *section, *key;
    std::vector<int> keys;
    Kind kind;
    const char *name, *description;
    unsigned pad = 0;
  };
  constexpr unsigned ls = XINPUT_GAMEPAD_LEFT_THUMB, rs = XINPUT_GAMEPAD_RIGHT_THUMB;
  const Sample samples[] = {
      {"InstantSurgery.asi", "Instant Surgery", "General", "ActivationKeyVK", {VK_PRIOR}, menu,
       "Body editor", "Opens the Instant Surgery body editor."},
      {"CrimsonRoute.asi", "Crimson Route", "SettingsWindow", "ManageLocationsHotkey", {VK_F8}, menu,
       "Settings window", "Opens Crimson Route's settings and saved locations.", ls | rs | XINPUT_GAMEPAD_A},
      {"CrimsonRoute.asi", "Crimson Route", "AutoMove", "ToggleHotkey", {VK_F9}, toggle,
       "Auto-move", "Starts or stops moving along the selected route.", ls | rs | XINPUT_GAMEPAD_X},
      {"CrimsonRoute.asi", "Crimson Route", "AutoMove", "ModeHotkey", {VK_CONTROL, VK_F10}, action,
       "Auto-move pace", "Cycles walking, running and sprinting while auto-moving.",
       ls | rs | XINPUT_GAMEPAD_Y},
      {"PrivateStorageAnywhere.asi", "Private Storage Anywhere", "Hotkeys", "PrivateHotkey", {VK_F4},
       menu, "Private Storage", "Opens your Private Storage."},
      {"PrivateStorageAnywhere.asi", "Private Storage Anywhere", "Hotkeys", "DresserHotkey", {VK_F6},
       menu, "Dresser", "Opens the housing Dresser."},
      {"CDWiki.asi", "Mount and Pet Wiki", "CDWiki", "ToggleKey", {VK_F7}, menu, "Wiki",
       "Opens or closes the mount and pet wiki.", XINPUT_GAMEPAD_BACK | ls},
      {"CrimsonFastLoad.asi", "Crimson FastLoad", "Menu", "Hotkey", {VK_F5}, menu, "FastLoad menu",
       "Opens the Crimson FastLoad menu, where every feature has its own switch."},
      // F3, not the mod's default ` key: a symbol key's label follows the
      // keyboard layout of whoever renders the preview.
      {"CombatEvolved.asi", "Combat Evolved", "CombatEvolved", "StanceCycleKey", {VK_F3}, action,
       "Cycle stance", "Switches to the next fighting style: Kliff's, Damiane's or Oongka's.",
       XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_DPAD_DOWN},
      {"CombatEvolved.asi", "Combat Evolved", "CombatEvolved", "StanceDamianeKey", {VK_CONTROL, '8'},
       action, "Damiane's stance", "Switches straight to Damiane's fighting style."},
      {"CalmerDynamicWind.asi", "Calmer Dynamic Wind", "Hotkeys", "ToggleKey", {VK_INSERT}, toggle,
       "Calmer wind", "Switches between the calmer wind and the game's own, to compare."},
      // The clash the mod's own config warns about: F12 is Steam's
      // screenshot key.
      {"CalmerDynamicWind.asi", "Calmer Dynamic Wind", "Hotkeys", "DisableKey", {VK_F12}, action,
       "Disable until restart", "Turns the mod off until the game is restarted."},
      {"CrimsonWeather.addon64", "Crimson Weather", "General", "HotkeyToggleEffect", {VK_DELETE},
       toggle, "Weather effect", "Turns Crimson Weather's effect on or off."},
      {"KC- Gilded Compass.asi", "Gilded Compass", "Menu", "MapHotkeyVk", {VK_CONTROL, VK_F8}, toggle,
       "Minimap", "Hides or shows the minimap."},
  };
  for (const auto &sample : samples) {
    const auto stem = std::filesystem::path(sample.module).stem().string();
    Action a{stem + " - " + sample.section + " / " + sample.key, sample.module, stem + ".ini",
             sample.section, sample.key, sample.kind == toggle};
    a.cachedKeys = sample.keys;
    a.cachedValue = shortcutLabel(sample.keys);
    a.modName = sample.modName;
    a.customName = sample.name;
    a.description = sample.description;
    a.descriptionSource = sourceKnownMods;
    a.padButtons = sample.pad;
    a.menuRank = sample.kind == menu ? 150 : 0;
    a.featureRank = sample.kind == toggle ? 150 : 0;
    a.chosenKind = true;
    if (sample.kind != action)
      catalog.push_back(a);
    allBindings.push_back(a);
  }
  // The conflicts a refresh would find: Calmer Dynamic Wind's F12 is Steam's
  // screenshot key, and Crimson Route's F8 overlaps Gilded Compass's Ctrl+F8.
  std::vector<ReservedKey> reserved{{{VK_F12}, "Steam: screenshot (default key)"}};
  markConflicts(allBindings, reserved);
  catalogChanged();
  ++layoutVersion;
  auto *ctx = ImGui::CreateContext();
  auto &io = ImGui::GetIO();
  // The Nexus images are scenes: the name and what it does on the left, large
  // enough to read when shrunk, and the panel on the right. The thumbnail is
  // 16:9; the header banner is 4:1, with the panel running off its bottom
  // edge and fading out.
  struct Scene {
    ImVec2 display;
    float left, titleY, titleMax, titleSize, ruleY;
    std::vector<const char *> tagline;
    float taglineY, taglineSize, featureY, featureSize, footY;
    ImVec2 glow;
    bool fade;
  };
  const Scene scenes[] = {
      {{1280, 720}, 64, 168, 470, 92, 290, {"Every mod's menu, toggle", "and hotkey in one panel"},
       322, 34, 448, 26, 624, {260, 470}, false},
      {{1920, 480}, 120, 92, 900, 124, 238, {"Every mod's menu, toggle and hotkey in one panel"},
       266, 40, 336, 30, 0, {420, 300}, true},
  };
  const bool thumbnail = sceneMode != 0;
  const Scene &scene = scenes[thumbnail ? sceneMode - 1 : 0];
  io.DisplaySize = thumbnail ? scene.display : ImVec2(1000, 860);
  io.DeltaTime = 1.0f / 60;
  io.IniFilename = nullptr;
  ml::gui::InitStyle(thumbnail ? 0.86f : 1.0f);
  ImFont *titleFont = nullptr, *textFont = nullptr;
  if (thumbnail) {
    wchar_t windows[MAX_PATH]{};
    GetWindowsDirectoryW(windows, MAX_PATH);
    const auto font = [&](const wchar_t *file, float size) -> ImFont * {
      const std::wstring path = std::wstring(windows) + L"\\Fonts\\" + file;
      const auto utf8 = narrowText(path.c_str(), (int)path.size(), CP_UTF8);
      return GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES
                 ? nullptr
                 : io.Fonts->AddFontFromFileTTF(utf8.c_str(), size);
    };
    titleFont = font(L"segoeuib.ttf", scene.titleSize);
    textFont = font(L"segoeui.ttf", scene.taglineSize);
    if (!titleFont || !textFont)
      titleFont = textFont = io.Fonts->Fonts[0];
  }
  unsigned char *pixels;
  int w, h;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
  io.Fonts->SetTexID((ImTextureID)1);
  for (int i = 0; i < 4; ++i) {
    ImGui::NewFrame();
    if (thumbnail) {
      auto *bg = ImGui::GetBackgroundDrawList();
      const ImVec2 size = io.DisplaySize;
      // Night over the desert: dark, warmer towards the bottom, with a
      // crimson glow behind the words.
      bg->AddRectFilledMultiColor(ImVec2(0, 0), size, IM_COL32(20, 17, 22, 255),
                                  IM_COL32(20, 17, 22, 255), IM_COL32(34, 14, 16, 255),
                                  IM_COL32(34, 14, 16, 255));
      // Many faint layers, so the glow fades without visible rings.
      for (int r = 48; r >= 1; --r)
        bg->AddCircleFilled(scene.glow, r * 10.5f, IM_COL32(150, 26, 30, 2), 96);
      const ImU32 gold = IM_COL32(214, 176, 110, 255), light = IM_COL32(232, 228, 220, 255),
                  muted = IM_COL32(150, 148, 150, 255);
      const float left = scene.left;
      // Text as large as fits beside the panel.
      const auto fit = [&](ImFont *font, float size, const char *text) {
        while (size > 14 && font->CalcTextSizeA(size, FLT_MAX, 0, text).x > scene.titleMax)
          size -= 1;
        return size;
      };
      bg->AddText(titleFont, fit(titleFont, scene.titleSize, "ModActivator"),
                  ImVec2(left, scene.titleY), gold, "ModActivator");
      bg->AddRectFilled(ImVec2(left + 4, scene.ruleY), ImVec2(left + 124, scene.ruleY + 4), gold);
      for (size_t l = 0; l < scene.tagline.size(); ++l)
        bg->AddText(textFont, fit(textFont, scene.taglineSize, scene.tagline[l]),
                    ImVec2(left, scene.taglineY + l * scene.taglineSize * 1.3f), light, scene.tagline[l]);
      const char *features = "Keyboard  \xc2\xb7  Controller  \xc2\xb7  Conflict checks";
      bg->AddText(textFont, fit(textFont, scene.featureSize, features), ImVec2(left, scene.featureY),
                  muted, features);
      // No version: the images outlive releases; the panel shows it.
      if (scene.footY > 0)
        bg->AddText(textFont, 24.0f, ImVec2(left, scene.footY), muted, "For Crimson Desert");
      const ImVec2 panel(820 * UI::scale, 740 * UI::scale);
      ImGui::SetNextWindowPos(scene.fade ? ImVec2(size.x - left - panel.x, 56)
                                         : ImVec2(size.x - 24 - panel.x, (size.y - panel.y) * 0.5f));
    }
    ml::gui::Render();
    // A panel that runs off the bottom fades into the backdrop over its last
    // third, drawn above it.
    if (thumbnail && scene.fade) {
      // Over the panel only, so the words beside it stay.
      // Fully faded a little before the edge, so nothing shows through at
      // the bottom.
      const ImVec2 size = io.DisplaySize;
      const float panelLeft = size.x - scene.left - 820 * UI::scale - 16;
      const ImU32 clear = IM_COL32(34, 14, 16, 0), solid = IM_COL32(34, 14, 16, 255);
      auto *fg = ImGui::GetForegroundDrawList();
      fg->AddRectFilledMultiColor(ImVec2(panelLeft, size.y * 0.58f), ImVec2(size.x, size.y * 0.9f),
                                  clear, clear, solid, solid);
      fg->AddRectFilled(ImVec2(panelLeft, size.y * 0.9f), size, solid);
    }
    ImGui::Render();
  }
  auto *d = ImGui::GetDrawData();
  FILE *f = fopen(path, "wb");
  if (!f) {
    ImGui::DestroyContext(ctx);
    previewMode = false;
    return -2;
  }
  auto put = [&](auto v) { fwrite(&v, sizeof v, 1, f); };
  put(w);
  put(h);
  fwrite(pixels, 4, w * h, f);
  put(d->CmdListsCount);
  for (int n = 0; n < d->CmdListsCount; ++n) {
    auto *l = d->CmdLists[n];
    put(l->VtxBuffer.Size);
    fwrite(l->VtxBuffer.Data, sizeof(ImDrawVert), l->VtxBuffer.Size, f);
    put(l->IdxBuffer.Size);
    for (auto idx : l->IdxBuffer)
      put((unsigned int)idx);
    put(l->CmdBuffer.Size);
    for (auto &c : l->CmdBuffer) {
      put(c.ElemCount);
      put(c.IdxOffset);
      put(c.VtxOffset);
      fwrite(&c.ClipRect, sizeof(float), 4, f);
    }
  }
  fclose(f);
  ImGui::DestroyContext(ctx);
  previewMode = false;
  return 0;
}
// The Nexus thumbnail (1280 x 720): ModActivatorPreview's samples on the
// given tab, beside the name and a line about what the mod does.
extern "C" __declspec(dllexport) int ModActivatorThumbnail(const char *path, int tab) {
  sceneMode = 1;
  const int result = ModActivatorPreview(path, tab);
  sceneMode = 0;
  return result;
}
// The Nexus header banner (1920 x 480): the same, wide, with the panel on
// the given tab running off the bottom.
extern "C" __declspec(dllexport) int ModActivatorHeader(const char *path, int tab) {
  sceneMode = 2;
  const int result = ModActivatorPreview(path, tab);
  sceneMode = 0;
  return result;
}
