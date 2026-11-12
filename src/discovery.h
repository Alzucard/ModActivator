#pragma once
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
static std::mutex catalogMutex;
static std::vector<Action> catalog, allBindings;
static std::deque<Action> pendingActions;
static std::set<std::string> restartRequired;
static std::atomic<bool> refreshRequested{false};
// Bumped whenever catalog, allBindings or restartRequired change. The menu
// copies them once per change instead of every frame.
static std::atomic<unsigned> catalogVersion{1};
static void catalogChanged() { catalogVersion.fetch_add(1); }
static std::string lower(std::string s) {
  for (auto &c : s)
    c = (char)tolower((unsigned char)c);
  std::replace(s.begin(), s.end(), '\\', '/');
  return s;
}
static int conflict(const std::vector<int> &a, const std::vector<int> &b) {
  auto main = [](const std::vector<int> &v) {
    for (int k : v)
      if (k != 16 && k != 17 && k != 18)
        return k;
    return 0;
  };
  if (a.empty() || b.empty() || !main(a) || main(a) != main(b))
    return 0;
  auto x = a, y = b;
  std::sort(x.begin(), x.end());
  std::sort(y.begin(), y.end());
  return x == y ? 2 : 1;
}
// Rank only menu/window bindings. Feature toggles and gameplay actions are not
// UI activation.
static int uiRank(const std::string &section, const std::string &field) {
  auto k = lower(field), sec = lower(section);
  if (k.empty() || k[0] == '_' || k.find("controller") != k.npos ||
      k.find("pad") != k.npos || k.find("modifier") != k.npos ||
      k.find("require") != k.npos)
    return 0;
  for (auto word : {"screenshot", "capture", "effect", "stance", "mode",
                    "cycle", "mark", "resume", "pause", "visibility", "reload"})
    if (k.find(word) != k.npos)
      return 0;
  if (k == "keyoverlay")
    return 100;
  if (k.find("key") == k.npos)
    return 0;
  if (k.find("menu") != k.npos || k.find("settingswindow") != k.npos ||
      k.find("uihotkey") != k.npos || k.find("windowhotkey") != k.npos)
    return 100;
  bool ui = sec.find("menu") != sec.npos ||
            sec.find("settingswindow") != sec.npos || sec == "ui" ||
            sec == "interface" || sec == "window";
  if (ui &&
      (k == "hotkey" || k == "key" || k == "activationkeyvk" ||
       k == "activationkey" || k == "togglekey" || k == "togglehotkey" ||
       k == "openkey" || k == "hotkeytoggle" || k == "managelocationshotkey"))
    return 90;
  if ((sec == "general" || sec == "settings") &&
      (k == "activationkeyvk" || k == "menuactivationkey"))
    return 80;
  return 0;
}
static int toggleRank(const std::string &section, const std::string &field) {
  auto k = lower(field);
  if (uiRank(section, field) || k.empty() || k[0] == '_' ||
      k.find("controller") != k.npos || k.find("pad") != k.npos)
    return 0;
  if (k == "togglekey" || k == "togglehotkey" || k == "hotkeytoggle" ||
      k == "activationhotkey")
    return 90;
  if (k == "hotkeytoggleeffect" || k == "toggleeffectkey")
    return 100;
  return 0;
}
static bool bindingField(const std::string &field) {
  auto k = lower(field);
  if (k.empty() || k[0] == '_' || k.find("controller") != k.npos ||
      k.find("pad") != k.npos || k.find("require") != k.npos ||
      k.find("modifier") != k.npos || k.find("options") != k.npos)
    return false;
  return k == "hotkey" || k == "key" || k.find("hotkey") != k.npos ||
         (k.size() >= 3 && k.substr(k.size() - 3) == "key") ||
         (k.size() >= 5 && k.substr(k.size() - 5) == "keyvk");
}
static std::string identity(const Action &a) {
  return lower(a.file + "|" + a.section + "|" + a.key);
}
// The same, computed once per Action for the menu, which asks for it per
// card and per row every frame. file, section and key never change after an
// Action is built.
static const std::string &cachedIdentity(const Action &a) {
  if (a.ui.identity.empty())
    a.ui.identity = identity(a);
  return a.ui.identity;
}
// Everything you choose in the menu: the panel shortcut, binding types, tabs,
// placement and card order, and your names and notes. No release ships this
// file, so installing an update, which brings a fresh ModActivator.ini, never
// replaces it. Created on the first save (or by migrateUserConfig, from what
// older versions kept in ModActivator.ini).
static std::string userConfigPath() { return dataPath("ModActivator.User.ini"); }
// For saving: the folder is created first if it is not there yet.
static std::string writableUserConfig() {
  ensureDataFolder();
  return userConfigPath();
}

