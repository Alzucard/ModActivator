#include "core/log.h"
#include "core/settings.h"
#include "core/state.h"
#include "gui/menu.h"
#include "hooks/dx12_hook.h"
#include "hooks/input.h"
#include <MinHook.h>
#include <TlHelp32.h>
#include <Windows.h>
#include "version.h"
#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <imgui.h>
#include <mutex>
#include <set>
#include <string>
#include <vector>
static std::string folder;
static std::atomic<unsigned long> configReads{0};

struct Action {
  std::string label, module, file, section, key;
  bool toggle = false;
  std::string cachedValue;
  std::vector<int> cachedKeys;
  int severity = 0;
  std::string overlapNames;
};
static std::string read(const Action &a) {
  ++configReads;
  char b[512];
  GetPrivateProfileStringA(a.section.c_str(), a.key.c_str(), "", b, sizeof b,
                           (folder + a.file).c_str());
  return b;
}
static bool held(int k) { return (GetAsyncKeyState(k) & 0x8000) != 0; }
static int vk(std::string s) {
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
  if (s == "GRAVE")
    return 192;
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
  if (isdigit(s[0])) {
    char *end = nullptr;
    long v = strtol(s.c_str(), &end, s.size() > 2 && s[1] == 'X' ? 16 : 10);
    return end && !*end && v >= 0 && v <= 255 ? (int)v : 0;
  }
  if (s.size() == 1)
    return s[0];
  return 0;
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
static std::vector<int> keysFrom(const Action &a, std::string s) {
  configReads += 3;
  s.erase(std::remove_if(s.begin(), s.end(),
                         [](unsigned char c) { return c == ' ' || c == '\t'; }),
          s.end());
  auto comment = s.find(';');
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
  if (s.find(',') != std::string::npos) {
    if (_stricmp(a.file.c_str(), "ReShade.ini"))
      return {};
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
  size_t pos = 0;
  while (true) {
    size_t end = s.find('+', pos);
    auto token = s.substr(pos, end == std::string::npos ? end : end - pos);
    out.push_back(s.find('+') != std::string::npos && token.size() == 1 &&
                          isdigit(token[0])
                      ? token[0]
                      : vk(token));
    if (end == std::string::npos)
      break;
    pos = end + 1;
  }
  return out;
}
static std::vector<int> keys(const Action &a) { return keysFrom(a, read(a)); }
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
static bool key(int k, bool up) {
  INPUT i{};
  i.type = INPUT_KEYBOARD;
  i.ki.dwExtraInfo = 0x4D414354;
  i.ki.wVk = (WORD)k;
  i.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
  if (k >= 33 && k <= 46)
    i.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
  return SendInput(1, &i, sizeof i) == 1;
}
static DWORD WINAPI dispatch(void *) {
  for (;;) {
    if (refreshRequested.exchange(false)) {
      refresh();
    }
    Action a;
    bool pending = false;
    {
      std::lock_guard<std::mutex> lock(catalogMutex);
      pending = hasPending;
      if (pending) {
        a = pendingAction;
        hasPending = false;
      }
    }
    if (pending) {
      bool restart;
      {
        std::lock_guard<std::mutex> lock(catalogMutex);
        restart = restartRequired.count(lower(a.module)) != 0;
      }
      if (restart)
        continue;
      Sleep(220);
      auto ks = keys(a);
      bool valid = validShortcut(ks) && GetModuleHandleA(a.module.c_str());
      for (int k : ks)
        if (k < 1 || k > 255)
          valid = false;
      for (int t = 0; t < 40 && (held(16) || held(17) || held(18) || held(1));
           ++t)
        Sleep(25);
      if (valid && ml::State::ForegroundIsOurs() && !held(16) && !held(17) &&
          !held(18) && !held(1)) {
        ml::State::Get().keyboardPassthrough.store(a.toggle);
        size_t pressed = 0;
        for (int k : ks) {
          if (!ml::State::ForegroundIsOurs() || !key(k, false))
            break;
          ++pressed;
        }
        bool complete = pressed == ks.size();
        Sleep(130);
        while (pressed)
          key(ks[--pressed], true);
        ml::State::Get().keyboardPassthrough.store(false);
        if (complete)
          LOG("Shortcut dispatched: %s", a.label.c_str());
        else
          LOG_ERR("Shortcut incomplete: %s", a.label.c_str());
      } else
        LOG_ERR("Shortcut cancelled: invalid binding, held modifiers, or game "
                "lost focus.");
    }
    Sleep(20);
  }
  return 0;
}
static void queueAction(const Action &a) {
  std::lock_guard<std::mutex> lock(catalogMutex);
  if (restartRequired.count(lower(a.module)))
    return;
  pendingAction = a;
  hasPending = true;
  if (!a.toggle)
    ml::State::Get().menuOpen = false;
}
namespace ml::gui {
void InitStyle(float scale) {
  ImGui::StyleColorsDark();
  ImGui::GetStyle().ScaleAllSizes((std::max)(1.0f, scale));
  ImGui::GetIO().IniFilename = nullptr;
}
bool FontsNeedRebuild() { return false; }
void RebuildFonts() {}
void PollToggle() {
  static bool was = false;
  bool now = held(17) && held(16) && held(VK_BACK) && !held(VK_MENU) && !held(VK_LWIN) && !held(VK_RWIN);
  auto &s = State::Get();
  if (State::ForegroundIsOurs()) {
    if (now && !was) {
      s.menuOpen = !s.menuOpen;
      if (s.menuOpen)
        refreshRequested.store(true);
    }
    if (s.menuOpen && held(VK_ESCAPE))
      s.menuOpen = false;
  } else
    s.menuOpen = false;
  was = now;
}
bool WantsDraw() { return State::Get().menuOpen; }
void FeedInput() {
  auto &s = State::Get();
  auto &io = ImGui::GetIO();
  bool c = s.Captures();
  static bool old = false;
  io.MouseDrawCursor = c;
  if (c != old) {
    old = c;
    if (c)
      input::MenuOpened();
    else
      input::MenuClosed();
  }
  if (c)
    input::FeedMouse(io);
  else
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
}
void Render() {
  auto &s = State::Get();
  ImGui::SetNextWindowSize(ImVec2(640, 650), ImGuiCond_FirstUseEver);
  bool open = s.menuOpen.load();
  if (ImGui::Begin("Mod Activator " MODACTIVATOR_VERSION, &open, ImGuiWindowFlags_NoCollapse)) {
    ImGui::TextUnformatted("Ctrl + Shift + Backspace");
    ImGui::SameLine();
    if (ImGui::Button("Refresh mods"))
      refreshRequested.store(true);
    std::vector<Action> actions;
    {
      std::lock_guard<std::mutex> lock(catalogMutex);
      actions = catalog;
    }
    if (ImGui::BeginTabBar("Categories")) {
      for (int tab = 0; tab < 3; ++tab) {
        if (ImGui::BeginTabItem(tab == 2 ? "Hotkeys"
                                         : (tab ? "Toggles" : "Mod UIs"))) {
          if (tab == 2) {
            drawHotkeys();
            ImGui::EndTabItem();
            continue;
          }
          if (tab)
            ImGui::TextWrapped("These mods do not report live state. "
                               "Unreported means on/off cannot be verified. "
                               "Sending a toggle does not confirm a change.");
          int count = 0;
          for (int n = 0; n < (int)actions.size(); ++n) {
            auto &a = actions[n];
            if (a.toggle != (tab == 1))
              continue;
            ++count;
            HMODULE module = GetModuleHandleA(a.module.c_str());
            bool managed = !a.toggle && module &&
                           lower(a.module) != "modhub.asi" &&
                           GetProcAddress(module, "ModHubMenuV1") &&
                           GetModuleHandleA("ModHub.asi");
            bool restart = false;
            {
              std::lock_guard<std::mutex> lock(catalogMutex);
              restart = restartRequired.count(lower(a.module)) != 0;
            }
            ImGui::PushID(n);
            ImGui::BeginDisabled(!module || managed || restart);
            std::string label = (a.toggle ? "Toggle: " : "") + a.label;
            if (ImGui::Button(label.c_str(), ImVec2(-1, 32))) {
              queueAction(a);
              if (!a.toggle)
                open = false;
            }
            ImGui::EndDisabled();
            if (restart)
              ImGui::TextDisabled("Restart the game to use this mod after "
                                  "editing its hotkeys.");
            else if (managed)
              ImGui::TextDisabled(
                  "Menu controlled by ModHub. Open it through ModHub.");
            else
              ImGui::TextDisabled("Shortcut: %s", a.cachedValue.c_str());
            if (a.toggle)
              ImGui::TextColored(ImVec4(1, 0.75f, 0.25f, 1),
                                 "State: Unreported (no live-state interface)");
            ImGui::PopID();
          }
          if (!count)
            ImGui::TextDisabled(
                "No matching loaded mods with recognized shortcuts.");
          ImGui::EndTabItem();
        }
      }
      ImGui::EndTabBar();
    }
  }
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
  ml::Settings::Get().wrapSwapChain =
      GetPrivateProfileIntA("General", "WrapSwapChain", 0,
                            (folder + "ModActivator.ini").c_str()) != 0;
  LOG("Mod Activator " MODACTIVATOR_VERSION " reliability fixes. Activation only; no gameplay "
      "hooks.");
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
extern "C" __declspec(dllexport) int ModActivatorReliabilityTest(){
 if(!folder.empty())return -1;
 Action a{"Fixture","Fixture.asi","absent.ini","Menu","Hotkey"};
 if(validShortcut(keysFrom(a,"F4+F5")))return 1;
 if(validShortcut(keysFrom(a,"Ctrl+Ctrl+F5")))return 2;
 if(validShortcut(keysFrom(a,"F5+Ctrl")))return 3;
 if(keysFrom(a,"Ctrl + 7")!=std::vector<int>({17,55}))return 4;
 a.file="reshade.ini";
 if(keysFrom(a,"36,1,0,0")!=std::vector<int>({17,36}))return 5;
 for(auto value:{"36,1","36,2,0,0","36,0,0,0junk","36,0,0,0,1"})if(validShortcut(keysFrom(a,value)))return 6;
 a.module="RestartFixture.asi";
 {std::lock_guard<std::mutex>lock(catalogMutex);restartRequired.insert(lower(a.module));hasPending=false;}
 queueAction(a);if(hasPending)return 7;
 {std::lock_guard<std::mutex>lock(catalogMutex);restartRequired.erase(lower(a.module));}
 queueAction(a);if(!hasPending)return 8;hasPending=false;return 0;
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
    for (auto &a : discover(names)) {
      report += a.module + "|" + a.file + "|" + a.section + "|" + a.key + "|";
      for (int k : keys(a))
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
    for (auto &a : discover(names, true)) {
      report += a.module + "|" + a.file + "|" + a.section + "|" + a.key + "|";
      for (int k : keys(a))
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
  hasPending = false;
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
