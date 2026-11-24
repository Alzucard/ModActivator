#pragma once
// What a binding does, and what to call it, from every source ModActivator
// knows. In order of trust:
//
//   1. your own name and note, saved in ModActivator.User.ini
//   2. the mod itself, through the optional ModActivatorBindingsV1 export
//      (see include/ModActivatorBindings.h)
//   3. the known-mods list, built into the plugin from
//      data/ModActivator.KnownMods.ini
//   4. the comment beside the setting in the mod's config file, or else
//      the default config text or settings schema built into the mod's own
//      file (see binary_info.h)
//   5. a readme next to the mod's config file
//   6. a sentence in the mod's own file that names the setting
//
// Names fall back to what the setting's key is called, and the menu says
// where each description came from.
#include "../include/ModActivatorBindings.h"

enum DescriptionSource {
  sourceNone = 0,
  sourceReadme,
  sourceComment,
  sourceKnownMods,
  sourceMod,
  sourceUser,
  sourceBinary, // ranks below sourceComment; numbered last so the others keep their numbers
};

static const char *sourceLabel(int source) {
  switch (source) {
  case sourceReadme: return "From the mod's readme";
  case sourceComment: return "From the mod's config file";
  case sourceKnownMods: return "From ModActivator's list of known mods";
  case sourceMod: return "From the mod itself";
  case sourceUser: return "Your note";
  case sourceBinary: return "From the mod's file";
  }
  return "";
}

// Binding kinds, as the database and the mod export name them. The numbers
// are the export's (ModActivatorKind*) and match the Settings tab's
// "Show as" types 1 to 3. The database's ignore is a setting that only looks
// like a shortcut, such as a flag called HotkeyCtrl.
static constexpr int kindIgnore = -1;
static int kindFromName(const std::string &name) {
  const auto k = lower(name);
  return k == "menu" ? 1 : k == "toggle" ? 2 : k == "action" ? 3 : k == "ignore" ? kindIgnore : 0;
}

static std::string hexText(const std::string &value) {
  std::string result;
  for (unsigned char c : value) {
    constexpr char digits[] = "0123456789abcdef";
    result += digits[c >> 4];
    result += digits[c & 15];
  }
  return result;
}
static std::string unhexText(const std::string &value) {
  if (value.size() % 2 || value.find_first_not_of("0123456789abcdef") != value.npos)
    return {};
  std::string result;
  for (size_t i = 0; i < value.size(); i += 2)
    result += (char)std::stoi(value.substr(i, 2), nullptr, 16);
  return result;
}

// --- what one source says about one binding ---------------------------------
struct BindingInfo {
  int kind = 0;
  std::string name, description;
};

// --- 1. your own names and notes --------------------------------------------
// In ModActivator.User.ini (see userConfigPath), which updates never replace.
// Hex-encoded UTF-8 under [BindingNames] and [BindingNotes], keyed like
// [BindingTypes], so any text survives the profile API.
static std::string readUserText(const char *section, const Action &action) {
  char value[4096]{};
  GetPrivateProfileStringA(section, identity(action).c_str(), "", value, sizeof value,
                           userConfigPath().c_str());
  return unhexText(value);
}
static bool saveUserText(const char *section, const Action &action, const std::string &text) {
  const auto value = hexText(text);
  if (value.size() >= 4000)
    return false;
  return WritePrivateProfileStringA(section, identity(action).c_str(),
                                    text.empty() ? nullptr : value.c_str(),
                                    writableUserConfig().c_str()) != FALSE;
}
static BindingInfo userInfo(const Action &action) {
  return {0, readUserText("BindingNames", action), readUserText("BindingNotes", action)};
}