// Overrides belong to ModActivator, keyed by the config path, section and field.
static int readBindingType(const Action &action) {
  return GetPrivateProfileIntA("BindingTypes", identity(action).c_str(), 0,
                               userConfigPath().c_str());
}

static bool saveBindingType(const Action &action, int type) {
  const auto value = std::to_string(type);
  return WritePrivateProfileStringA("BindingTypes", identity(action).c_str(),
                                    type ? value.c_str() : nullptr,
                                    writableUserConfig().c_str()) != FALSE;
}

static std::string trimConfigLine(const std::string &line) {
  auto start = line.find_first_not_of(" \t\r\n");
  if (start == line.npos)
    return {};
  return line.substr(start, line.find_last_not_of(" \t\r\n") - start + 1);
}

// --- text encodings -------------------------------------------------------
// Windows hands this plugin names in the ANSI code page (module, file and
// section names, values read through the profile API), while Dear ImGui
// draws UTF-8. Anything shown in the menu goes through displayText.
static bool isAscii(const std::string &s) {
  return std::all_of(s.begin(), s.end(), [](unsigned char c) { return c < 0x80; });
}
static std::string narrowText(const wchar_t *wide, int count, UINT page) {
  if (count <= 0)
    return {};
  const int n = WideCharToMultiByte(page, 0, wide, count, nullptr, 0, nullptr, nullptr);
  if (n <= 0)
    return {};
  std::string out(n, '\0');
  WideCharToMultiByte(page, 0, wide, count, out.data(), n, nullptr, nullptr);
  return out;
}
static std::string convertText(const std::string &s, UINT from, UINT to) {
  if (s.empty() || isAscii(s))
    return s;
  const int n = MultiByteToWideChar(from, 0, s.data(), (int)s.size(), nullptr, 0);
  if (n <= 0)
    return s;
  std::wstring wide(n, L'\0');
  MultiByteToWideChar(from, 0, s.data(), (int)s.size(), wide.data(), n);
  const auto out = narrowText(wide.data(), n, to);
  return out.empty() ? s : out;
}
static bool validUtf8(const std::string &s) {
  return s.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                                          (int)s.size(), nullptr, 0) > 0;
}
static std::string displayText(const std::string &ansi) {
  return convertText(ansi, CP_ACP, CP_UTF8);
}

struct BindingComment {
  std::string description;
  std::string classificationHint;
};
using BindingComments = std::map<std::pair<std::string, std::string>, BindingComment>;

