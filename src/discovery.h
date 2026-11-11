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
// Overrides belong to ModActivator, keyed by the config path, section and field.
static int readBindingType(const Action &action) {
  return GetPrivateProfileIntA("BindingTypes", identity(action).c_str(), 0,
                               (folder + "ModActivator.ini").c_str());
}

static bool saveBindingType(const Action &action, int type) {
  const auto value = std::to_string(type);
  return WritePrivateProfileStringA("BindingTypes", identity(action).c_str(),
                                    type ? value.c_str() : nullptr,
                                    (folder + "ModActivator.ini").c_str()) != FALSE;
}

static std::string trimConfigLine(const std::string &line) {
  auto start = line.find_first_not_of(" \t\r\n");
  if (start == line.npos)
    return {};
  return line.substr(start, line.find_last_not_of(" \t\r\n") - start + 1);
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
  if (content.compare(0, 3, "\xef\xbb\xbf") == 0)
    content.erase(0, 3);
  else if (content.compare(0, 2, "\xff\xfe") == 0 ||
           content.compare(0, 2, "\xfe\xff") == 0) {
    const bool littleEndian = static_cast<unsigned char>(content[0]) == 0xff;
    std::string ascii;
    for (size_t i = 2; i + 1 < content.size(); i += 2) {
      const auto low = static_cast<unsigned char>(content[i + (littleEndian ? 0 : 1)]);
      const auto high = content[i + (littleEndian ? 1 : 0)];
      ascii += !high && low < 128 ? static_cast<char>(low) : ' ';
    }
    content = std::move(ascii);
  }

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
        description += previousComment;
      }
    } else if (line[0] == '[') {
      const auto end = line.find(']');
      section = end == line.npos ? "" : lower(trimConfigLine(line.substr(1, end - 1)));
      previousComment.clear();
      description.clear();
    } else {
      const auto equals = line.find('=');
      if (equals != line.npos && !section.empty()) {
        const auto inlineComment = bindingComment(line, equals + 1);
        const auto note = inlineComment == line.npos
                              ? std::string{}
                              : trimConfigLine(line.substr(inlineComment + 1));
        if (!note.empty() || !description.empty()) {
          auto &entry = comments[{section, lower(trimConfigLine(line.substr(0, equals)))}];
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

static std::string moduleFor(const std::filesystem::path &rel,
                             const std::vector<std::string> &mods) {
  auto stem = lower(rel.stem().string());
  for (auto &m : mods) {
    auto ms = lower(std::filesystem::path(m).stem().string());
    if (stem == ms)
      return m;
  }
  if (lower(rel.filename().string()) == "reshade.ini") {
    for (auto &m : mods)
      if (lower(m) == "dxgi.dll")
        return m;
  }
  for (auto &m : mods) {
    auto ms = lower(std::filesystem::path(m).stem().string());
    for (const auto &part : rel.parent_path())
      if (lower(part.string()) == ms)
        return m;
  }
  return {};
}
static std::vector<Action> scanBindings(const std::vector<std::string> &mods) {
  const auto layout = gameKeyboardLayout();
  std::vector<Action> found;
  std::error_code ec;
  std::filesystem::recursive_directory_iterator it(
      folder, std::filesystem::directory_options::skip_permission_denied, ec),
      end;
  size_t files = 0;
  for (; it != end && !ec && files < 2000; it.increment(ec)) {
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
    auto module = moduleFor(rel, mods);
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
      for (const char *line = values; *line; line += strlen(line) + 1) {
        std::string entry = line;
        auto sep = entry.find('=');
        if (sep == entry.npos)
          continue;
        auto field = entry.substr(0, sep);
        if (!bindingField(field) && !uiRank(sec, field) && !toggleRank(sec, field))
          continue;
        // A single modifier flag is supported; unknown modifier metadata is
        // skipped, rather than sending a possibly incomplete combination.
        if (GetPrivateProfileIntA(sec, "RequireWin", 0,
                                  it->path().string().c_str()))
          continue;
        Action a{std::filesystem::path(module).stem().string() + " - " + sec +
                     " / " + field,
                 module,
                 file,
                 sec,
                 field,
                 false};
        classifyBinding(a, comments);
        a.toggle = a.featureRank > 0;
        a.cachedValue = read(a);
        a.cachedKeys = keysFrom(a, a.cachedValue, layout);
        if (validShortcut(a.cachedKeys))
          found.push_back(std::move(a));
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
static void refresh() {
  std::vector<std::string> mods;
  HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
  if (h == INVALID_HANDLE_VALUE)
    return;
  MODULEENTRY32 e{};
  e.dwSize = sizeof e;
  if (Module32First(h, &e))
    do {
      auto n = lower(e.szModule);
      if (n.size() > 4 && (n.substr(n.size() - 4) == ".asi" ||
                           std::filesystem::path(n).extension() == ".addon64" ||
                           n == "dxgi.dll"))
        mods.emplace_back(e.szModule);
    } while (Module32Next(h, &e));
  CloseHandle(h);
  try {
    auto all = scanBindings(mods);
    auto found = selectActions(all);
    auto toggles = selectActions(all, true);
    for (auto &a : toggles)
      if (a.typeOverride == 2 || std::none_of(found.begin(), found.end(), [&](const Action &b) {
            return lower(a.module) == lower(b.module);
          }))
        found.push_back(a);
    const auto ownKeys = openerKeys();
    std::map<int, std::vector<size_t>> bindingsByKey;
    for (size_t i = 0; i < all.size(); ++i)
      bindingsByKey[all[i].cachedKeys.back()].push_back(i);
    for (size_t i = 0; i < all.size(); ++i) {
      auto &a = all[i];
      a.severity = conflict(a.cachedKeys, ownKeys);
      if (a.severity)
        a.overlapNames = "ModActivator\n";
      for (size_t j : bindingsByKey.at(a.cachedKeys.back()))
        if (i != j) {
          int c = conflict(a.cachedKeys, all[j].cachedKeys);
          if (c) {
            a.severity = (std::max)(a.severity, c);
            a.overlapNames += all[j].label + "\n";
          }
        }
    }
    std::lock_guard<std::mutex> lock(catalogMutex);
    catalog = std::move(found);
    allBindings = std::move(all);
  } catch (const std::exception &e) {
    LOG_ERR("Discovery failed: %s", e.what());
  }
}