// --- 3. ModActivator.KnownMods.ini -------------------------------------------
struct KnownBinding {
  std::string section, key; // lower case; section empty for any, with one * for a pattern
  BindingInfo info;
  std::string modifiers; // Action::modifierRule, as written in the file
  std::string padField;  // the setting holding its controller combination, if not found by name
};
struct KnownMod {
  std::string file; // lower case, '/' separated
  std::string name, about;
  bool hexNumbers = false;
  // @reload: "live" when the mod reads its config while the game runs, or the
  // key (Section/Key or Key, in the same file) that makes it read it again.
  std::string reload;
  // @edit: why the hotkey editor leaves this file alone, said to you in its
  // place. Empty: it can be edited.
  std::string noEdit;
  std::vector<KnownBinding> bindings;
  // In the [@reserved] section only: keys something other than a mod uses
  // (Steam, Windows), as spelled in the file, with what uses them.
  std::vector<std::pair<std::string, std::string>> reserved;
};
using KnownMods = std::vector<KnownMod>;

static std::vector<std::string> splitFields(const std::string &value, size_t count) {
  std::vector<std::string> fields;
  size_t pos = 0;
  while (fields.size() + 1 < count) {
    const auto bar = value.find('|', pos);
    if (bar == value.npos)
      break;
    fields.push_back(trimConfigLine(value.substr(pos, bar - pos)));
    pos = bar + 1;
  }
  fields.push_back(trimConfigLine(value.substr(pos)));
  fields.resize(count);
  return fields;
}

// A pattern's one * matches any run of characters: Shortcut.* every section
// starting Shortcut., Shortcut.*.toggle those that also end .toggle.
static bool sectionMatches(const std::string &pattern, const std::string &section) {
  if (pattern.empty())
    return true;
  const auto star = pattern.find('*');
  if (star == pattern.npos)
    return pattern == section;
  const size_t tail = pattern.size() - star - 1;
  return section.size() >= star + tail && section.compare(0, star, pattern, 0, star) == 0 &&
         section.compare(section.size() - tail, tail, pattern, star + 1, tail) == 0;
}
// The part of section a pattern's * matched; the whole section without a *.
static std::string sectionWildcardPart(const std::string &pattern, const std::string &section) {
  const auto star = pattern.find('*');
  if (star == pattern.npos)
    return section;
  return section.substr(star, section.size() - (pattern.size() - 1));
}
// How specific a section pattern is: an exact section, then the pattern that
// spells out more of it, then any section.
static int sectionScore(const std::string &pattern) {
  if (pattern.empty())
    return 0;
  return pattern.find('*') == pattern.npos ? 100000 : 1 + (int)pattern.size();
}
static KnownMods parseKnownMods(const std::string &text) {
  KnownMods mods;
  std::istringstream lines(text.compare(0, 3, "\xef\xbb\xbf") == 0 ? text.substr(3) : text);
  std::string line;
  while (std::getline(lines, line)) {
    line = trimConfigLine(line);
    if (line.empty() || line[0] == ';' || line[0] == '#')
      continue;
    if (line[0] == '[') {
      const auto end = line.find(']');
      if (end == line.npos)
        continue;
      mods.emplace_back();
      mods.back().file = lower(trimConfigLine(line.substr(1, end - 1)));
      continue;
    }
    const auto equals = line.find('=');
    if (mods.empty() || equals == line.npos)
      continue;
    auto &mod = mods.back();
    const auto field = trimConfigLine(line.substr(0, equals));
    const auto value = trimConfigLine(line.substr(equals + 1));
    if (mod.file == "@reserved") {
      mod.reserved.emplace_back(field, value);
    } else if (field == "@reload") {
      mod.reload = value;
    } else if (field == "@edit") {
      mod.noEdit = value;
    } else if (field == "@mod") {
      const auto parts = splitFields(value, 2);
      mod.name = parts[0];
      mod.about = parts[1];
    } else if (field == "@numbers") {
      mod.hexNumbers = lower(value) == "hex";
    } else if (field[0] != '@') {
      const auto parts = splitFields(value, 5);
      KnownBinding binding;
      binding.modifiers = parts[3];
      binding.padField = parts[4];
      const auto slash = field.find('/');
      binding.section = slash == field.npos ? "" : lower(trimConfigLine(field.substr(0, slash)));
      binding.key = lower(trimConfigLine(slash == field.npos ? field : field.substr(slash + 1)));
      binding.info = {kindFromName(parts[0]), parts[1], parts[2]};
      if (!binding.key.empty())
        mod.bindings.push_back(std::move(binding));
    }
  }
  // An entry for one section takes what it leaves out (modifiers, what it
  // does, the controller setting) from the most specific * entry covering
  // it. One that says - for any of them takes nothing there: it has none.
  const auto fields = [](KnownBinding &b) {
    return std::vector<std::string *>{&b.modifiers, &b.padField, &b.info.description};
  };
  for (auto &mod : mods) {
    for (auto &binding : mod.bindings) {
      if (binding.section.empty() || binding.section.find('*') != binding.section.npos)
        continue;
      KnownBinding *general = nullptr;
      for (auto &other : mod.bindings)
        if (other.key == binding.key && other.section.find('*') != other.section.npos &&
            sectionMatches(other.section, binding.section) &&
            (!general || sectionScore(other.section) > sectionScore(general->section)))
          general = &other;
      if (!general)
        continue;
      const auto to = fields(binding), from = fields(*general);
      for (size_t i = 0; i < to.size(); ++i)
        if (to[i]->empty())
          *to[i] = *from[i];
    }
    for (auto &binding : mod.bindings)
      for (auto *field : fields(binding))
        if (*field == "-")
          field->clear();
  }
  return mods;
}