static BindingComments readBindingComments(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file || file.tellg() > 1024 * 1024 || file.tellg() < 0)
    return {};
  std::string content(static_cast<size_t>(file.tellg()), '\0');
  file.seekg(0);
  if (!file.read(content.data(), content.size()))
    return {};
  // Descriptions are kept as UTF-8 for the menu. Section and field names are
  // kept as the profile API reports them, so they still match the binding:
  // the file's own bytes for a byte file, the ANSI code page for UTF-16.
  bool textIsUtf8 = false, keysAreAnsi = false;
  if (content.compare(0, 3, "\xef\xbb\xbf") == 0) {
    content.erase(0, 3);
    textIsUtf8 = true;
  } else if (content.compare(0, 2, "\xff\xfe") == 0 ||
             content.compare(0, 2, "\xfe\xff") == 0) {
    const bool littleEndian = static_cast<unsigned char>(content[0]) == 0xff;
    std::wstring wide;
    for (size_t i = 2; i + 1 < content.size(); i += 2) {
      const auto low = static_cast<unsigned char>(content[i + (littleEndian ? 0 : 1)]);
      const auto high = static_cast<unsigned char>(content[i + (littleEndian ? 1 : 0)]);
      wide += static_cast<wchar_t>(low | (high << 8));
    }
    content = narrowText(wide.data(), (int)wide.size(), CP_UTF8);
    textIsUtf8 = keysAreAnsi = true;
  } else {
    // No BOM: UTF-8 when it decodes as UTF-8, the ANSI code page otherwise.
    textIsUtf8 = validUtf8(content);
  }
  const auto text = [&](const std::string &s) { return textIsUtf8 ? s : displayText(s); };
  const auto name = [&](const std::string &s) {
    return keysAreAnsi ? convertText(s, CP_UTF8, CP_ACP) : s;
  };

  BindingComments comments;
  std::istringstream lines(content);
  std::string line, section, previousComment, description;
  while (std::getline(lines, line)) {
    line = trimConfigLine(line);
    if (line.empty()) {
      previousComment.clear();
      description.clear();
    } else if (line[0] == ';' || line[0] == '#') {
      previousComment = trimConfigLine(line.substr(1));
      if (!previousComment.empty()) {
        if (!description.empty())
          description += '\n';
        description += text(previousComment);
      }
    } else if (line[0] == '[') {
      const auto end = line.find(']');
      section = end == line.npos ? "" : lower(name(trimConfigLine(line.substr(1, end - 1))));
      previousComment.clear();
      description.clear();
    } else {
      const auto equals = line.find('=');
      if (equals != line.npos && !section.empty()) {
        const auto inlineComment = bindingComment(line, equals + 1);
        const auto note = inlineComment == line.npos
                              ? std::string{}
                              : text(trimConfigLine(line.substr(inlineComment + 1)));
        if (!note.empty() || !description.empty()) {
          auto &entry = comments[{section, lower(name(trimConfigLine(line.substr(0, equals))))}];
          entry.description = note.empty() || description.empty()
                                  ? note + description
                                  : note + "\n" + description;
          entry.classificationHint = previousComment + " " + note;
        }
      }
      previousComment.clear();
      description.clear();
    }
  }
  return comments;
}

static void classifyBinding(Action &action, const BindingComments &comments) {
  const auto comment = comments.find({lower(action.section), lower(action.key)});
  action.description = comment == comments.end() ? "" : comment->second.description;
  action.typeOverride = readBindingType(action);
  if (action.typeOverride >= 1 && action.typeOverride <= 3) {
    action.menuRank = action.typeOverride == 1 ? 200 : 0;
    action.featureRank = action.typeOverride == 2 ? 200 : 0;
    action.chosenKind = true;
    return;
  }
  action.typeOverride = 0;
  action.menuRank = uiRank(action.section, action.key);
  action.featureRank = toggleRank(action.section, action.key);

  // Look for the action and its object together. A mention of a menu alone
  // could describe where a gameplay shortcut works.
  static const std::regex opensInterface(
      R"(\b(?:open(?:s|/close)?|close[sd]?|show[s]?|hide[s]?|toggle[s]?)\s+(?:(?:the|a|an|mod|settings|in-game|in game)\s+)*(?:menu|gui|ui|window|panel|overlay|wiki|journal|interface)\b)",
      std::regex::icase | std::regex::optimize);
  if (comment != comments.end() &&
      std::regex_search(comment->second.classificationHint, opensInterface)) {
    action.menuRank = 120;
    action.featureRank = 0;
  }
}

