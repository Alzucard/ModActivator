#pragma once
// What a binding does, and what to call it, from every source ModActivator
// knows. In order of trust:
//
//   1. your own name and note, saved in ModActivator.User.ini
//   2. the mod itself, through the optional ModActivatorBindingsV1 export
//      (see include/ModActivatorBindings.h)
//   3. ModActivator.KnownMods.ini, shipped beside the plugin
//   4. the comment beside the setting in the mod's config file
//   5. a readme next to the mod's config file
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
};

static const char *sourceLabel(int source) {
  switch (source) {
  case sourceReadme: return "From the mod's readme";
  case sourceComment: return "From the mod's config file";
  case sourceKnownMods: return "From ModActivator's list of known mods";
  case sourceMod: return "From the mod itself";
  case sourceUser: return "Your note";
  }
  return "";
}

// Binding kinds, as the database and the mod export name them. The numbers
// are the export's (ModActivatorKind*) and match the Settings tab's
// "Show as" types 1 to 3.
static int kindFromName(const std::string &name) {
  const auto k = lower(name);
  return k == "menu" ? 1 : k == "toggle" ? 2 : k == "action" ? 3 : 0;
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
                                    userConfigPath().c_str()) != FALSE;
}
static BindingInfo userInfo(const Action &action) {
  return {0, readUserText("BindingNames", action), readUserText("BindingNotes", action)};
}

// --- 3. ModActivator.KnownMods.ini -------------------------------------------
struct KnownBinding {
  std::string section, key; // lower case; section empty for any, "x*" for a prefix
  BindingInfo info;
};
struct KnownMod {
  std::string file; // lower case, '/' separated
  std::string name, about;
  bool hexNumbers = false;
  std::vector<KnownBinding> bindings;
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
    if (field == "@mod") {
      const auto parts = splitFields(value, 2);
      mod.name = parts[0];
      mod.about = parts[1];
    } else if (field == "@numbers") {
      mod.hexNumbers = lower(value) == "hex";
    } else if (field[0] != '@') {
      const auto parts = splitFields(value, 3);
      KnownBinding binding;
      const auto slash = field.find('/');
      binding.section = slash == field.npos ? "" : lower(trimConfigLine(field.substr(0, slash)));
      binding.key = lower(trimConfigLine(slash == field.npos ? field : field.substr(slash + 1)));
      binding.info = {kindFromName(parts[0]), parts[1], parts[2]};
      if (!binding.key.empty())
        mod.bindings.push_back(std::move(binding));
    }
  }
  return mods;
}

static KnownMods loadKnownMods() {
  std::ifstream file(folder + "ModActivator.KnownMods.ini", std::ios::binary);
  if (!file)
    return {};
  std::string text(std::istreambuf_iterator<char>(file), {});
  return parseKnownMods(text);
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
static bool sectionMatches(const std::string &pattern, const std::string &section) {
  if (pattern.empty())
    return true;
  if (pattern.back() == '*')
    return section.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0;
  return pattern == section;
}
// The most specific entry: an exact section, then a prefix, then any section.
static const KnownBinding *knownBindingFor(const KnownMod &mod, const std::string &section,
                                           const std::string &key) {
  const KnownBinding *best = nullptr;
  int bestScore = -1;
  const auto sec = lower(section), k = lower(key);
  for (const auto &binding : mod.bindings) {
    if (binding.key != k || !sectionMatches(binding.section, sec))
      continue;
    const int score = binding.section.empty() ? 0 : binding.section.back() == '*' ? 1 : 2;
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

// Copies a C string from another module, which may be bad memory. No C++
// objects live in here, so structured exception handling can guard it.
static bool copyForeign(const char *source, char *target, size_t capacity) {
  target[0] = 0;
  if (!source)
    return true;
  __try {
    size_t n = 0;
    while (n + 1 < capacity && source[n]) {
      target[n] = source[n];
      ++n;
    }
    target[n] = 0;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    target[0] = 0;
    return false;
  }
}
static const ModActivatorBindingV1 *callExport(ModActivatorBindingsV1Fn fn, unsigned *count) {
  __try {
    return fn(count);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    *count = 0;
    return nullptr;
  }
}
static bool readForeignEntry(const ModActivatorBindingV1 *entry, ModActivatorBindingV1 *copy) {
  __try {
    *copy = *entry;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static std::vector<ModExport> readModExport(HMODULE module) {
  const auto fn = module ? reinterpret_cast<ModActivatorBindingsV1Fn>(
                               GetProcAddress(module, "ModActivatorBindingsV1"))
                         : nullptr;
  if (!fn)
    return {};
  unsigned count = 0;
  const auto *entries = callExport(fn, &count);
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
// Text files beside a mod's config that are about the mod: readme*, or named
// after the mod. Licences and notices are not.
static std::vector<std::string> readmeLines(const std::filesystem::path &directory,
                                            const std::string &moduleStem) {
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
    if (name.find("readme") == name.npos && name.find("read me") == name.npos &&
        (moduleStem.empty() || name.compare(0, moduleStem.size(), moduleStem) != 0))
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
    if (it == readmes.end())
      it = readmes.emplace(key, readmeLines(directory, stem)).first;
    return it->second;
  }
};

// The kind a source gave, unless you chose one in Settings.
static void applyKind(Action &action, int kind) {
  if (action.typeOverride || !kind)
    return;
  action.menuRank = kind == 1 ? 150 : 0;
  action.featureRank = kind == 2 ? 150 : 0;
}

// Before the binding's value is read: the database can say how its numbers
// are written. After classifyBinding, which sets the comment's description.
static void describeBinding(Action &action, DescriptionSources &sources,
                            const std::filesystem::path &config) {
  const KnownMod *mod = knownModFor(sources.known, action);
  const KnownBinding *known = mod ? knownBindingFor(*mod, action.section, action.key) : nullptr;
  const ModExport *own = exportFor(sources.exportsFor(action.module), action);
  const auto user = userInfo(action);
  if (mod) {
    action.modName = mod->name;
    action.modAbout = mod->about;
    action.hexNumbers = mod->hexNumbers;
  }
  if (!action.description.empty())
    action.descriptionSource = sourceComment;
  // Weakest first, so each stronger source replaces what it has.
  if (action.description.empty()) {
    action.description = readmeDescription(sources.readmesFor(config, action.module), action.key);
    if (!action.description.empty())
      action.descriptionSource = sourceReadme;
  }
  for (const auto &[info, source] : {std::pair<const BindingInfo *, int>{known ? &known->info : nullptr, sourceKnownMods},
                                     {own ? &own->info : nullptr, sourceMod},
                                     {&user, sourceUser}}) {
    if (!info)
      continue;
    if (!info->name.empty())
      action.customName = info->name;
    if (!info->description.empty()) {
      action.description = info->description;
      action.descriptionSource = source;
    }
    applyKind(action, info->kind);
  }
}

// Whether a database entry or the mod's export names this field, so it is
// read as a shortcut whatever it is called.
static bool describedField(DescriptionSources &sources, const Action &probe) {
  if (const auto *mod = knownModFor(sources.known, probe))
    if (knownBindingFor(*mod, probe.section, probe.key))
      return true;
  return exportFor(sources.exportsFor(probe.module), probe) != nullptr;
}