#include "known_mods_data.h" // builtInKnownMods, generated by scripts/build.py
// The list built into the plugin, which every install has. A
// ModActivator.KnownMods.ini in the ModActivator folder is read as well, but
// only adds to it: config files the built-in list does not know, and
// reserved keys. So a mod author can try an entry before sending it in,
// while an older copy can never replace what is there.
#ifdef MODACTIVATOR_TESTS
// Set by the tests to see mods as ModActivator sees one it has never heard of.
static bool testsIgnoreKnownMods = false;
#endif
static KnownMods loadKnownMods() {
#ifdef MODACTIVATOR_TESTS
  if (testsIgnoreKnownMods)
    return {};
#endif
  // The built-in list never changes while the game runs: parsed once.
  static const KnownMods builtIn = parseKnownMods(builtInKnownMods);
  auto mods = builtIn;
  std::ifstream file(dataPath("ModActivator.KnownMods.ini"), std::ios::binary);
  if (!file)
    return mods;
  std::string text(std::istreambuf_iterator<char>(file), {});
  for (auto &extra : parseKnownMods(text)) {
    const auto same = std::find_if(mods.begin(), mods.end(),
                                   [&](const KnownMod &m) { return m.file == extra.file; });
    if (same == mods.end())
      mods.push_back(std::move(extra));
    else if (extra.file == "@reserved")
      same->reserved.insert(same->reserved.end(), extra.reserved.begin(), extra.reserved.end());
  }
  return mods;
}

// Keys that something other than a mod already uses: the known-mods list's
// [@reserved] section (Steam, Windows), then yours, [ReservedKeys] in
// ModActivator.User.ini, where you can add the game's own keys:
//   I = Game: inventory
struct ReservedKey {
  std::vector<int> keys;
  std::string label; // as bindings' labels are: shown through displayText
};
static std::vector<ReservedKey> loadReservedKeys() {
  std::vector<ReservedKey> out;
  const auto add = [&](const std::string &spelling, const std::string &label) {
    auto keys = parseShortcut(spelling);
    if (validShortcut(keys) && !label.empty())
      out.push_back({std::move(keys), label});
  };
  for (const auto &mod : loadKnownMods())
    for (const auto &[keys, label] : mod.reserved)
      add(keys, label);
  std::vector<char> section(32768);
  GetPrivateProfileSectionA("ReservedKeys", section.data(), (DWORD)section.size(),
                            userConfigPath().c_str());
  for (const char *line = section.data(); *line; line += strlen(line) + 1)
    if (const char *eq = strchr(line, '='); eq && eq != line)
      add(trimConfigLine(std::string(line, eq)), trimConfigLine(eq + 1));
  return out;
}