// ReShade is identified by its add-on export, not by its file name: it can be
// installed as dxgi.dll, d3d12.dll, ReShade64.dll or anything else, and the
// system dxgi.dll is loaded in every D3D12 game whether ReShade is or not.
static bool isReShadeModule(HMODULE module) {
  return module && GetProcAddress(module, "ReShadeRegisterAddon");
}
// The module refresh() last identified as ReShade, lower case. Read by the
// menu for card titles, so it is guarded by its own lock.
static std::mutex reshadeModuleMutex;
static std::string reshadeModuleName;
static bool isReShadeName(const std::string &module) {
  std::lock_guard<std::mutex> lock(reshadeModuleMutex);
  return !reshadeModuleName.empty() && lower(module) == reshadeModuleName;
}
// Whether a wide path survives conversion to the ANSI code page unchanged.
static bool representableInAnsi(const std::wstring &wide) {
  if (wide.empty())
    return true;
  BOOL lossy = FALSE;
  const int n = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.c_str(),
                                    (int)wide.size(), nullptr, 0, nullptr, &lossy);
  return n > 0 && !lossy;
}
#include "descriptions.h"
static std::string moduleFor(const std::filesystem::path &rel,
                             const std::vector<std::string> &mods,
                             const std::string &reshade) {
  auto stem = lower(rel.stem().string());
  for (auto &m : mods) {
    auto ms = lower(std::filesystem::path(m).stem().string());
    if (stem == ms)
      return m;
  }
  // ReShade.ini belongs to ReShade only while ReShade is actually loaded.
  // A leftover file must not produce a card whose button sends keys to
  // nothing.
  if (lower(rel.filename().string()) == "reshade.ini")
    return reshade;
  for (auto &m : mods) {
    auto ms = lower(std::filesystem::path(m).stem().string());
    for (const auto &part : rel.parent_path())
      if (lower(part.string()) == ms)
        return m;
  }
  return {};
}
static std::vector<Action> scanBindings(const std::vector<std::string> &mods,
                                        const std::string &reshade = {}) {
  const auto layout = gameKeyboardLayout();
  DescriptionSources sources;
  std::vector<Action> found;
  std::error_code ec;
  std::filesystem::recursive_directory_iterator it(
      folder, std::filesystem::directory_options::skip_permission_denied, ec),
      end;
  size_t files = 0;
  for (; it != end && !ec && files < 2000; it.increment(ec)) {
    // Every config path is later handed to the ANSI profile API. A name the
    // system code page cannot spell would make .string() throw and abort the
    // whole scan, so such an entry, and everything below it, is skipped.
    if (!representableInAnsi(it->path().native())) {
      it.disable_recursion_pending();
      static std::set<std::wstring> reported;
      if (reported.insert(it->path().native()).second)
        LOG_ERR("Skipped %s: its name uses characters outside the system code page, "
                "so its settings cannot be read.",
                reinterpret_cast<const char *>(it->path().u8string().c_str()));
      continue;
    }
    std::error_code statusError;
    const auto status = it->symlink_status(statusError);
    if (statusError || std::filesystem::is_symlink(status)) {
      it.disable_recursion_pending();
      continue;
    }
    if (std::filesystem::is_directory(status)) {
      auto n = lower(it->path().filename().string());
      if (it.depth() >= 2 || n == "reshade-shaders" || n == "community" ||
          n == "cache" || n == "icons" || n == "shapes")
        it.disable_recursion_pending();
      continue;
    }
    if (!std::filesystem::is_regular_file(status) ||
        lower(it->path().extension().string()) != ".ini")
      continue;
    ++files;
    auto rel = it->path().lexically_relative(std::filesystem::path(folder));
    auto module = moduleFor(rel, mods, reshade);
    if (module.empty() || lower(module) == "modactivator.asi")
      continue;
    auto file = rel.generic_string();
    const auto comments = readBindingComments(it->path());
    char sections[65536]{};
    DWORD count = GetPrivateProfileSectionNamesA(sections, sizeof sections,
                                                 it->path().string().c_str());
    if (count == sizeof sections - 2)
      continue;
    for (const char *sec = sections; *sec; sec += strlen(sec) + 1) {
      char values[65536]{};
      DWORD size = GetPrivateProfileSectionA(sec, values, sizeof values,
                                             it->path().string().c_str());
      if (size == sizeof values - 2)
        continue;
      // The section's settings by lower-case name, for finding the
      // controller setting that goes with a key.
      std::map<std::string, std::string> settings;
      for (const char *line = values; *line; line += strlen(line) + 1)
        if (const char *eq = strchr(line, '='); eq && eq != line)
          settings.emplace(lower(trimConfigLine(std::string(line, eq))), trimConfigLine(eq + 1));
      for (const char *line = values; *line; line += strlen(line) + 1) {
        std::string entry = line;
        auto sep = entry.find('=');
        if (sep == entry.npos)
          continue;
        auto field = entry.substr(0, sep);
        const Action probe{"", module, file, sec, field};
        if (ignoredField(sources, probe) ||
            (!bindingField(field) && !uiRank(sec, field) && !toggleRank(sec, field) &&
             !describedField(sources, probe)))
          continue;
        // A single modifier flag is supported; unknown modifier metadata is
        // skipped, rather than sending a possibly incomplete combination.
        if (GetPrivateProfileIntA(sec, "RequireWin", 0,
                                  it->path().string().c_str()))
          continue;
        const bool fromReShade = !reshade.empty() && lower(module) == lower(reshade);
        Action a{(fromReShade ? std::string("ReShade")
                              : std::filesystem::path(module).stem().string()) +
                     " - " + sec + " / " + field,
                 module,
                 file,
                 sec,
                 field,
                 false};
        classifyBinding(a, comments);
        describeBinding(a, sources, it->path());
        a.toggle = a.featureRank > 0;
        a.cachedValue = read(a);
        a.cachedKeys = keysFrom(a, a.cachedValue, layout);
        if (validShortcut(a.cachedKeys)) {
          findPadBinding(a, settings);
          found.push_back(std::move(a));
        } else {
          // Say once per value why a binding is missing, unless it is
          // plainly switched off. Otherwise an unknown key name just
          // vanishes from the menu.
          const auto value = trimConfigLine(a.cachedValue.substr(0, a.cachedValue.find_first_of(";|")));
          const auto folded = lower(value);
          static std::set<std::string> reported;
          if (!value.empty() && folded != "none" && folded != "0" &&
              reported.insert(identity(a) + "=" + folded).second)
            LOG("Skipped %s [%s] %s=%s: not a keyboard shortcut ModActivator can read.",
                a.file.c_str(), a.section.c_str(), a.key.c_str(), value.c_str());
        }
      }
    }
  }
  std::sort(found.begin(), found.end(), [](const Action &a, const Action &b) {
    const auto am = lower(a.module), bm = lower(b.module);
    return am == bm ? identity(a) < identity(b) : am < bm;
  });
  found.erase(std::unique(found.begin(), found.end(), [](const Action &a, const Action &b) {
                return identity(a) == identity(b);
              }), found.end());
  return found;
}

