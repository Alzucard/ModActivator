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
#include <cmath>
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
#include <string_view>
#include <vector>
static bool previewMode = false;
static int previewTab = 0;
// The plugin's folder (bin64), where the mods and their configs are.
static std::string folder;
// ModActivator's own files (its settings, your choices, the known-mods list
// and the log) are in a folder of their own inside it, so bin64 holds only
// ModActivator.asi. The plugin itself must stay in bin64 for the loader.
static constexpr char dataFolderName[] = "ModActivator";
static std::string dataPath(const char *name) {
  return folder + dataFolderName + "\\" + name;
}
// Before writing one of those files: the folder may not exist yet.
static void ensureDataFolder() {
  CreateDirectoryA((folder + dataFolderName).c_str(), nullptr);
}
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
  std::string description;
  // Where description came from (a DescriptionSource in descriptions.h), and
  // the names the description sources gave: the binding's own, and the mod's
  // with a one-line summary of it. Empty means work one out from the key.
  int descriptionSource = 0;
  std::string customName, modName, modAbout;
  // The mod writes bare key codes in hexadecimal (73 is F4).
  bool hexNumbers = false;
  // Strings the menu derives from the fields above, filled on first use and
  // reused every frame after. Empty means not computed yet. The fields they
  // come from are not changed once an Action is shown.
  struct UiCache {
    std::string identity, name, actionName, shortcut, search;
  };
  mutable UiCache ui;
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
  if (s.size() > 3 && s.compare(0, 3, "VK_") == 0)
    s.erase(0, 3);
  if (s.empty() || s == "NONE")
    return 0;
  // Other spellings mods use for the same keys. Only side-neutral and left
  // modifier names map to the generic modifier: an injected generic Ctrl,
  // Shift or Alt arrives as the left key, so a right-hand binding would not
  // be honoured and is left unrecognised instead.
  static const std::map<std::string, int> aliases{
      {"CONTROL", 17}, {"LCTRL", 17}, {"LCONTROL", 17},
      {"LSHIFT", 16},
      {"LALT", 18}, {"LMENU", 18},
      {"DEL", 46}, {"INS", 45}, {"BACK", 8}, {"PRIOR", 33}, {"NEXT", 34},
      {"LEFTARROW", 37}, {"UPARROW", 38}, {"RIGHTARROW", 39}, {"DOWNARROW", 40},
      {"PRINT", 44}, {"PRTSC", 44}, {"PRTSCN", 44}, {"SNAPSHOT", 44},
      {"CAPS", 20}, {"CAPITAL", 20}, {"SCROLL", 145}, {"NUMLOCK", 144},
      {"MULTIPLY", 106}, {"NUMPADMULTIPLY", 106}, {"NUMPAD*", 106},
      {"ADD", 107}, {"NUMPADADD", 107}, {"NUMPADPLUS", 107},
      {"SUBTRACT", 109}, {"NUMPADSUBTRACT", 109}, {"NUMPADMINUS", 109}, {"NUMPAD-", 109},
      {"DECIMAL", 110}, {"NUMPADDECIMAL", 110}, {"NUMPADPERIOD", 110}, {"NUMPAD.", 110},
      {"DIVIDE", 111}, {"NUMPADDIVIDE", 111}, {"NUMPAD/", 111}};
  if (const auto alias = aliases.find(s); alias != aliases.end())
    return alias->second;
  if (s.size() == 4 && s.compare(0, 3, "NUM") == 0 && isdigit((unsigned char)s[3]))
    return 96 + s[3] - '0';
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
  // A mod that writes its codes in hex without the 0x: 73 is F4, not I.
  // A lone letter is still that letter's key.
  if (a.hexNumbers && !s.empty() && s.size() <= 2 && isdigit((unsigned char)s[0]) &&
      s.find_first_not_of("0123456789abcdefABCDEF") == s.npos)
    s = "0x" + s;
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
  // A modifier both required by a RequireX flag and typed in the value
  // (Hotkey=Ctrl+F5 with RequireCtrl=1) is one key, not two. A modifier
  // repeated within the value itself is still rejected by validShortcut.
  const size_t required = out.size();
  for (int k : parseShortcut(s, layout, mainScan, standaloneDigit(a, s)))
    if (std::find(out.begin(), out.begin() + required, k) == out.begin() + required)
      out.push_back(k);
  return out;
}
namespace ml::Log {
// One file handle for the whole run, opened by Begin() once the plugin folder
// is known. Lines are flushed as written so nothing is lost if the game dies.
static std::mutex logMutex;
static FILE *logFile = nullptr;
// Starts a fresh log each launch and keeps the previous one as
// ModActivator.log.old, so the file no longer grows forever (issue #5).
static void Begin() {
  std::lock_guard<std::mutex> lock(logMutex);
  if (logFile)
    return;
  // Without a usable plugin folder, the working directory instead.
  if (!folder.empty())
    ensureDataFolder();
  const auto path = folder.empty() ? std::string("ModActivator.log") : dataPath("ModActivator.log");
  MoveFileExA(path.c_str(), (path + ".old").c_str(), MOVEFILE_REPLACE_EXISTING);
  logFile = fopen(path.c_str(), "w");
}
void Write(const char *level, const char *fmt, ...) {
  char b[2048];
  va_list a;
  va_start(a, fmt);
  vsnprintf(b, sizeof b, fmt, a);
  va_end(a);
  SYSTEMTIME t;
  GetLocalTime(&t);
  std::lock_guard<std::mutex> lock(logMutex);
  if (!logFile)
    return;
  fprintf(logFile, "%02u:%02u:%02u.%03u [%s] %s\n", t.wHour, t.wMinute,
          t.wSecond, t.wMilliseconds, level, b);
  fflush(logFile);
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
static float clampScale(float scale) { return (std::max)(0.8f, (std::min)(1.7f, scale)); }
// Set by SetDisplayHeight when the swapchain's height implies another UI
// scale; applied, with the fonts, by RebuildFonts between frames.
static float pendingScale = 0;
// The menu window was sized for the old scale; size it again once.
static bool resizeWindow = false;

static void ApplyStyle(float scale) {
  UI::scale = clampScale(scale);
  // From defaults every time: ScaleAllSizes multiplies what is there, so
  // rescaling the previous style would compound.
  ImGui::GetStyle() = ImGuiStyle();
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
}

// Windows fonts that cover scripts Segoe UI does not. Only the first one
// present in a group is loaded, and only when the menu has text in it: a CJK
// font is tens of megabytes. The CJK group follows the Windows display
// language, so kanji look Japanese to a Japanese reader and hanzi Chinese.
struct FallbackGroup {
  std::vector<const char *> files;
  std::vector<std::pair<unsigned, unsigned>> blocks;
};
static std::vector<FallbackGroup> fallbackGroups() {
  const LANGID ui = GetUserDefaultUILanguage();
  std::vector<const char *> cjk{"msyh.ttc", "msjh.ttc", "YuGothM.ttc", "meiryo.ttc", "malgun.ttf"};
  if (PRIMARYLANGID(ui) == LANG_JAPANESE)
    cjk = {"YuGothM.ttc", "meiryo.ttc", "msyh.ttc", "msjh.ttc"};
  else if (PRIMARYLANGID(ui) == LANG_CHINESE && SUBLANGID(ui) != SUBLANG_CHINESE_SIMPLIFIED &&
           SUBLANGID(ui) != SUBLANG_CHINESE_SINGAPORE)
    cjk = {"msjh.ttc", "msyh.ttc", "YuGothM.ttc"};
  else if (PRIMARYLANGID(ui) == LANG_KOREAN)
    cjk = {"malgun.ttf", "msyh.ttc", "YuGothM.ttc"};
  return {
      {cjk, {{0x2E80, 0x9FFF}, {0xF900, 0xFAFF}, {0xFE30, 0xFE4F}, {0xFF00, 0xFFEF}}},
      {{"malgun.ttf", "gulim.ttc"}, {{0x1100, 0x11FF}, {0x3130, 0x318F}, {0xA960, 0xA97F}, {0xAC00, 0xD7FF}}},
      {{"LeelawUI.ttf", "tahoma.ttf"}, {{0x0E00, 0x0EFF}}},
      {{"Nirmala.ttf"}, {{0x0900, 0x0DFF}}},
      {{"seguisym.ttf"}, {{0x2190, 0x2BFF}}},
  };
}

static void BuildFonts() {
  // Glyph ranges are read when the atlas is built, which is after this
  // returns (the backend builds it on the next NewFrame), so they live here.
  static ImVector<ImWchar> mainRanges, extraRanges;
  auto &io = ImGui::GetIO();
  io.Fonts->Clear();
  static const ImWchar base[] = {0x20, 0x024F, 0x0370, 0x052F, 0x2000, 0x206F, 0};
  ImFontGlyphRangesBuilder builder;
  builder.AddRanges(base);
  for (const ImWchar c : UI::extraGlyphs)
    builder.AddChar(c);
  mainRanges.clear();
  builder.BuildRanges(&mainRanges);

  wchar_t windows[MAX_PATH]{};
  GetWindowsDirectoryW(windows, MAX_PATH);
  // ImGui opens font files from UTF-8 names.
  const auto fontPath = [&](const char *file) {
    const std::wstring path = std::wstring(windows) + L"\\Fonts\\" +
                              std::wstring(file, file + strlen(file));
    return GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES
               ? std::string()
               : narrowText(path.c_str(), (int)path.size(), CP_UTF8);
  };
  const float size = 17.0f * UI::scale;
  const auto segoe = fontPath("segoeui.ttf");
  if (segoe.empty() || !io.Fonts->AddFontFromFileTTF(segoe.c_str(), size, nullptr, mainRanges.Data)) {
    ImFontConfig fallback;
    fallback.SizePixels = size;
    io.Fonts->AddFontDefault(&fallback);
  }
  if (UI::extraGlyphs.empty())
    return;

  ImFontGlyphRangesBuilder extra;
  for (const ImWchar c : UI::extraGlyphs)
    extra.AddChar(c);
  extraRanges.clear();
  extra.BuildRanges(&extraRanges);
  // Merged fonts only add what the fonts before them lack, so Segoe UI keeps
  // every glyph it has and each fallback fills in its own script.
  for (const auto &group : fallbackGroups()) {
    const bool needed = std::any_of(UI::extraGlyphs.begin(), UI::extraGlyphs.end(), [&](ImWchar c) {
      return std::any_of(group.blocks.begin(), group.blocks.end(),
                         [&](const auto &block) { return c >= block.first && c <= block.second; });
    });
    if (!needed)
      continue;
    for (const char *file : group.files) {
      const auto path = fontPath(file);
      if (path.empty())
        continue;
      ImFontConfig merge;
      merge.MergeMode = true;
      merge.OversampleH = 1; // dense scripts: half the atlas, no visible cost at this size
      if (io.Fonts->AddFontFromFileTTF(path.c_str(), size, &merge, extraRanges.Data))
        break;
    }
  }
}

void InitStyle(float scale) {
  ApplyStyle(scale);
  ImGui::GetIO().IniFilename = nullptr;
  BuildFonts();
  UI::glyphsChanged = false;
  pendingScale = 0;
}
void SetDisplayHeight(float height) {
  if (height <= 0)
    return;
  const float scale = clampScale(height / 1080.0f);
  pendingScale = fabsf(scale - UI::scale) > 0.01f ? scale : 0;
}
bool FontsNeedRebuild() { return UI::glyphsChanged || pendingScale > 0; }
// Between frames, after the backend has released its font texture.
void RebuildFonts() {
  if (pendingScale > 0) {
    LOG("[overlay] UI scale %.2f -> %.2f for the new resolution.", UI::scale, pendingScale);
    ApplyStyle(pendingScale);
    pendingScale = 0;
    resizeWindow = true;
  }
  if (UI::glyphsChanged)
    LOG("[overlay] font atlas rebuilt for %zu characters outside Latin, Greek and Cyrillic.",
        UI::extraGlyphs.size());
  BuildFonts();
  UI::glyphsChanged = false;
}
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
    input::FeedInput(io);
}
// The menu's copy of the cards, taken when the catalog or the card order
// changes rather than every frame, with what each card needs worked out once:
// which tab it is on, and whether its module is loaded, managed by ModHub or
// waiting for a restart. Render thread only.
struct CardState {
  int tab = 0;
  bool loaded = false, managed = false, restart = false;
};
struct MenuSnapshot {
  unsigned catalog = 0, layout = 0, stamp = 0;
  std::vector<Action> actions;
  std::vector<CardState> cards;
  std::map<int, int> counts;
};
static const MenuSnapshot &menuSnapshot() {
  static MenuSnapshot snap;
  const unsigned catalogNow = catalogVersion.load(), layoutNow = layoutVersion.load();
  if (snap.catalog == catalogNow && snap.layout == layoutNow)
    return snap;
  std::set<std::string> restarts;
  {
    std::lock_guard<std::mutex> lock(catalogMutex);
    snap.actions = catalog;
    restarts = restartRequired;
  }
  // Fresh from the catalog on a layout change too: re-sorting the previous
  // order would keep its leftovers.
  orderActions(snap.actions);
  const HMODULE modHub = GetModuleHandleA("ModHub.asi");
  snap.cards.clear();
  snap.counts.clear();
  for (const auto &a : snap.actions) {
    CardState card;
    card.tab = actionTab(a);
    const HMODULE module = previewMode ? GetModuleHandleA("kernel32.dll")
                                       : GetModuleHandleA(a.module.c_str());
    card.loaded = module != nullptr;
    card.managed = !a.toggle && module && modHub && lower(a.module) != "modhub.asi" &&
                   GetProcAddress(module, "ModHubMenuV1");
    card.restart = restarts.count(lower(a.module)) != 0;
    snap.cards.push_back(card);
    ++snap.counts[card.tab];
    UI::noteGlyphs(a);
  }
  for (const auto &[id, name] : modLayout.tabs)
    UI::noteGlyphs(name);
  snap.catalog = catalogNow;
  snap.layout = layoutNow;
  ++snap.stamp;
  return snap;
}

void Render() {
  auto &s = State::Get();
  auto &io = ImGui::GetIO();
  float u = UI::scale;
  ImVec2 size{(std::min)(820 * u, io.DisplaySize.x - 32),
              (std::min)(740 * u, io.DisplaySize.y - 32)};
  ImGui::SetNextWindowSize(size, resizeWindow ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
  resizeWindow = false;
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
    if (ImGui::InputTextWithHint("##Search", "Search mods or shortcuts...",
                                 UI::search, sizeof UI::search))
      UI::noteGlyphs(std::string(UI::search)); // typed text can be in any script
    ImGui::SameLine();
    if (ImGui::Button("Refresh", ImVec2(112 * u, 0)))
      refreshRequested.store(true);
    const auto &snap = menuSnapshot();
    const auto &actions = snap.actions;
    std::map<int, std::string> tabs{{0, "Mod menus"}, {1, "Toggles"}, {2, "Hotkeys"}};
    tabs.insert(modLayout.tabs.begin(), modLayout.tabs.end());
    constexpr int settingsTab = 65536;
    tabs.emplace(settingsTab, "Settings");
    const auto &counts = snap.counts;
    ImGui::Spacing();
    if (ImGui::BeginTabBar("Categories",
                           ImGuiTabBarFlags_NoCloseWithMiddleMouseButton |
                               ImGuiTabBarFlags_FittingPolicyScroll)) {
      static int lastTab = -1;
      for (const auto &[tab, name] : tabs) {
        const auto counted = counts.find(tab);
        const std::string count = tab == 2 || tab == settingsTab
                                      ? "" : "  " + std::to_string(counted == counts.end() ? 0 : counted->second);
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
              drawSettings(actions, snap.stamp);
            ImGui::EndTabItem();
            continue;
          }
          ImGui::Spacing();
          ImGui::TextDisabled("Drag the handle on a mod to reorder it.");
          ImGui::Spacing();
          ImGui::BeginChild("ModList", ImVec2(0, -34 * u), false);
          int count = 0;
          PlacementDrop drop;
          static bool reorderFailed = false;
          for (size_t i = 0; i < actions.size(); ++i) {
            const auto &a = actions[i];
            const auto &card = snap.cards[i];
            if (card.tab != tab || !UI::matches(a))
              continue;
            ++count;
            ImGui::PushID(cachedIdentity(a).c_str());
            const bool managed = card.managed, restart = card.restart;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * u, 5 * u));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10 * u, 3 * u));
            // The list owns scrolling, including when the cursor is over a card.
            ImGui::BeginChild("Card", ImVec2(0, 55 * u), true,
                              ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoScrollWithMouse);
            const float top = ImGui::GetCursorPosY();
            const float handleX = ImGui::GetCursorPosX();
            ImGui::SetCursorPosY(top + 5 * u);
            dragHandle(a, 31 * u);
            const bool overHandle = ImGui::IsItemHovered();
            const float labelX = handleX + 41 * u;
            ImGui::SetCursorPos(ImVec2(labelX, top));
            const float buttonX = ImGui::GetWindowContentRegionMax().x - 110 * u;
            const float textWidth = buttonX - ImGui::GetCursorPosX() - 8 * u;
            ImGui::PushClipRect(ImGui::GetCursorScreenPos(),
                                ImVec2(ImGui::GetCursorScreenPos().x + textWidth,
                                       ImGui::GetWindowPos().y + 55 * u), true);
            ImGui::TextUnformatted(UI::name(a).c_str());
            ImGui::SetCursorPosX(labelX);
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
            ImGui::BeginDisabled(!card.loaded || managed || restart);
            if (ImGui::Button(a.toggle ? "Toggle" : "Open menu",
                              ImVec2(110 * u, 31 * u))) {
              if (queueAction(a) && !a.toggle)
                open = false;
            }
            ImGui::EndDisabled();
            if (!overHandle && !ImGui::GetDragDropPayload() &&
                ImGui::IsWindowHovered() && !ImGui::IsItemHovered())
              UI::bindingTooltip(a, detail);
            ImGui::EndChild();
            dropOnCard(a, drop);
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
          if (!drop.source.empty())
            reorderFailed = !reorderActions(actions, drop);
          ImGui::Separator();
          if (reorderFailed)
            ImGui::TextDisabled("Could not save mod order.");
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
// The folder in the ANSI code page, since every config is read through the
// ANSI profile API. A folder name the code page cannot spell falls back to
// its 8.3 short name; empty when neither works.
static std::string ansiFolder(const std::wstring &wide) {
  const auto convert = [](const std::wstring &w) -> std::string {
    BOOL lossy = FALSE;
    const int n = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w.c_str(), -1,
                                      nullptr, 0, nullptr, &lossy);
    if (n <= 1 || lossy)
      return {};
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w.c_str(), -1, s.data(), n,
                        nullptr, nullptr);
    return s;
  };
  auto result = convert(wide);
  if (result.empty()) {
    wchar_t shortPath[MAX_PATH]{};
    const DWORD n = GetShortPathNameW(wide.c_str(), shortPath, MAX_PATH);
    if (n && n < MAX_PATH)
      result = convert(shortPath);
  }
  if (!result.empty() && result.back() != '\\' && result.back() != '/')
    result += '\\';
  return result;
}
static DWORD WINAPI start(void *arg) {
  char p[MAX_PATH];
  GetModuleFileNameA(nullptr, p, MAX_PATH);
  const char *name = strrchr(p, '\\');
  if (_stricmp(name ? name + 1 : p, "CrimsonDesert.exe"))
    return 0;
  wchar_t self[MAX_PATH]{};
  const DWORD selfLength = GetModuleFileNameW((HMODULE)arg, self, MAX_PATH);
  std::wstring selfFolder(self, selfLength < MAX_PATH ? selfLength : 0);
  selfFolder = selfFolder.substr(0, selfFolder.find_last_of(L"\\/") + 1);
  folder = ansiFolder(selfFolder);
  if (folder.empty()) {
    // Nothing can be read or written beside the plugin. The log goes to the
    // working directory instead so the reason is on record.
    ml::Log::Begin();
    LOG_ERR("Mod Activator " MODACTIVATOR_VERSION " cannot use its folder: the path "
            "contains characters outside the system code page and has no short name. "
            "Move the game to a path with plain characters, or enable 8.3 names on that drive.");
    return 0;
  }
  // Files an older version left in bin64 move into the ModActivator folder
  // first, the log among them, so the log opens after and records the move.
  const auto moved = moveIntoDataFolder();
  ml::Log::Begin();
  LOG("Mod Activator " MODACTIVATOR_VERSION " started.");
  for (const auto &line : moved)
    LOG("%s", line.c_str());
  loadLayout();
  const auto config = dataPath("ModActivator.ini");
  ml::Settings::Get().wrapSwapChain =
      GetPrivateProfileIntA("General", "WrapSwapChain", 0, config.c_str()) != 0;
  ml::Settings::Get().enableDred =
      GetPrivateProfileIntA("General", "EnableDred", 0, config.c_str()) != 0;
  ml::Settings::Get().debugInput =
      GetPrivateProfileIntA("General", "DebugInput", 0, config.c_str()) != 0;
  ml::Settings::Get().hookXInput =
      GetPrivateProfileIntA("General", "HookXInput", 1, config.c_str()) != 0;
  if (const auto status = MH_Initialize(); status != MH_OK) {
    LOG_ERR("MinHook failed to initialise (%s). No hooks were installed, so the menu cannot open.",
            MH_StatusToString(status));
    return 0;
  }
  const bool useReShadeEvents =
      GetPrivateProfileIntA("General", "UseReShadeEvents", 1, config.c_str()) != 0;
  if (!ml::hooks::InstallDX12Hooks((HMODULE)arg, useReShadeEvents)) {
    LOG_ERR("The DirectX hooks could not be installed, so the menu cannot open.");
    return 0;
  }
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
// --- test entry points ----------------------------------------------------
// Called by the suites in tests/ through build/ModActivator-tests.asi. Only
// compiled with MODACTIVATOR_TESTS, so the shipped plugin exports none of them.
#ifdef MODACTIVATOR_TESTS
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
// The move of the menu's choices out of ModActivator.ini, and of every file
// out of bin64 into the ModActivator folder. root is an empty folder; each
// case writes its own ModActivator.ini.
extern "C" __declspec(dllexport) int ModActivatorMigrationTest(const char *root) {
  if (!folder.empty() || !root)
    return -1;
  folder = root;
  ensureDataFolder();
  const auto old = dataPath("ModActivator.ini"), user = userConfigPath();
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

    // A version from before the folder: everything sits in bin64, its
    // settings file holds your options and your old menu choices.
    for (const char *name : {"ModActivator.ini", "ModActivator.User.ini", "ModActivator.log"})
      DeleteFileA(dataPath(name).c_str());
    const auto rootIni = folder + "ModActivator.ini";
    std::ofstream(rootIni) << "[General]\nHookXInput=0\n[BindingTypes]\nmod.ini|menu|hotkey=2\n";
    std::ofstream(folder + "ModActivator.log") << "old log\n";
    auto moved = moveIntoDataFolder();
    if (exists(rootIni) || exists(folder + "ModActivator.log") || !exists(dataPath("ModActivator.log")) ||
        GetPrivateProfileIntA("General", "HookXInput", 1, old.c_str()) != 0 ||
        readBindingType(probe) != 2 || moved.size() != 3)
      return 8;
    // The update shipped a fresh ModActivator.ini into the folder while the
    // old one is still in bin64: your options carry over into the new one,
    // its comments stay, and the old one is renamed so this happens once.
    std::ofstream(old) << "[General]\n; shipped comment\nHookXInput=1\nUseReShadeEvents=1\n";
    std::ofstream(rootIni) << "[General]\nHookXInput=0\n";
    moveIntoDataFolder();
    std::ifstream fresh(old);
    const std::string text(std::istreambuf_iterator<char>(fresh), {});
    fresh.close();
    if (exists(rootIni) || !exists(rootIni + ".moved") ||
        GetPrivateProfileIntA("General", "HookXInput", 1, old.c_str()) != 0 ||
        GetPrivateProfileIntA("General", "UseReShadeEvents", 0, old.c_str()) != 1 ||
        text.find("; shipped comment") == text.npos)
      return 9;
    WritePrivateProfileStringA("General", "HookXInput", "1", old.c_str());
    if (!moveIntoDataFolder().empty() ||
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
    std::ofstream(folder + "Hex.ini") << "[Hotkeys]\nPrivateHotkey=73\n";
    if (encodeBinding(hex, "F6") != "75" || encodeBinding(hex, "", {VK_F7}) != "76") {
      folder.clear();
      return -5;
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
// The test build describes one of its own fixture bindings, so the mod
// export is read end to end: tests/test_descriptions.py puts a config named
// after this DLL beside the fixtures.
extern "C" __declspec(dllexport) const ModActivatorBindingV1 *ModActivatorBindingsV1(unsigned *count) {
  static const ModActivatorBindingV1 bindings[] = {
      {"ModActivator-tests.ini", "Hotkeys", "SecretKey", "Exported name",
       "Described by the mod itself.", ModActivatorKindToggle},
      {nullptr, nullptr, "", "ignored", "an entry without a key is skipped", ModActivatorKindMenu},
  };
  *count = sizeof bindings / sizeof bindings[0];
  return bindings;
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
  catalogChanged();
  ++layoutVersion;
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
    io.AddMousePosEvent(source.x + 24, source.y + 25);
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
                    io.Fonts->ConfigData[0].SizePixels != 17.0f * 1.7f || ml::gui::FontsNeedRebuild()))
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
#endif // MODACTIVATOR_TESTS