// The entry for a binding's config file: the same file name, or for an entry
// with a folder in it, the same tail of the path.
static const KnownMod *knownModFor(const KnownMods &mods, const Action &action) {
  const auto file = lower(action.file);
  for (const auto &mod : mods)
    if (file == mod.file || (file.size() > mod.file.size() &&
                             file.compare(file.size() - mod.file.size(), mod.file.size(), mod.file) == 0 &&
                             file[file.size() - mod.file.size() - 1] == '/'))
      return &mod;
  return nullptr;
}
// The most specific entry: an exact section, then a pattern, then any section.
static const KnownBinding *knownBindingFor(const KnownMod &mod, const std::string &section,
                                           const std::string &key) {
  const KnownBinding *best = nullptr;
  int bestScore = -1;
  const auto sec = lower(section), k = lower(key);
  for (const auto &binding : mod.bindings) {
    if (binding.key != k || !sectionMatches(binding.section, sec))
      continue;
    const int score = sectionScore(binding.section);
    if (score > bestScore) {
      best = &binding;
      bestScore = score;
    }
  }
  return best;
}

// --- 2. the mod's own export -------------------------------------------------
struct ModExport {
  std::string file; // lower case file name, empty for any of the mod's configs
  std::string section, key;
  BindingInfo info;
};

// Another module's pointers may be bad: unloaded, freed or simply wrong. Every
// byte is checked before it is read, since __try/__except does not catch
// faults in this MinGW build: a bad read would take the whole game down.

// The end of the readable memory starting at address, or address itself when
// none is: committed pages that can be read and are not guard pages.
static uintptr_t readableUntil(uintptr_t address) {
  MEMORY_BASIC_INFORMATION info{};
  if (!address || !VirtualQuery(reinterpret_cast<const void *>(address), &info, sizeof info))
    return address;
  constexpr DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                             PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
  if (info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
      !(info.Protect & readable))
    return address;
  return reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
}
static bool readableRange(const void *pointer, size_t size) {
  auto address = reinterpret_cast<uintptr_t>(pointer);
  const auto end = address + size;
  if (!pointer || end < address)
    return false;
  while (address < end) {
    const auto until = readableUntil(address);
    if (until <= address)
      return false;
    address = until;
  }
  return true;
}
// Copies a C string, truncated to capacity. False when it runs into memory
// that cannot be read before its end (or before capacity fills).
static bool copyForeign(const char *source, char *target, size_t capacity) {
  target[0] = 0;
  if (!source)
    return true;
  auto address = reinterpret_cast<uintptr_t>(source);
  uintptr_t until = address;
  size_t n = 0;
  while (n + 1 < capacity) {
    if (address + n >= until && (until = readableUntil(address + n)) <= address + n) {
      target[0] = 0;
      return false;
    }
    const char c = source[n];
    if (!c)
      break;
    target[n++] = c;
  }
  target[n] = 0;
  return true;
}
static bool readForeignEntry(const ModActivatorBindingV1 *entry, ModActivatorBindingV1 *copy) {
  if (!readableRange(entry, sizeof *entry))
    return false;
  *copy = *entry;
  return true;
}