static std::vector<Action> selectActions(const std::vector<Action> &bindings,
                                         bool toggles = false) {
  std::vector<Action> found;
  for (const auto &action : bindings)
    if (toggles ? action.featureRank : action.menuRank)
      found.push_back(action);
  // Pick the highest-ranked action, breaking ties by configuration path.
  std::stable_sort(found.begin(), found.end(),
            [toggles](const Action &a, const Action &b) {
              auto am = lower(a.module), bm = lower(b.module);
              if (am != bm)
                return am < bm;
              return toggles ? a.featureRank > b.featureRank
                             : a.menuRank > b.menuRank;
            });
  found.erase(std::unique(found.begin(), found.end(),
                            [](const Action &a, const Action &b) {
                              return lower(a.module) == lower(b.module);
                            }),
                found.end());
  return found;
}
// The cards: for each mod its best-guessed menu, and its best-guessed toggle
// when it has no menu (or you chose that toggle). Then every binding whose kind
// was chosen rather than guessed (by you in Settings, by the mod itself or by
// the known-mods list) as a menu or a toggle gets a card of its own, so a mod
// can have as many cards as it has such shortcuts.
static std::vector<Action> chooseCards(const std::vector<Action> &all) {
  auto found = selectActions(all);
  for (auto &a : selectActions(all, true))
    if (a.typeOverride == 2 || std::none_of(found.begin(), found.end(), [&](const Action &b) {
          return lower(a.module) == lower(b.module);
        }))
      found.push_back(a);
  for (const auto &a : all)
    if (a.chosenKind && (a.menuRank || a.featureRank) &&
        std::none_of(found.begin(), found.end(),
                     [&](const Action &b) { return identity(a) == identity(b); }))
      found.push_back(a);
  return found;
}
// Keys taken outside the mods, from the last refresh. Guarded by catalogMutex.
static std::vector<ReservedKey> reservedKeys;

