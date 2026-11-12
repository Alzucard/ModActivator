#include "core/log.h"
#include "core/settings.h"
#include "core/state.h"
#include "gui/menu.h"
#include "hooks/dx12_hook.h"
#include "hooks/input.h"
#include "hooks/xinput_hook.h"
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
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
static bool previewMode = false;
static int previewTab = 0;
// The plugin's folder (bin64), where the mods and their configs are.
static std::string folder;
// ModActivator's INI files (its settings, your choices and the known-mods
// list) sit beside the plugin in bin64: DMM installs the files there but not
// a release's subfolders. What ModActivator writes for itself, the log, goes
// into a ModActivator folder inside bin64, created when needed.
static std::string configPath(const char *name) { return folder + name; }
// WrapSwapChain's value: 1 always hands the game's swapchain the wrapper, 0
// never, -1 auto. A number counts by being zero or not, with anything after
// it ignored, as the profile API's integer reading did; true/on/yes and
// false/off/no as well. Anything else, or nothing, is auto.
static int wrapValue(const char *text) {
  while (*text == ' ' || *text == '\t')
    ++text;
  char *end = nullptr;
  const long number = strtol(text, &end, 10);
  if (end != text)
    return number != 0;
  std::string word;
  for (; isalpha((unsigned char)*text); ++text)
    word += (char)tolower((unsigned char)*text);
  if (word == "true" || word == "on" || word == "yes")
    return 1;
  if (word == "false" || word == "off" || word == "no")
    return 0;
  return -1;
}
// WrapSwapChain from the settings file (see wrapValue above). Auto, the default,
// leaves it to the swapchain hook, which wraps when DLSS frame generation's
// Streamline layer is loaded: there the present hook draws on frames the
// menu cannot appear on.
static int wrapSetting(const std::string &config) {
  char value[64]{};
  GetPrivateProfileStringA("General", "WrapSwapChain", "auto", value, sizeof value, config.c_str());
  return wrapValue(value);
}
static constexpr char dataFolderName[] = "ModActivator";
static std::string dataPath(const char *name) {
  return folder + dataFolderName + "\\" + name;
}
// Before writing into the folder: it may not exist yet.
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
  // The kind (menu, toggle, action) was chosen, by you in Settings, by the mod
  // or by the known-mods list, rather than guessed. Every chosen menu and
  // toggle gets a card of its own.
  bool chosenKind = false;
  std::string description;
  // Where description came from (a DescriptionSource in descriptions.h), and
  // the names the description sources gave: the binding's own, and the mod's
  // with a one-line summary of it. Empty means work one out from the key.
  int descriptionSource = 0;
  std::string customName, modName, modAbout;
  // The mod writes bare key codes in hexadecimal (73 is F4).
  bool hexNumbers = false;
  // Modifiers the mod requires that its value does not say, from the
  // known-mods list (see ruleModifiers): "Ctrl", "Ctrl?HotkeyCtrl",
  // "=OpenKey2WithVk". Empty for none.
  std::string modifierRule;
  // The controller combination for the same action (pad.h bits), and the
  // setting it was read from: the value's own |suffix, or a setting beside it.
  unsigned padButtons = 0;
  std::string padField;
  // How the mod takes a changed config without a restart, from the
  // known-mods list's @reload: "live", or the key that makes it reread it.
  // Empty: only at the next start.
  std::string reload;
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
#include "pad.h"
static bool queueAction(const Action &a, bool closeMenu);
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
// The panel's controller shortcut (pad.h bits), 0 for none. LB + RB + Back by
// default: hold both bumpers, then press Back, which the game then never sees.
static constexpr unsigned defaultPadOpener =
    XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_BACK;