static std::vector<ModExport> readModExport(HMODULE module) {
  const auto fn = module ? reinterpret_cast<ModActivatorBindingsV1Fn>(
                               GetProcAddress(module, "ModActivatorBindingsV1"))
                         : nullptr;
  if (!fn)
    return {};
  // The call itself runs the mod's own code; a mod that faults in there would
  // crash the game with or without ModActivator. What it returns is checked.
  unsigned count = 0;
  const auto *entries = fn(&count);
  std::vector<ModExport> out;
  if (!entries)
    return out;
  char file[260], section[256], key[256], name[256], description[2048];
  for (unsigned i = 0; i < count && i < 512; ++i) {
    ModActivatorBindingV1 entry{};
    if (!readForeignEntry(entries + i, &entry) ||
        !copyForeign(entry.file, file, sizeof file) ||
        !copyForeign(entry.section, section, sizeof section) ||
        !copyForeign(entry.key, key, sizeof key) ||
        !copyForeign(entry.name, name, sizeof name) ||
        !copyForeign(entry.description, description, sizeof description))
      break;
    if (!key[0])
      continue;
    // Text is UTF-8 by contract; anything else is shown as nothing.
    const auto text = [](const char *s) { return validUtf8(s) ? std::string(s) : std::string(); };
    std::string fileName = lower(file);
    if (const auto slash = fileName.find_last_of('/'); slash != fileName.npos)
      fileName.erase(0, slash + 1);
    out.push_back({fileName, lower(section), lower(key),
                   {entry.kind >= 1 && entry.kind <= 3 ? entry.kind : 0, text(name),
                    text(description)}});
  }
  return out;
}
static const ModExport *exportFor(const std::vector<ModExport> &exports, const Action &action) {
  auto file = lower(action.file);
  if (const auto slash = file.find_last_of('/'); slash != file.npos)
    file.erase(0, slash + 1);
  const auto section = lower(action.section), key = lower(action.key);
  const ModExport *best = nullptr;
  for (const auto &e : exports)
    if (e.key == key && (e.section.empty() || e.section == section) &&
        (e.file.empty() || e.file == file) &&
        (!best || (!e.section.empty() && best->section.empty())))
      best = &e;
  return best;
}

// --- 5. readmes --------------------------------------------------------------
// Whether a file name is the mod's own name followed by nothing or by
// something other than a letter or digit: "ReadmeMod readme.txt" and
// "ReadmeMod.txt" for ReadmeMod, but not "ReadmeModPlus.txt".
static bool namedAfter(const std::string &name, const std::string &moduleStem) {
  return !moduleStem.empty() && name.compare(0, moduleStem.size(), moduleStem) == 0 &&
         (name.size() == moduleStem.size() ||
          !isalnum(static_cast<unsigned char>(name[moduleStem.size()])));
}
// Text files beside a mod's config that are about the mod: named after it,
// or, in a folder of the mod's own, any readme*. In a folder every mod shares
// (bin64 itself) a readme could be anyone's, so only the mod's name counts.
// Licences and notices never do.
static std::vector<std::string> readmeLines(const std::filesystem::path &directory,
                                            const std::string &moduleStem, bool shared) {
  std::vector<std::string> lines;
  std::error_code ec;
  int files = 0;
  for (std::filesystem::directory_iterator it(directory, ec), end; it != end && !ec && files < 8;
       it.increment(ec)) {
    if (!representableInAnsi(it->path().native()))
      continue;
    const auto name = lower(it->path().filename().string());
    const auto extension = lower(it->path().extension().string());
    if ((extension != ".txt" && extension != ".md") || name.find("licen") != name.npos ||
        name.find("notice") != name.npos)
      continue;
    const bool generic = name.find("readme") != name.npos || name.find("read me") != name.npos;
    if (!namedAfter(name, moduleStem) && (shared || !generic))
      continue;
    std::error_code sizeError;
    if (!it->is_regular_file(sizeError) || it->file_size(sizeError) > 256 * 1024 || sizeError)
      continue;
    std::ifstream file(it->path(), std::ios::binary);
    std::string text(std::istreambuf_iterator<char>(file), {});
    if (text.compare(0, 3, "\xef\xbb\xbf") == 0)
      text.erase(0, 3);
    else if (!validUtf8(text))
      text = displayText(text);
    if (text.find('\0') != text.npos)
      continue; // UTF-16 readmes are rare; not worth guessing at
    ++files;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line))
      lines.push_back(trimConfigLine(line));
  }
  return lines;
}

