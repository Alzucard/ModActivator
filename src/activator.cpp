#include "core/log.h"
#include "core/settings.h"
#include "core/state.h"
#include "gui/menu.h"
#include "hooks/dx12_hook.h"
#include "hooks/input.h"
#include "version.h"
#include <MinHook.h>
#include <TlHelp32.h>
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <deque>
#include <imgui.h>
#include <imgui_internal.h>
#include <mutex>
#include <map>
#include <set>
#include <string>
#include <vector>
static bool previewMode = false;
static int previewTab = 0;
static std::string folder;
static std::atomic<unsigned long> configReads{0};

struct Action {
  std::string label, module, file, section, key;
  bool toggle = false;
  std::string cachedValue;
  std::vector<int> cachedKeys;
  int severity = 0;
  std::string overlapNames;
  int typeOverride = 0;
  int menuRank = 0;
  int featureRank = 0;
};
static std::string read(const Action &a) {
  ++configReads;
  char b[512];
  const auto size = GetPrivateProfileStringA(
      a.section.c_str(), a.key.c_str(), "", b, sizeof b, (folder + a.file).c_str());
  return size < sizeof b - 1 ? std::string(b, size) : std::string{};
}
static bool held(int k) { return (GetAsyncKeyState(k) & 0x8000) != 0; }
#include "shortcut_keys.h"
static int vk(std::string s, HKL layout = gameKeyboardLayout(),
              unsigned *physicalScan = nullptr, bool namedDigit = false) {
  s.erase(std::remove_if(s.begin(), s.end(),
                         [](unsigned char c) { return c == ' '; }),
          s.end());
  for (auto &c : s)
    c = (char)toupper((unsigned char)c);
  if (s.empty() || s == "NONE")
    return 0;
  if (s == "CTRL")
    return 17;
  if (s == "SHIFT")
    return 16;
  if (s == "ALT")
    return 18;
  if (s == "HOME")
    return 36;
  if (s == "END")
    return 35;
  if (s == "INSERT")
    return 45;
  if (s == "DELETE")
    return 46;
  if (s == "PAGEUP" || s == "PGUP")
    return 33;
  if (s == "PAGEDOWN" || s == "PGDN")
    return 34;
  if (s == "LEFT")
    return 37;
  if (s == "UP")
    return 38;
  if (s == "RIGHT")
    return 39;
  if (s == "DOWN")
    return 40;
  if (s == "BACKSPACE")
    return 8;
  if (s == "CAPSLOCK")
    return 20;
  if (s == "PRINTSCREEN")
    return 44;
  if (s.size() == 7 && s.substr(0, 6) == "NUMPAD" && isdigit(s[6]))
    return 96 + s[6] - '0';
  if (s == "PAUSE")
    return 19;
  if (s == "SCROLLLOCK")
    return 145;
  if (s == "SPACE")
    return 32;
  if (s == "TAB")
    return 9;
  if (s == "ESC" || s == "ESCAPE")
    return 27;
  if (s == "RETURN" || s == "ENTER")
    return 13;
  if (s[0] == 'F' && s.size() > 1) {
    char *end = nullptr;
    long n = strtol(s.c_str() + 1, &end, 10);
    return end && !*end && n >= 1 && n <= 24 ? 111 + (int)n : 0;
  }
  if (isdigit((unsigned char)s[0]) && !(namedDigit && s.size() == 1)) {
    char *end = nullptr;
    long v = strtol(s.c_str(), &end, s.size() > 2 && s[1] == 'X' ? 16 : 10);
    return end && !*end && v >= 0 && v <= 255 ? (int)v : 0;
  }
  const unsigned scan = s == "GRAVE" ? 0x29 :
                        s.size() == 1 && (unsigned char)s[0] < 128
                            ? scanForUsSymbol(s[0])
                            : scanForLocalizedSymbol(s, layout);
  const int key = scan ? vkForScan(scan, layout) : 0;
  if (physicalScan)
    *physicalScan = scan;
  return key;
}
static bool validShortcut(const std::vector<int> &ks) {
  if (ks.empty() || ks.size() > 4)
    return false;
  std::set<int> seen;
  for (size_t i = 0; i < ks.size(); ++i) {
    int k = ks[i];
    if (k < 8 || k > 254 || !seen.insert(k).second)
      return false;
    bool modifier = k == 16 || k == 17 || k == 18;
    if (modifier == (i == ks.size() - 1))
      return false;
  }
  return true;
}
static std::string compactShortcut(std::string s) {
  s.erase(std::remove_if(s.begin(), s.end(),
                         [](unsigned char c) { return c == ' ' || c == '\t'; }),
          s.end());
  return s;
}
static std::vector<int> parseShortcut(std::string s,
                                      HKL layout = gameKeyboardLayout(),
                                      unsigned *mainScan = nullptr,
                                      bool namedDigit = false) {
  s = compactShortcut(std::move(s));
  if (mainScan)
    *mainScan = 0;
  std::vector<int> out;
  size_t pos = 0;
  while (true) {
    // A final literal plus can also be the main key (for example Ctrl++).
    size_t end = pos + 1 == s.size() && s[pos] == '+' ? s.npos : s.find('+', pos);
    auto token = s.substr(pos, end == s.npos ? end : end - pos);
    out.push_back(vk(token, layout, end == s.npos ? mainScan : nullptr,
                     pos != 0 || namedDigit));
    if (end == s.npos)
      return out;
    pos = end + 1;
  }
}
static constexpr unsigned defaultOpener = VK_BACK | (1u << 8) | (1u << 9);
static std::atomic<unsigned> opener{defaultOpener};
static unsigned shortcutCode(const std::vector<int> &keys) {
  if (!validShortcut(keys))
    return 0;
  unsigned code = keys.back();
  for (int k : keys) {
    if (k == VK_CONTROL)
      code |= 1u << 8;
    if (k == VK_SHIFT)
      code |= 1u << 9;
    if (k == VK_MENU)
      code |= 1u << 10;
  }
  return code;
}
static std::vector<int> openerKeys() {
  auto code = opener.load();
  std::vector<int> keys;
  if (code & (1u << 8))
    keys.push_back(VK_CONTROL);
  if (code & (1u << 9))
    keys.push_back(VK_SHIFT);
  if (code & (1u << 10))
    keys.push_back(VK_MENU);
  keys.push_back(code & 0xff);
  return keys;
}
static unsigned heldModifiers() {
  return (held(VK_CONTROL) ? 1u << 8 : 0) |
         (held(VK_SHIFT) ? 1u << 9 : 0) |
         (held(VK_MENU) ? 1u << 10 : 0);
}
static size_t bindingComment(const std::string &s, size_t start = 0) {
  auto semi = s.find(';', start);
  if (semi == s.npos)
    return semi;
  const auto previous = s.find_last_not_of(" \t", semi ? semi - 1 : 0);
  const auto after = s.find_first_not_of(" \t\r", semi + 1);
  const bool mainKey = semi == start || previous == s.npos ||
                       previous < start || s[previous] == '+';
  if (mainKey && (after == s.npos || s[after] == ';' || s[after] == '|'))
    return s.find(';', semi + 1);
  return semi;
}
static bool standaloneDigit(const Action &a, const std::string &s) {
  const auto n = a.key.size();
  const bool explicitVk = (n >= 2 && !_stricmp(a.key.c_str() + n - 2, "VK")) ||
                          (n >= 7 && !_stricmp(a.key.c_str() + n - 7, "KeyCode"));
  // A standalone zero is commonly used to disable a shortcut.
  return !explicitVk && s.size() == 1 && s[0] >= '1' && s[0] <= '9';
}
static std::vector<int> keysFrom(const Action &a, std::string s,
                                 HKL layout = gameKeyboardLayout(),
                                 unsigned *mainScan = nullptr) {
  if (mainScan)
    *mainScan = 0;
  configReads += 3;
  s.erase(std::remove_if(s.begin(), s.end(),
                         [](unsigned char c) { return c == ' ' || c == '\t'; }),
          s.end());
  auto comment = bindingComment(s);
  if (comment != std::string::npos)
    s.resize(comment);
  s = s.substr(0, s.find('|'));
  std::vector<int> out;
  for (auto pair : {std::pair<const char *, int>{"RequireCtrl", 17},
                    {"RequireShift", 16},
                    {"RequireAlt", 18}})
    if (GetPrivateProfileIntA(a.section.c_str(), pair.first, 0,
                              (folder + a.file).c_str()))
      out.push_back(pair.second);
  if (!_stricmp(a.file.c_str(), "ReShade.ini")) {
    int k = 0, c = 0, sh = 0, al = 0, used = 0;
    if (sscanf(s.c_str(), "%d,%d,%d,%d%n", &k, &c, &sh, &al, &used) != 4 ||
        used != (int)s.size() || c < 0 || c > 1 || sh < 0 || sh > 1 || al < 0 ||
        al > 1)
      return {};
    if (c)
      out.push_back(17);
    if (sh)
      out.push_back(16);
    if (al)
      out.push_back(18);
    out.push_back(k);
    return out;
  }
  auto named = parseShortcut(s, layout, mainScan, standaloneDigit(a, s));
  out.insert(out.end(), named.begin(), named.end());
  return out;
}
namespace ml::Log {
void Write(const char *level, const char *fmt, ...) {
  char b[2048];
  va_list a;
  va_start(a, fmt);
  vsnprintf(b, sizeof b, fmt, a);
  va_end(a);
  FILE *f = fopen((folder + "ModActivator.log").c_str(), "a");
  if (f) {
    fprintf(f, "[%s] %s\n", level, b);
    fclose(f);
  }
}
} // namespace ml::Log
#include "discovery.h"
#include "hotkey_editor.h"
#include "mod_layout.h"
static bool key(int k, bool up, unsigned scan) {
  INPUT i{};
  i.type = INPUT_KEYBOARD;
  i.ki.dwExtraInfo = ml::shortcutInputTag;
  i.ki.wVk = (WORD)k;
  i.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
  if (scan) {
    i.ki.wVk = 0;
    i.ki.wScan = (WORD)(scan & 0xff);
    i.ki.dwFlags |= KEYEVENTF_SCANCODE;
    if ((scan & 0xff00) == 0xe000)
      i.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
  } else if (k >= 33 && k <= 46) {
    i.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
  }
  return SendInput(1, &i, sizeof i) == 1;
}
static void dispatchAction(const Action &action) {
  const auto window = GetForegroundWindow();
  if (!ml::State::ForegroundIsOurs())
    return;

  Sleep(220);
  const auto layout = gameKeyboardLayout();
  unsigned mainScan = 0;
  auto keys = keysFrom(action, action.cachedValue, layout, &mainScan);
  if (!validShortcut(keys))
    return;
  if (keys.size() != action.cachedKeys.size() ||
      !std::equal(keys.begin(), keys.end() - 1, action.cachedKeys.begin())) {
    LOG_ERR("Shortcut cancelled: modifier settings changed. Refresh the list.");
    refreshRequested.store(true);
    return;
  }
  const auto inputHeld = [&] {
    return heldModifiers() || held(VK_LWIN) || held(VK_RWIN) ||
           held(VK_LBUTTON) || held(keys.back());
  };
  for (int attempt = 0; attempt < 40 && inputHeld() &&
                        GetForegroundWindow() == window; ++attempt)
    Sleep(25);

  auto &state = ml::State::Get();
  size_t pressed = 0;
  {
    std::lock_guard<std::mutex> lock(catalogMutex);
    if (restartRequired.count(lower(action.module)) ||
        !GetModuleHandleA(action.module.c_str()) ||
        GetForegroundWindow() != window || inputHeld()) {
      LOG_ERR("Shortcut cancelled: mod unavailable, input held, or game lost focus.");
      return;
    }
    unsigned currentScan = 0;
    const auto currentValue = read(action);
    const auto currentKeys = keysFrom(action, currentValue, gameKeyboardLayout(), &currentScan);
    if (currentValue != action.cachedValue || currentKeys != keys || currentScan != mainScan) {
      LOG_ERR("Shortcut cancelled: config or keyboard layout changed. Refresh the list.");
      refreshRequested.store(true);
      return;
    }
    state.sendingShortcut.store(true);
    for (int k : keys) {
      const auto scan = pressed + 1 == keys.size() ? mainScan : 0;
      if (GetForegroundWindow() != window || !key(k, false, scan))
        break;
      ++pressed;
    }
  }

  bool complete = pressed == keys.size();
  if (pressed)
    Sleep(130);
  while (pressed) {
    const auto scan = pressed == keys.size() ? mainScan : 0;
    const auto k = keys[--pressed];
    if (!key(k, true, scan) && !key(k, true, scan))
      complete = false;
  }
  state.sendingShortcut.store(false);
  if (complete)
    LOG("Shortcut dispatched: %s", action.label.c_str());
  else
    LOG_ERR("Shortcut incomplete: %s", action.label.c_str());
}