static std::atomic<unsigned> padOpener{defaultPadOpener};
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
// A bare hex key code, for a mod that writes them so: one digit, or two hex
// digits other than F followed by a digit.
static bool isHexCode(const std::string &s) {
  const auto hex = [](char c) { return isxdigit(static_cast<unsigned char>(c)) != 0; };
  if (s.size() == 1)
    return isdigit(static_cast<unsigned char>(s[0])) != 0;
  return s.size() == 2 && hex(s[0]) && hex(s[1]) &&
         !(toupper(static_cast<unsigned char>(s[0])) == 'F' && isdigit(static_cast<unsigned char>(s[1])));
}
// The modifiers a.modifierRule adds. Its words, separated by spaces or
// commas:
//   Ctrl, Shift, Alt      always that modifier
//   Ctrl?Field            Ctrl when Field, in the binding's section, is not 0
//   =Field                the modifier Field names: ALT, Ctrl, 18, 0x12...
static std::vector<int> ruleModifiers(const Action &a) {
  std::vector<int> out;
  const auto config = folder + a.file;
  const auto add = [&](int k) {
    if ((k == VK_CONTROL || k == VK_SHIFT || k == VK_MENU) &&
        std::find(out.begin(), out.end(), k) == out.end())
      out.push_back(k);
  };
  std::string rule = a.modifierRule;
  std::replace(rule.begin(), rule.end(), ',', ' ');
  std::istringstream words(rule);
  for (std::string word; words >> word;) {
    if (word[0] == '=') {
      char value[64]{};
      GetPrivateProfileStringA(a.section.c_str(), word.c_str() + 1, "", value, sizeof value,
                               config.c_str());
      add(vk(value));
    } else if (const auto query = word.find('?'); query != word.npos) {
      if (GetPrivateProfileIntA(a.section.c_str(), word.c_str() + query + 1, 0, config.c_str()))
        add(vk(word.substr(0, query)));
    } else {
      add(vk(word));
    }
  }
  return out;
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
  // A mod that writes its codes in hex without the 0x: 73 is F4, not I, and
  // BA is the ; key. A lone letter is still that letter's key, and F plus a
  // digit is still that F key (0xF0 to 0xF9 are no keys anyone binds).
  if (a.hexNumbers && isHexCode(s))
    s = "0x" + s;
  std::vector<int> out;
  for (auto pair : {std::pair<const char *, int>{"RequireCtrl", 17},
                    {"RequireShift", 16},
                    {"RequireAlt", 18}})
    if (GetPrivateProfileIntA(a.section.c_str(), pair.first, 0,
                              (folder + a.file).c_str()))
      out.push_back(pair.second);
  for (int k : ruleModifiers(a))
    if (std::find(out.begin(), out.end(), k) == out.end())
      out.push_back(k);
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
// Sends a binding's shortcut once the panel is out of the way. closeMenu for
// one that opens a window of its own, which needs the keyboard and mouse:
// a mod menu card. Toggles and plain actions keep the panel open.
static bool queueAction(const Action &a, bool closeMenu) {
  std::lock_guard<std::mutex> lock(catalogMutex);
  constexpr size_t queueLimit = 8;
  if (pendingActions.size() >= queueLimit || restartRequired.count(lower(a.module)))
    return false;
  pendingActions.push_back(a);
  if (closeMenu)
    ml::State::Get().menuOpen = false;
  return true;
}
static bool queueAction(const Action &a) { return queueAction(a, !a.toggle); }
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
// The controller, read once a frame by PollToggle for the panel's controller
// shortcut and handed to the menu by FeedInput. Render thread only.
static XINPUT_GAMEPAD framePad{};
static bool framePadConnected = false;
// LB or RB pressed and released on its own while the menu is open: the
// previous or the next tab. Used up by Render.
static int padTabStep = 0;

static void readController(bool menuOpen) {
  const unsigned shortcut = padOpener.load();
  framePadConnected = false;
  if (!shortcut && !menuOpen)
    return; // nothing uses the controller: leave XInput alone
  // In before the menu is first opened, so the shortcut's last button is
  // kept from the game from the start.
  if (shortcut)
    hooks::EnsureXInputHooks();
  framePadConnected = hooks::ReadPadRaw(&framePad);
  const unsigned held = framePadConnected ? padButtonsHeld(framePad) : 0;
  static unsigned before = 0, alone = 0;
  constexpr unsigned lb = XINPUT_GAMEPAD_LEFT_SHOULDER, rb = XINPUT_GAMEPAD_RIGHT_SHOULDER;
  const unsigned pressed = held & ~before;
  if (!before && (pressed == lb || pressed == rb))
    alone = pressed;
  else if (held & ~alone)
    alone = 0; // another button joined in: a combination, not a tab step
  if (alone && !held) {
    if (menuOpen)
      padTabStep = alone == lb ? -1 : 1;
    alone = 0;
  }
  before = held;
}

// Each opening and closing, and what did it, so a menu that opens and
// closes by itself shows who is pressing. The first 40 of a session are
// written, then every 50th with the count.
static void logToggle(bool opened, const char *cause, unsigned buttons = 0) {
  static unsigned count = 0;
  if (++count > 40 && count % 50)
    return;
  char held[40] = "";
  if (buttons)
    snprintf(held, sizeof held, " (buttons held 0x%X)", buttons);
  char nth[48] = "";
  if (count > 40)
    snprintf(nth, sizeof nth, " - toggle %u this session", count);
  LOG("[menu] %s by %s%s%s", opened ? "opened" : "closed", cause, held, nth);
}
void PollToggle() {
  static bool was = false;
  static bool escapeWas = false;
  static bool padWas = false;
  const auto code = opener.load();
  auto &s = State::Get();
  readController(s.menuOpen);
  const unsigned padCode = padOpener.load();
  const bool padNow = padCode && framePadConnected &&
                      (padButtonsHeld(framePad) & padCode) == padCode;
  const bool now = (held(code & 0xff) && heldModifiers() == (code & ~0xffu) &&
                    !held(VK_LWIN) && !held(VK_RWIN)) ||
                   padNow;
  const bool padEdge = padNow && !padWas;
  padWas = padNow;
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
    const bool acceptsShortcut = !UI::recording && !padShortcutRecording && !textEditing &&
                                 !s.sendingShortcut;
    if (((now && !was) || padEdge) && acceptsShortcut) {
      s.menuOpen = !s.menuOpen;
      if (padEdge)
        logToggle(s.menuOpen, "the controller shortcut", padButtonsHeld(framePad));
      else
        logToggle(s.menuOpen, "the keyboard shortcut");
      if (s.menuOpen)
        refreshRequested.store(true);
    }
    const bool escape = held(VK_ESCAPE);
    if (s.menuOpen && escape && !escapeWas && acceptsShortcut) {
      s.menuOpen = false;
      logToggle(false, "Escape");
    }
    escapeWas = escape;
  } else {
    if (s.menuOpen)
      logToggle(false, "the game window losing focus");
    s.menuOpen = false;
    UI::recording = 0;
    escapeWas = held(VK_ESCAPE);
    std::lock_guard<std::mutex> lock(catalogMutex);
    pendingActions.clear();
  }
  was = now;
}
bool WantsDraw() { return State::Get().menuOpen; }
// The controller as Dear ImGui's gamepad navigation: the d-pad or left stick
// moves between items, A presses, B backs out, Y and X as ImGui assigns them.
// The Win32 backend's own gamepad reading is compiled out
// (IMGUI_IMPL_WIN32_DISABLE_GAMEPAD), since its XInput calls come through
// ModActivator's detour, which hands everyone a neutral pad while the menu is
// open; this feeds the real one instead.
static void feedController(ImGuiIO &io, bool use) {
  static bool fed = false;
  if (!use) {
    if (fed) {
      // Let go of everything once, so nothing stays held.
      for (int k = ImGuiKey_GamepadStart; k <= ImGuiKey_GamepadRStickDown; ++k)
        io.AddKeyAnalogEvent(static_cast<ImGuiKey>(k), false, 0.0f);
      io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
      fed = false;
    }
    return;
  }
  fed = true;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  const auto &pad = framePad;
  const auto button = [&](ImGuiKey key, WORD bit) { io.AddKeyEvent(key, (pad.wButtons & bit) != 0); };
  button(ImGuiKey_GamepadStart, XINPUT_GAMEPAD_START);
  button(ImGuiKey_GamepadBack, XINPUT_GAMEPAD_BACK);
  button(ImGuiKey_GamepadFaceLeft, XINPUT_GAMEPAD_X);
  button(ImGuiKey_GamepadFaceRight, XINPUT_GAMEPAD_B);
  button(ImGuiKey_GamepadFaceUp, XINPUT_GAMEPAD_Y);
  button(ImGuiKey_GamepadFaceDown, XINPUT_GAMEPAD_A);
  button(ImGuiKey_GamepadDpadLeft, XINPUT_GAMEPAD_DPAD_LEFT);
  button(ImGuiKey_GamepadDpadRight, XINPUT_GAMEPAD_DPAD_RIGHT);
  button(ImGuiKey_GamepadDpadUp, XINPUT_GAMEPAD_DPAD_UP);
  button(ImGuiKey_GamepadDpadDown, XINPUT_GAMEPAD_DPAD_DOWN);
  button(ImGuiKey_GamepadL1, XINPUT_GAMEPAD_LEFT_SHOULDER);
  button(ImGuiKey_GamepadR1, XINPUT_GAMEPAD_RIGHT_SHOULDER);
  button(ImGuiKey_GamepadL3, XINPUT_GAMEPAD_LEFT_THUMB);
  button(ImGuiKey_GamepadR3, XINPUT_GAMEPAD_RIGHT_THUMB);
  const auto analog = [&](ImGuiKey key, float value, float dead, float full) {
    const float v = (std::min)(1.0f, (std::max)(0.0f, (value - dead) / (full - dead)));
    io.AddKeyAnalogEvent(key, v > 0.1f, v);
  };
  analog(ImGuiKey_GamepadL2, pad.bLeftTrigger, XINPUT_GAMEPAD_TRIGGER_THRESHOLD, 255);
  analog(ImGuiKey_GamepadR2, pad.bRightTrigger, XINPUT_GAMEPAD_TRIGGER_THRESHOLD, 255);
  constexpr float dead = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, full = 32767;
  analog(ImGuiKey_GamepadLStickLeft, -pad.sThumbLX, dead, full);
  analog(ImGuiKey_GamepadLStickRight, pad.sThumbLX, dead, full);
  analog(ImGuiKey_GamepadLStickUp, pad.sThumbLY, dead, full);
  analog(ImGuiKey_GamepadLStickDown, -pad.sThumbLY, dead, full);
  analog(ImGuiKey_GamepadRStickLeft, -pad.sThumbRX, dead, full);
  analog(ImGuiKey_GamepadRStickRight, pad.sThumbRX, dead, full);
  analog(ImGuiKey_GamepadRStickUp, pad.sThumbRY, dead, full);
  analog(ImGuiKey_GamepadRStickDown, -pad.sThumbRY, dead, full);
}
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
  feedController(io, c && framePadConnected);
}
// The menu's copy of the cards, taken when the catalog or the card order
// changes rather than every frame, with what each card needs worked out once:
// which tab it is on, and whether its module is loaded, managed by ModHub or
// waiting for a restart. Render thread only.
struct CardState {
  int tab = 0;
  bool loaded = false, managed = false, restart = false;
  // The mod has other cards too, so this one is titled with its action.
  bool several = false;
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
  std::map<std::string, int> perModule;
  for (const auto &a : snap.actions)
    ++perModule[lower(a.module)];
  for (size_t i = 0; i < snap.actions.size(); ++i)
    snap.cards[i].several = perModule[lower(snap.actions[i].module)] > 1;
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
      // LB and RB on their own step through the tabs.
      int padTab = -1;
      if (padTabStep && !tabs.empty()) {
        std::vector<int> order;
        for (const auto &[id, name] : tabs)
          order.push_back(id);
        const auto at = std::find(order.begin(), order.end(), lastTab);
        const int index = at == order.end() ? 0 : int(at - order.begin());
        padTab = order[(index + padTabStep + int(order.size())) % int(order.size())];
        padTabStep = 0;
      }
      for (const auto &[tab, name] : tabs) {
        const auto counted = counts.find(tab);
        const std::string count = tab == 2 || tab == settingsTab
                                      ? "" : "  " + std::to_string(counted == counts.end() ? 0 : counted->second);
        const std::string title = name + count + "###Category" + std::to_string(tab);
        if (ImGui::BeginTabItem(title.c_str(), nullptr,
                                (previewMode && previewTab == tab) || padTab == tab
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
          ImGui::BeginChild("ModList", ImVec2(0, -34 * u), ImGuiChildFlags_NavFlattened);
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
            ImGui::BeginChild("Card", ImVec2(0, 55 * u),
                              ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened,
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
            const auto title = card.several ? UI::name(a) + " - " + UI::actionName(a) : UI::name(a);
            ImGui::TextUnformatted(title.c_str());
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
  // Files older versions left in the wrong place move first: the INIs 1.3
  // kept in the ModActivator folder back to bin64, and a log from before 1.3
  // into the folder. The log opens after, and records the moves.
  const auto moved = relocateFiles();
  ml::Log::Begin();
  LOG("Mod Activator " MODACTIVATOR_VERSION " started.");
  for (const auto &line : moved)
    LOG("%s", line.c_str());
  loadLayout();
  const auto config = configPath("ModActivator.ini");
  ml::Settings::Get().wrapSwapChain = wrapSetting(config);
  // Logged here as well as where the swapchain is decided, which a game whose
  // swapchain never reaches this mod does not get to.
  LOG("WrapSwapChain=%s.", ml::Settings::Get().wrapSwapChain < 0 ? "auto"
                           : ml::Settings::Get().wrapSwapChain ? "1" : "0");
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
#include "test_exports/logic.h"

#include "test_exports/preview.h"

#include "test_exports/ui.h"

#endif // MODACTIVATOR_TESTS