static bool wordChar(char c) {
  return isalnum(static_cast<unsigned char>(c)) || c == '_';
}
// The readme's words about a setting: the rest of the first line that names
// it as a whole word, or the next line when that one is only the name.
static std::string readmeDescription(const std::vector<std::string> &lines, const std::string &key) {
  const auto k = lower(key);
  for (size_t i = 0; i < lines.size(); ++i) {
    const auto folded = lower(lines[i]);
    size_t at = folded.find(k);
    while (at != folded.npos && ((at && wordChar(folded[at - 1])) ||
                                 (at + k.size() < folded.size() && wordChar(folded[at + k.size()]))))
      at = folded.find(k, at + 1);
    if (at == folded.npos)
      continue;
    std::string rest = lines[i].substr(at + k.size());
    // Skip "= F8" (the setting and its value), then ": ", " - " and the like
    // between the name and the words.
    size_t p = 0;
    const auto space = [&](size_t q) { return q < rest.size() && (rest[q] == ' ' || rest[q] == '\t'); };
    while (space(p))
      ++p;
    if (p < rest.size() && rest[p] == '=') {
      ++p;
      while (space(p))
        ++p;
      while (p < rest.size() && !space(p))
        ++p;
    }
    rest.erase(0, p);
    const auto words = rest.find_first_not_of(" \t:;-=|*#>()`'\"");
    rest = words == rest.npos ? std::string() : trimConfigLine(rest.substr(words));
    if (rest.size() < 4 && i + 1 < lines.size() && !lines[i + 1].empty() &&
        lines[i + 1][0] != '[' && lines[i + 1].find('=') == lines[i + 1].npos)
      rest = lines[i + 1];
    if (rest.size() >= 4)
      return rest.size() > 300 ? rest.substr(0, 297) + "..." : rest;
  }
  return {};
}

// --- everything, in order ----------------------------------------------------
// The sources that do not depend on the binding's own file, gathered once per
// scan: the database, each loaded mod's export, and readmes by folder.
struct DescriptionSources {
  KnownMods known = loadKnownMods();
  std::map<std::string, std::vector<ModExport>> exports; // by lower-case module
  std::map<std::string, std::vector<std::string>> readmes; // by folder and module
  std::map<std::string, std::shared_ptr<const BinaryInfo>> binaries; // by lower-case module

  // At the end of the scan, what new or changed mods said goes to disk.
  ~DescriptionSources() {
    try {
      binary_cache::save();
    } catch (...) {
    }
  }

  const std::vector<ModExport> &exportsFor(const std::string &module) {
    const auto key = lower(module);
    auto it = exports.find(key);
    if (it == exports.end())
      it = exports.emplace(key, readModExport(GetModuleHandleA(module.c_str()))).first;
    return it->second;
  }
  const std::vector<std::string> &readmesFor(const std::filesystem::path &config,
                                             const std::string &module) {
    const auto stem = lower(std::filesystem::path(module).stem().string());
    const auto directory = config.parent_path();
    const auto key = lower(directory.string()) + "|" + stem;
    auto it = readmes.find(key);
    if (it == readmes.end()) {
      // The scan's root: folder ends in a separator, so its parent_path is
      // the folder itself.
      const bool shared = lower(directory.string()) ==
                          lower(std::filesystem::path(folder).parent_path().string());
      it = readmes.emplace(key, readmeLines(directory, stem, shared)).first;
    }
    return it->second;
  }
  // What the mod's file says: the loaded module's file, or for a mod not
  // loaded, a file of that name beside the config file or in bin64.
  const BinaryInfo *binaryFor(const std::string &module, const std::filesystem::path &config) {
    const auto key = lower(module);
    auto it = binaries.find(key);
    if (it == binaries.end()) {
      std::shared_ptr<const BinaryInfo> info;
      const auto extension = lower(std::filesystem::path(module).extension().string());
      if (extension == ".asi" || extension == ".addon64") {
        std::vector<std::filesystem::path> paths;
        if (const HMODULE loaded = GetModuleHandleA(module.c_str())) {
          wchar_t path[MAX_PATH * 4]{};
          const DWORD n = GetModuleFileNameW(loaded, path, (DWORD)std::size(path));
          if (n && n < std::size(path))
            paths.emplace_back(path);
        }
        paths.push_back(config.parent_path() / module);
        paths.push_back(std::filesystem::path(folder) / module);
        for (const auto &path : paths) {
          std::error_code ec;
          if (std::filesystem::is_regular_file(path, ec) && (info = binaryInfo(path)))
            break;
        }
      }
      it = binaries.emplace(key, std::move(info)).first;
    }
    return it->second.get();
  }
};