static DWORD WINAPI dispatch(void *) {
  for (;;) {
    if (refreshRequested.exchange(false))
      refresh();
    Action action;
    bool pending = false;
    {
      std::lock_guard<std::mutex> lock(catalogMutex);
      if (!pendingActions.empty()) {
        action = std::move(pendingActions.front());
        pendingActions.pop_front();
        pending = true;
      }
    }
    if (pending)
      dispatchAction(action);
    Sleep(20);
  }
  return 0;
}
static bool queueAction(const Action &a) {
  std::lock_guard<std::mutex> lock(catalogMutex);
  constexpr size_t queueLimit = 8;
  if (pendingActions.size() >= queueLimit || restartRequired.count(lower(a.module)))
    return false;
  pendingActions.push_back(a);
  if (!a.toggle)
    ml::State::Get().menuOpen = false;
  return true;
}
namespace ml::gui {
void InitStyle(float scale) {
  UI::scale = (std::max)(0.8f, (std::min)(1.7f, scale));
  ImGui::StyleColorsDark();
  auto &style = ImGui::GetStyle();
  style.WindowPadding = {22, 20};
  style.FramePadding = {12, 8};
  style.ItemSpacing = {10, 10};
  style.ItemInnerSpacing = {8, 6};
  style.CellPadding = {10, 4};
  style.WindowRounding = 12;
  style.ChildRounding = 8;
  style.FrameRounding = 6;
  style.TabRounding = 6;
  style.ScrollbarRounding = 8;
  style.ScrollbarSize = 10;
  style.WindowBorderSize = 1;
  style.ChildBorderSize = 1;
  auto *c = style.Colors;
  c[ImGuiCol_WindowBg] = {.075f, .080f, .092f, 1};
  c[ImGuiCol_ChildBg] = {.095f, .103f, .118f, 1};
  c[ImGuiCol_Border] = {.19f, .20f, .23f, 1};
  c[ImGuiCol_Text] = {.91f, .91f, .89f, 1};
  c[ImGuiCol_TextDisabled] = UI::muted;
  c[ImGuiCol_FrameBg] = {.12f, .13f, .15f, 1};
  c[ImGuiCol_FrameBgHovered] = {.17f, .18f, .20f, 1};
  c[ImGuiCol_FrameBgActive] = {.21f, .20f, .18f, 1};
  c[ImGuiCol_Button] = {.18f, .17f, .15f, 1};
  c[ImGuiCol_ButtonHovered] = {.30f, .25f, .18f, 1};
  c[ImGuiCol_ButtonActive] = {.40f, .32f, .20f, 1};
  c[ImGuiCol_CheckMark] = UI::gold;
  c[ImGuiCol_Header] = {.25f, .21f, .15f, 1};
  c[ImGuiCol_HeaderHovered] = {.29f, .25f, .18f, 1};
  c[ImGuiCol_HeaderActive] = {.35f, .29f, .19f, 1};
  c[ImGuiCol_Tab] = {.11f, .12f, .14f, 1};
  c[ImGuiCol_TabHovered] = {.28f, .24f, .17f, 1};
  c[ImGuiCol_TabSelected] = {.25f, .21f, .15f, 1};
  c[ImGuiCol_TabSelectedOverline] = UI::gold;
  c[ImGuiCol_TableHeaderBg] = {.13f, .14f, .16f, 1};
  c[ImGuiCol_TableRowBgAlt] = {1, 1, 1, .025f};
  c[ImGuiCol_Separator] = {.22f, .22f, .23f, 1};
  c[ImGuiCol_ScrollbarBg] = {0, 0, 0, 0};
  c[ImGuiCol_ScrollbarGrab] = {.30f, .30f, .32f, 1};
  style.ScaleAllSizes(UI::scale);
  auto &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  char fonts[MAX_PATH];
  GetWindowsDirectoryA(fonts, MAX_PATH);
  std::string font = std::string(fonts) + "\\Fonts\\segoeui.ttf";
  if (GetFileAttributesA(font.c_str()) != INVALID_FILE_ATTRIBUTES) {
    static const ImWchar ranges[] = {0x20, 0x024F, 0x0370, 0x052F, 0};
    io.Fonts->AddFontFromFileTTF(font.c_str(), 17.0f * UI::scale, nullptr, ranges);
  }
}
bool FontsNeedRebuild() { return false; }
void RebuildFonts() {}
void PollToggle() {
  static bool was = false;
  static bool escapeWas = false;
  const auto code = opener.load();
  const bool now = held(code & 0xff) && heldModifiers() == (code & ~0xffu) &&
                   !held(VK_LWIN) && !held(VK_RWIN);
  auto &s = State::Get();
  if (State::ForegroundIsOurs()) {
    static HKL lastLayout = nullptr;
    if (s.menuOpen) {
      const auto layout = gameKeyboardLayout();
      if (layout != lastLayout)
        refreshRequested.store(true);
      lastLayout = layout;
    }
    const bool textEditing = s.menuOpen && ImGui::GetCurrentContext() &&
                             ImGui::GetIO().WantTextInput;
    const bool acceptsShortcut = !UI::recording && !textEditing && !s.sendingShortcut;
    if (now && !was && acceptsShortcut) {
      s.menuOpen = !s.menuOpen;
      if (s.menuOpen)
        refreshRequested.store(true);
    }
    const bool escape = held(VK_ESCAPE);
    if (s.menuOpen && escape && !escapeWas && acceptsShortcut)
      s.menuOpen = false;
    escapeWas = escape;
  } else {
    s.menuOpen = false;
    UI::recording = 0;
    escapeWas = held(VK_ESCAPE);
    std::lock_guard<std::mutex> lock(catalogMutex);
    pendingActions.clear();
  }
  was = now;
}
bool WantsDraw() { return State::Get().menuOpen; }
void FeedInput() {
  auto &s = State::Get();
  auto &io = ImGui::GetIO();
  const bool c = s.menuOpen.load();
  static bool old = false;
  io.MouseDrawCursor = c;
  if (c != old) {
    old = c;
    if (c)
      input::MenuOpened();
    else {
      input::MenuClosed();
      s.lastDrawAt.store(0);
      io.ClearEventsQueue();
      io.ClearInputKeys();
      io.ClearInputMouse();
      io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }
  }
  if (c)
    input::FeedMouse(io);
}
void Render() {
  auto &s = State::Get();
  auto &io = ImGui::GetIO();
  float u = UI::scale;
  ImVec2 size{(std::min)(820 * u, io.DisplaySize.x - 32),
              (std::min)(740 * u, io.DisplaySize.y - 32)};
  ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(
      ImVec2((std::min)(540 * u, size.x), (std::min)(440 * u, size.y)),
      ImVec2(io.DisplaySize.x - 16, io.DisplaySize.y - 16));
  bool open = s.menuOpen.load();
  if (ImGui::Begin("ModActivator##Main", &open,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                       ImGuiWindowFlags_NoScrollbar)) {
    ImGui::TextColored(UI::gold, "MODACTIVATOR");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", MODACTIVATOR_VERSION);
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 38 * u);
    if (ImGui::SmallButton(" X "))
      open = false;
    ImGui::TextDisabled("In-game mod controls");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(
        (std::max)(180.0f, ImGui::GetContentRegionAvail().x - 126 * u));
    ImGui::InputTextWithHint("##Search", "Search mods or shortcuts...",
                             UI::search, sizeof UI::search);
    ImGui::SameLine();
    if (ImGui::Button("Refresh", ImVec2(112 * u, 0)))
      refreshRequested.store(true);
    std::vector<Action> actions;
    std::set<std::string> restarts;
    {
      std::lock_guard<std::mutex> lock(catalogMutex);
      actions = catalog;
      restarts = restartRequired;
    }
    orderActions(actions);
    std::map<int, std::string> tabs{{0, "Mod menus"}, {1, "Toggles"}, {2, "Hotkeys"}};
    tabs.insert(modLayout.tabs.begin(), modLayout.tabs.end());
    constexpr int settingsTab = 65536;
    tabs.emplace(settingsTab, "Settings");
    std::map<int, int> counts;
    for (const auto &a : actions)
      ++counts[actionTab(a)];
    ImGui::Spacing();
    if (ImGui::BeginTabBar("Categories",
                           ImGuiTabBarFlags_NoCloseWithMiddleMouseButton |
                               ImGuiTabBarFlags_FittingPolicyScroll)) {
      static int lastTab = -1;
      for (const auto &[tab, name] : tabs) {
        const std::string count = tab == 2 || tab == settingsTab
                                      ? "" : "  " + std::to_string(counts[tab]);
        const std::string title = name + count + "###Category" + std::to_string(tab);
        if (ImGui::BeginTabItem(title.c_str(), nullptr,
                                previewMode && previewTab == tab
                                    ? ImGuiTabItemFlags_SetSelected
                                    : 0)) {
          if (lastTab != tab) {
            UI::recording = 0;
            lastTab = tab;
          }
          if (tab == 2 || tab == settingsTab) {
            if (tab == 2)
              drawHotkeys();
            else
              drawSettings(actions);
            ImGui::EndTabItem();
            continue;
          }
          ImGui::Spacing();
          ImGui::TextDisabled(
              tab == 0 ? "Open a mod's settings with its configured shortcut."
              : tab == 1 ? "Toggle features without leaving this panel."
                         : "Manage this tab from Settings.");
          ImGui::Spacing();
          ImGui::BeginChild("ModList", ImVec2(0, -34 * u), false);
          int count = 0;
          for (size_t i = 0; i < actions.size(); ++i) {
            auto &a = actions[i];
            if (actionTab(a) != tab || !UI::matches(a))
              continue;
            ++count;
            ImGui::PushID((int)i);
            HMODULE module = previewMode ? GetModuleHandleA("kernel32.dll")
                                         : GetModuleHandleA(a.module.c_str());
            bool managed = !a.toggle && module &&
                           lower(a.module) != "modhub.asi" &&
                           GetProcAddress(module, "ModHubMenuV1") &&
                           GetModuleHandleA("ModHub.asi");
            bool restart = restarts.count(lower(a.module));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * u, 5 * u));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10 * u, 3 * u));
            // The list owns scrolling, including when the cursor is over a card.
            ImGui::BeginChild("Card", ImVec2(0, 55 * u), true,
                              ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoScrollWithMouse);
            float top = ImGui::GetCursorPosY();
            const float buttonX = ImGui::GetWindowContentRegionMax().x - 110 * u;
            const float textWidth = buttonX - ImGui::GetCursorPosX() - 8 * u;
            ImGui::PushClipRect(ImGui::GetCursorScreenPos(),
                                ImVec2(ImGui::GetCursorScreenPos().x + textWidth,
                                       ImGui::GetWindowPos().y + 55 * u), true);
            ImGui::TextUnformatted(UI::name(a).c_str());
            std::string detail = UI::shortcut(a);
            if (restart)
              detail += "  |  Restart required";
            else if (managed)
              detail += "  |  Managed by ModHub";
            else if (a.toggle)
              detail += "  |  State unavailable";
            ImGui::TextDisabled("%s", detail.c_str());
            ImGui::PopClipRect();
            ImGui::SetCursorPos(ImVec2(buttonX, top + 5 * u));
            ImGui::BeginDisabled(!module || managed || restart);
            if (ImGui::Button(a.toggle ? "Toggle" : "Open menu",
                              ImVec2(110 * u, 31 * u))) {
              if (queueAction(a) && !a.toggle)
                open = false;
            }
            ImGui::EndDisabled();
            if (ImGui::IsWindowHovered() && !ImGui::IsItemHovered())
              ImGui::SetTooltip("%s\n%s\n[%s] %s", UI::name(a).c_str(),
                                detail.c_str(), a.section.c_str(), a.key.c_str());
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::PopID();
          }
          if (!count) {
            ImGui::Spacing();
            ImGui::TextColored(UI::gold, "Nothing here yet");
            ImGui::TextWrapped(UI::search[0]
                                   ? "Try another search."
                                   : "No matching loaded mods were detected. "
                                     "Refresh after your mods have loaded.");
          }
          ImGui::EndChild();
          ImGui::Separator();
          if (tab == 1)
            ImGui::TextDisabled("Toggle status is unverified unless the mod reports it.");
          else
            ImGui::TextDisabled("%s to close", shortcutLabel(openerKeys()).c_str());
          ImGui::EndTabItem();
        }
      }
      ImGui::EndTabBar();
    }
  }
  if (!open)
    UI::recording = 0;
  s.menuOpen = open;
  ImGui::End();
}

} // namespace ml::gui
static DWORD WINAPI start(void *arg) {
  char p[MAX_PATH];
  GetModuleFileNameA(nullptr, p, MAX_PATH);
  const char *name = strrchr(p, '\\');
  if (_stricmp(name ? name + 1 : p, "CrimsonDesert.exe"))
    return 0;
  GetModuleFileNameA((HMODULE)arg, p, MAX_PATH);
  folder = std::string(p);
  folder = folder.substr(0, folder.find_last_of("\\/") + 1);
  loadLayout();
  const auto config = folder + "ModActivator.ini";
  ml::Settings::Get().wrapSwapChain =
      GetPrivateProfileIntA("General", "WrapSwapChain", 0, config.c_str()) != 0;
  ml::Settings::Get().enableDred =
      GetPrivateProfileIntA("General", "EnableDred", 0, config.c_str()) != 0;
  LOG("Mod Activator " MODACTIVATOR_VERSION " started.");
  if (MH_Initialize() != MH_OK)
    return 0;
  if (!ml::hooks::InstallDX12Hooks())
    return 0;
  if (HANDLE h = CreateThread(nullptr, 0, dispatch, nullptr, 0, nullptr))
    CloseHandle(h);
  return 0;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD why, LPVOID) {
  if (why == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(h);
    if (HANDLE t = CreateThread(nullptr, 0, start, h, 0, nullptr))
      CloseHandle(t);
  }
  return TRUE;
}
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
    if (encodeBinding(first, "", {VK_CONTROL, VK_SHIFT, VK_F6}) != "Ctrl+Shift+F6" ||
        encodeBinding(first, umlaut) != ";" ||
        replaceValue("[Menu]\nHotkey=; ; comment\n", first, "F8") !=
            "[Menu]\nHotkey=F8 ; comment\n")
      return 2;
    if (encodeBinding(second, "", {VK_CONTROL, VK_F6}) != "0x75")
      return 3;

    ModLayout test;
    test.tabs[firstCustomTab] = "Test tab";
    placementFor(test, identity(second)).tab = firstCustomTab;
    placementFor(test, identity(first)).tab = firstCustomTab;
    if (!saveLayout(std::move(test)) || !saveOpener({VK_CONTROL, VK_BACK}))
      return 4;
    opener.store(defaultOpener);
    modLayout = {};
    loadLayout();
    if (openerKeys() != std::vector<int>{VK_CONTROL, VK_BACK} ||
        modLayout.tabs[firstCustomTab] != "Test tab" ||
        actionTab(first) != firstCustomTab || actionTab(third) != 0)
      return 5;
    std::vector<Action> actions{first, second, third};
    orderActions(actions);
    if (actions.front().module != second.module || actions.back().module != third.module)
      return 6;

    if (!saveLayout({}))
      return 7;
    loadLayout();
    if (!modLayout.tabs.empty() || !modLayout.placements.empty())
      return 8;
    return 0;
  }();
  folder.clear();
  opener.store(defaultOpener);
  modLayout = {};
  refreshRequested.store(false);
  return result;
}
extern "C" __declspec(dllexport) int ModActivatorSelfTest() {
  if (sizeof(INPUT) != 40 || vk("F8") != 119 || vk("0x78") != 120 ||
      vk("DELETE") != 46 || vk("PageUp") != 33 || vk("None") != 0)
    return 1;
  return 0;
}
extern "C" __declspec(dllexport) int
ModActivatorDiscoveryTest(const char *root, const char *modules, char *out,
                          int capacity) {
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
    for (auto &a : selectActions(scanBindings(names))) {
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
ModActivatorToggleDiscoveryTest(const char *root, const char *modules,
                                char *out, int capacity) {
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
    for (auto &a : selectActions(scanBindings(names), true)) {
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
ModActivatorEditorTest(const char *root, const char *file, const char *sec,
                       const char *field, const char *input,
                       const char *expected, char *error, int size) {
  if (!folder.empty())
    return -1;
  folder = root;
  try {
    Action a{"test", "Fixture.asi", file, sec, field};
    auto value = encodeBinding(a, input);
    saveBinding(a, expected, value);
    folder.clear();
    return 0;
  } catch (const std::exception &e) {
    folder.clear();
    if (size > 0)
      snprintf(error, size, "%s", e.what());
    return 1;
  }
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
  return reads ? 3 : 0;
}

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
  struct Sample {
    const char *name;
    const char *key;
    int vk;
    bool toggle;
  };
  Sample samples[] = {
      {"InstantSurgery.asi", "ActivationKeyVK", 33, false},
      {"BodyPhysics.asi", "ActivationKeyVK", 115, false},
      {"BloodLifetime20302.asi", "ActivationKeyVK", 113, false},
      {"CrimsonRoute.asi", "ManageLocationsHotkey", 119, false},
      {"CrimsonFastLoad.asi", "Hotkey", 116, false},
      {"ModHub.asi", "ActivationKeyVK", 35, false},
      {"CalmerDynamicWind.asi", "ToggleKey", 45, true},
      {"CrimsonWeather.addon64", "HotkeyToggleEffect", 46, true}};
  for (auto &sample : samples) {
    Action a{std::filesystem::path(sample.name).stem().string() +
                 " - General / " + sample.key,
             sample.name,
             std::string(sample.name) + ".ini",
             "General",
             sample.key,
             sample.toggle};
    a.cachedKeys = {sample.vk};
    if (sample.vk == 113)
      a.cachedKeys.insert(a.cachedKeys.begin(), 17);
    a.cachedValue = std::to_string(sample.vk);
    catalog.push_back(a);
    allBindings.push_back(a);
  }
  allBindings[3].severity = 1;
  allBindings[3].overlapNames = "Another mod - Settings shortcut";
  allBindings[4].severity = 2;
  allBindings[4].overlapNames = "Another mod - Menu shortcut";
  auto *ctx = ImGui::CreateContext();
  auto &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(1000, 860);
  io.DeltaTime = 1.0f / 60;
  io.IniFilename = nullptr;
  ml::gui::InitStyle(1.0f);
  unsigned char *pixels;
  int w, h;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
  io.Fonts->SetTexID((ImTextureID)1);
  for (int i = 0; i < 4; ++i) {
    ImGui::NewFrame();
    ml::gui::Render();
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
    Action action{"Fixture", "Fixture.asi", "", "", ""};
    action.cachedKeys = {119};
    catalog.push_back(action);
  }
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
  previewMode = false;
  ImGui::DestroyContext(context);
  return result;
}