// A binding against a reserved combination: the same keys (2), or the
// reserved one plus more modifiers (1: Ctrl+F12 still sets off Steam's F12).
// Missing one of its modifiers is no clash: F4 is not Alt+F4.
static int reservedConflict(const std::vector<int> &keys, const std::vector<int> &reserved) {
  const int c = conflict(keys, reserved);
  if (c != 1)
    return c;
  for (int k : reserved)
    if (std::find(keys.begin(), keys.end(), k) == keys.end())
      return 0;
  return 1;
}

// Severity (2 the same, 1 overlapping) and who else uses each binding's keys
// or controller buttons: ModActivator's own shortcuts, every other binding,
// and keys reserved by Steam, Windows or you. Controller clashes are marked
// as such, since the keys themselves may be clear.
static void markConflicts(std::vector<Action> &all, const std::vector<ReservedKey> &reserved) {
  const auto ownKeys = openerKeys();
  const unsigned ownPad = padOpener.load();
  std::map<int, std::vector<size_t>> bindingsByKey;
  for (size_t i = 0; i < all.size(); ++i)
    bindingsByKey[all[i].cachedKeys.back()].push_back(i);
  for (size_t i = 0; i < all.size(); ++i) {
    auto &a = all[i];
    a.severity = 0;
    a.overlapNames.clear();
    const auto note = [&](int severity, const std::string &who) {
      if (!severity)
        return;
      a.severity = (std::max)(a.severity, severity);
      a.overlapNames += who + "\n";
    };
    note(conflict(a.cachedKeys, ownKeys), "ModActivator");
    note(padConflict(a.padButtons, ownPad), "ModActivator (controller)");
    for (size_t j : bindingsByKey.at(a.cachedKeys.back()))
      if (i != j)
        note(conflict(a.cachedKeys, all[j].cachedKeys), all[j].label);
    for (size_t j = 0; j < all.size(); ++j)
      if (i != j)
        note(padConflict(a.padButtons, all[j].padButtons), all[j].label + " (controller)");
    for (const auto &r : reserved)
      note(reservedConflict(a.cachedKeys, r.keys), r.label);
  }
}
// Who outside the mods uses keys you are about to save, for the editors.
static std::vector<std::pair<int, std::string>> reservedClashes(const std::vector<int> &keys) {
  std::vector<std::pair<int, std::string>> out;
  std::lock_guard<std::mutex> lock(catalogMutex);
  for (const auto &r : reservedKeys)
    if (const int c = reservedConflict(keys, r.keys))
      out.emplace_back(c, r.label);
  return out;
}

static void refresh() {
  std::vector<std::string> mods;
  std::string reshade;
  HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
  if (h == INVALID_HANDLE_VALUE)
    return;
  MODULEENTRY32 e{};
  e.dwSize = sizeof e;
  if (Module32First(h, &e))
    do {
      auto n = lower(e.szModule);
      if (reshade.empty() && isReShadeModule(e.hModule)) {
        reshade = e.szModule;
        mods.emplace_back(e.szModule);
      } else if (n.size() > 4 &&
                 (n.substr(n.size() - 4) == ".asi" ||
                  std::filesystem::path(n).extension() == ".addon64"))
        mods.emplace_back(e.szModule);
    } while (Module32Next(h, &e));
  CloseHandle(h);
  {
    std::lock_guard<std::mutex> lock(reshadeModuleMutex);
    reshadeModuleName = lower(reshade);
  }
  try {
    auto all = scanBindings(mods, reshade);
    auto found = chooseCards(all);
    auto reserved = loadReservedKeys();
    markConflicts(all, reserved);
    std::lock_guard<std::mutex> lock(catalogMutex);
    catalog = std::move(found);
    allBindings = std::move(all);
    reservedKeys = std::move(reserved);
    catalogChanged();
  } catch (const std::exception &e) {
    LOG_ERR("Discovery failed: %s", e.what());
  }
}