// The kind a source gave, unless you chose one in Settings.
static void applyKind(Action &action, int kind) {
  if (action.typeOverride || kind <= 0)
    return;
  action.menuRank = kind == 1 ? 150 : 0;
  action.featureRank = kind == 2 ? 150 : 0;
  action.chosenKind = true;
}

// Before the binding's value is read: the database can say how its numbers
// are written. After classifyBinding, which sets the comment's description.
static void describeBinding(Action &action, DescriptionSources &sources,
                            const std::filesystem::path &config) {
  const KnownMod *mod = knownModFor(sources.known, action);
  const KnownBinding *known = mod ? knownBindingFor(*mod, action.section, action.key) : nullptr;
  const ModExport *own = exportFor(sources.exportsFor(action.module), action);
  const auto user = userInfo(action);
  // The mod's file is read only for what nothing stronger gives: its name and
  // about text, and a description and kind where the comment, the database
  // and the mod's export all leave them out.
  const auto settles = [](const BindingInfo *info) {
    return info && !info->description.empty() && info->kind > 0;
  };
  const bool needBinary = !mod || mod->name.empty() || mod->about.empty() ||
                          (action.description.empty() && !settles(known ? &known->info : nullptr) &&
                           !settles(own ? &own->info : nullptr));
  const BinaryInfo *binary = needBinary ? sources.binaryFor(action.module, config) : nullptr;
  if (mod) {
    action.modName = mod->name;
    action.modAbout = mod->about;
    action.hexNumbers = mod->hexNumbers;
    action.reload = mod->reload;
    action.noEdit = mod->noEdit;
  }
  if (binary && action.modName.empty())
    action.modName = binary->product;
  if (binary && action.modAbout.empty())
    action.modAbout = binary->about;
  if (known) {
    action.modifierRule = known->modifiers;
    action.padField = known->padField;
  }
  if (!action.description.empty())
    action.descriptionSource = sourceComment;
  // Default config text or a settings schema built into the mod reads like
  // the config file's own comments, so it can say the shortcut opens a
  // window as they do.
  if (action.description.empty() && binary) {
    std::string hint;
    if (const auto *comment = binaryTemplate(*binary, action.section, action.key)) {
      action.description = comment->description;
      hint = comment->classificationHint;
    } else {
      hint = action.description = binarySchemaText(*binary, action.section, action.key);
    }
    if (!action.description.empty())
      action.descriptionSource = sourceBinary;
    if (!action.typeOverride && action.menuRank != 120 && opensInterface(hint)) {
      action.menuRank = 120;
      action.featureRank = 0;
    }
  }
  // Weakest first, so each stronger source replaces what it has.
  if (action.description.empty()) {
    action.description = readmeDescription(sources.readmesFor(config, action.module), action.key);
    if (!action.description.empty())
      action.descriptionSource = sourceReadme;
  }
  if (action.description.empty() && binary) {
    action.description = binarySentence(*binary, action.key);
    if (!action.description.empty())
      action.descriptionSource = sourceBinary;
  }
  for (const auto &[info, source] : {std::pair<const BindingInfo *, int>{known ? &known->info : nullptr, sourceKnownMods},
                                     {own ? &own->info : nullptr, sourceMod},
                                     {&user, sourceUser}}) {
    if (!info)
      continue;
    // A name of * is the part of the section the entry's * matched, or the
    // whole section: Shortcut.* names [Shortcut.storage.open.private]
    // storage.open.private.
    if (known && info == &known->info && info->name == "*")
      action.customName = displayText(sectionWildcardPart(known->section, action.section));
    else if (!info->name.empty())
      action.customName = info->name;
    if (!info->description.empty()) {
      action.description = info->description;
      action.descriptionSource = source;
    }
    applyKind(action, info->kind);
  }
}

