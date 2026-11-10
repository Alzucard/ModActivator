#pragma once
static std::mutex catalogMutex;
static std::vector<Action> catalog, allBindings;
static Action pendingAction;
static std::set<std::string> restartRequired;
static bool hasPending = false;
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
static bool validKeys(const Action &a) { return validShortcut(keys(a)); }
static std::string identity(const Action &a) {
  return lower(a.file + "|" + a.section + "|" + a.key);
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
static std::vector<Action> discover(const std::vector<std::string> &mods,
                                    int toggles = 0) {
  std::vector<Action> found;
  std::error_code ec;
  std::filesystem::recursive_directory_iterator it(
      folder, std::filesystem::directory_options::skip_permission_denied, ec),
      end;
  size_t files = 0;
  for (; it != end && !ec && files < 2000; it.increment(ec)) {
    if (it->is_symlink(ec)) {
      if (it->is_directory(ec))
        it.disable_recursion_pending();
      continue;
    }
    if (it->is_directory(ec)) {
      auto n = lower(it->path().filename().string());
      if (it.depth() >= 2 || n == "reshade-shaders" || n == "community" ||
          n == "cache" || n == "icons" || n == "shapes")
        it.disable_recursion_pending();
      continue;
    }
    if (lower(it->path().extension().string()) != ".ini")
      continue;
    ++files;
    auto rel = it->path().lexically_relative(std::filesystem::path(folder));
    auto module = moduleFor(rel, mods);
    if (module.empty() || lower(module) == "modactivator.asi")
      continue;
    auto file = rel.generic_string();
    char sections[65536];
    DWORD count = GetPrivateProfileSectionNamesA(sections, sizeof sections,
                                                 it->path().string().c_str());
    if (count == sizeof sections - 2)
      continue;
    for (const char *sec = sections; *sec; sec += strlen(sec) + 1) {
      char values[65536];
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
        if (!(toggles == 2
                  ? bindingField(field)
                  : (toggles ? toggleRank(sec, field) : uiRank(sec, field))))
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
                 toggles == 1};
        if (!validKeys(a))
          continue;
        a.cachedValue = read(a);
        a.cachedKeys = keysFrom(a, a.cachedValue);
        found.push_back(a);
      }
    }
  }
  // Stable choice independent of filesystem enumeration order; one UI binding
  // per module.
  std::sort(found.begin(), found.end(),
            [toggles](const Action &a, const Action &b) {
              auto am = lower(a.module), bm = lower(b.module);
              if (am != bm)
                return am < bm;
              int ar = toggles ? toggleRank(a.section, a.key)
                               : uiRank(a.section, a.key),
                  br = toggles ? toggleRank(b.section, b.key)
                               : uiRank(b.section, b.key);
              if (ar != br)
                return ar > br;
              return identity(a) < identity(b);
            });
  if (toggles != 2)
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
    auto found = discover(mods);
    auto all = discover(mods, 2);
    auto toggles = discover(mods, true);
    for (auto &a : toggles)
      if (std::none_of(found.begin(), found.end(), [&](const Action &b) {
            return lower(a.module) == lower(b.module);
          }))
        found.push_back(a);
    for (size_t i = 0; i < all.size(); ++i) {
      auto &a = all[i];
      a.severity = conflict(a.cachedKeys, {17, 16, 8});
      if (a.severity)
        a.overlapNames = "ModActivator\n";
      for (size_t j = 0; j < all.size(); ++j)
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