// The controller combination for a binding, read after its value. Where it
// is, in order: a setting the known-mods list names; the value's own suffix
// (F8|LeftStick,RightStick,A); a setting beside it in the same section whose
// name pairs with the key's (ToggleKey and PadToggle, StanceCycleKey and
// StanceCycleButtons, HotkeyToggleEffect and ControllerToggleEffect,
// PrivateHotkey and PrivateControllerButton). section maps the section's
// lower-case setting names to their values.
static void findPadBinding(Action &action, const std::map<std::string, std::string> &section) {
  action.padButtons = 0;
  if (!action.padField.empty()) {
    const auto it = section.find(lower(action.padField));
    if (it != section.end())
      action.padButtons = padButtonsFrom(it->second);
    return;
  }
  if (const auto bar = action.cachedValue.find('|'); bar != action.cachedValue.npos) {
    auto suffix = action.cachedValue.substr(bar + 1);
    suffix = suffix.substr(0, bindingComment(suffix));
    if ((action.padButtons = padButtonsFrom(trimConfigLine(suffix))) != 0) {
      action.padField = action.key;
      return;
    }
  }
  // The action's own word: the key without hotkey/key at either end.
  auto base = lower(action.key);
  for (std::string_view suffix : {"hotkeyvk", "hotkeycode", "hotkey", "keycode", "keyvk", "key"})
    if (base.size() > suffix.size() &&
        base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0) {
      base.resize(base.size() - suffix.size());
      break;
    }
  for (std::string_view prefix : {"hotkey", "key"})
    if (base.size() > prefix.size() && base.compare(0, prefix.size(), prefix) == 0) {
      base.erase(0, prefix.size());
      break;
    }
  if (base.empty() || base == "hotkey" || base == "key")
    return;
  for (const auto &name : {base + "controllerbuttons", base + "controllerbutton", base + "buttons",
                           base + "button", base + "controller", base + "gamepad", base + "pad",
                           "pad" + base, "controller" + base, "gamepad" + base}) {
    const auto it = section.find(name);
    if (it == section.end())
      continue;
    if ((action.padButtons = padButtonsFrom(it->second)) != 0) {
      action.padField = name;
      return;
    }
  }
}

// Whether a database entry or the mod's export names this field, so it is
// read as a shortcut whatever it is called.
static bool describedField(DescriptionSources &sources, const Action &probe) {
  if (const auto *mod = knownModFor(sources.known, probe))
    if (const auto *known = knownBindingFor(*mod, probe.section, probe.key))
      return known->info.kind != kindIgnore;
  return exportFor(sources.exportsFor(probe.module), probe) != nullptr;
}
// Whether the database says a field that looks like a shortcut is not one.
static bool ignoredField(DescriptionSources &sources, const Action &probe) {
  if (const auto *mod = knownModFor(sources.known, probe))
    if (const auto *known = knownBindingFor(*mod, probe.section, probe.key))
      return known->info.kind == kindIgnore;
  return false;
}
