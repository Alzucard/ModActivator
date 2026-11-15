#pragma once
// What a mod's own file says about its shortcuts, for mods that are neither in
// the known-mods list nor export ModActivatorBindingsV1. The file is read from
// disk as data, the way a virus scanner reads it: nothing in it is run. What
// it can give, for any mod:
//
//   - its version resource: the product's name, and a line about it
//   - default config text built into it, with the comment above each setting
//     ("; FastStart=true enables automatic startup ...")
//   - a settings schema built in as JSON, with a label or help per setting
//   - sentences that name a setting, such as "Stance keys: ... (StanceCycleKey)"
//   - the words in the code that reads the keyboard or controller: "enabled"
//     and "disabled" there say a shortcut switches something on and off,
//     "opened" and "closed" that it opens a window.
//
// Each file is read once per size and modification time.
#include <unordered_map>
#include <unordered_set>

struct BinaryInfo {
  std::string product, about;
  BindingComments templates; // section "" where the text names none
  struct SchemaEntry {
    std::string key;                // lower case
    std::vector<std::string> path;  // the member names above it, lower case
    std::string text;
  };
  std::vector<SchemaEntry> schema;
  std::vector<std::string> texts; // strings that read as words, for sentences
  int kindHint = 0;               // 1 menu, 2 toggle, from the input code
};

namespace binary {

struct Section {
  uint32_t va = 0, size = 0, raw = 0, rawSize = 0, flags = 0;
};
constexpr uint32_t executable = 0x20000000;

struct Image {
  const std::string &d;
  std::vector<Section> sections;
  uint32_t dirs[16][2]{};

  explicit Image(const std::string &data) : d(data) {}
  template <class T> bool get(size_t off, T &out) const {
    if (off > d.size() || d.size() - off < sizeof(T))
      return false;
    memcpy(&out, d.data() + off, sizeof(T));
    return true;
  }
  bool parse() {
    uint32_t e = 0, sig = 0;
    uint16_t machine = 0, count = 0, optSize = 0, magic = 0;
    if (d.size() < 0x40 || d.compare(0, 2, "MZ") != 0 || !get(0x3C, e) || !get(e, sig) ||
        sig != 0x4550 || !get(e + 4, machine) || machine != 0x8664 || !get(e + 6, count) ||
        !get(e + 20, optSize) || !get(e + 24, magic) || magic != 0x20b)
      return false;
    const size_t opt = e + 24;
    uint32_t dirCount = 0;
    get(opt + 108, dirCount);
    for (uint32_t i = 0; i < dirCount && i < 16; ++i) {
      get(opt + 112 + 8 * i, dirs[i][0]);
      get(opt + 116 + 8 * i, dirs[i][1]);
    }
    for (uint16_t i = 0; i < count && i < 96; ++i) {
      const size_t o = opt + optSize + 40 * size_t(i);
      Section s;
      uint32_t vsize = 0;
      if (!get(o + 8, vsize) || !get(o + 12, s.va) || !get(o + 16, s.rawSize) ||
          !get(o + 20, s.raw) || !get(o + 36, s.flags))
        return false;
      if (s.raw >= d.size())
        s.rawSize = 0;
      s.rawSize = (uint32_t)std::min<size_t>(s.rawSize, d.size() - std::min<size_t>(s.raw, d.size()));
      s.size = std::max(vsize, s.rawSize);
      sections.push_back(s);
    }
    return !sections.empty();
  }
  bool offset(uint32_t rva, size_t &out) const {
    for (const auto &s : sections)
      if (rva >= s.va && rva - s.va < s.size) {
        if (rva - s.va >= s.rawSize)
          return false;
        out = size_t(s.raw) + (rva - s.va);
        return true;
      }
    return false;
  }
  std::string cstr(uint32_t rva, size_t cap = 256) const {
    size_t o = 0;
    if (!offset(rva, o))
      return {};
    const auto end = d.find('\0', o);
    return d.substr(o, std::min(cap, (end == d.npos ? d.size() : end) - o));
  }
};

// Import slots of the functions that read the keyboard or a controller.
static std::unordered_set<uint32_t> inputSlots(const Image &img) {
  static const std::set<std::string> input = {
      "GetAsyncKeyState", "GetKeyState", "GetKeyboardState", "XInputGetState", "GetRawInputData"};
  std::unordered_set<uint32_t> slots;
  size_t o = 0;
  if (!img.dirs[1][0] || !img.offset(img.dirs[1][0], o))
    return slots;
  for (int n = 0; n < 1024; ++n, o += 20) {
    uint32_t lookup = 0, name = 0, iat = 0;
    if (!img.get(o, lookup) || !img.get(o + 12, name) || !img.get(o + 16, iat) || !name)
      break;
    size_t t = 0;
    if (!img.offset(lookup ? lookup : iat, t))
      continue;
    for (int i = 0; i < 8192; ++i, t += 8) {
      uint64_t v = 0;
      if (!img.get(t, v) || !v)
        break;
      if (!(v >> 63) && input.count(img.cstr(uint32_t(v & 0x7fffffff) + 2)))
        slots.insert(iat + 8 * i);
    }
  }
  return slots;
}

// Function bounds, from the exception table every x64 function with a frame
// is listed in.
static std::vector<std::pair<uint32_t, uint32_t>> functions(const Image &img) {
  std::vector<std::pair<uint32_t, uint32_t>> out;
  size_t o = 0;
  if (!img.dirs[3][0] || !img.offset(img.dirs[3][0], o))
    return out;
  for (uint32_t i = 0; i < img.dirs[3][1] / 12 && i < 2000000; ++i) {
    uint32_t b = 0, e = 0;
    if (!img.get(o + 12 * size_t(i), b) || !img.get(o + 12 * size_t(i) + 4, e))
      break;
    if (b && e > b && e - b < 1 << 20)
      out.emplace_back(b, e);
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

// The version resource's StringFileInfo value for a name, found by its
// UTF-16 key inside the resource section.
static std::string versionString(const Image &img, const char *key) {
  size_t start = 0;
  if (!img.dirs[2][0] || !img.offset(img.dirs[2][0], start))
    return {};
  const size_t end = std::min(img.d.size(), start + img.dirs[2][1]);
  std::string pattern;
  for (const char *c = key; *c; ++c)
    pattern += *c, pattern += '\0';
  pattern += std::string(2, '\0');
  const auto at = img.d.find(pattern, start);
  if (at == img.d.npos || at >= end)
    return {};
  size_t v = at + pattern.size();
  v += (4 - (v - start) % 4) % 4;
  std::wstring text;
  for (; v + 1 < end && text.size() < 512; v += 2) {
    const wchar_t c = wchar_t((unsigned char)img.d[v] | ((unsigned char)img.d[v + 1] << 8));
    if (!c)
      break;
    text += c;
  }
  return trimConfigLine(narrowText(text.data(), (int)text.size(), CP_UTF8));
}

struct Strings {
  std::vector<std::string> text;
  std::unordered_map<uint32_t, uint32_t> at; // start rva -> index
};
// NUL-terminated runs of text in the data sections: UTF-8 bytes, and UTF-16
// text in the Latin-1 range.
static Strings strings(const Image &img) {
  Strings out;
  const auto &d = img.d;
  const auto add = [&](uint32_t rva, std::string s) {
    if (out.text.size() < 400000 && out.at.emplace(rva, (uint32_t)out.text.size()).second)
      out.text.push_back(std::move(s));
  };
  const auto plain = [](unsigned c) { return c >= 0x20 ? c != 0x7f : c == '\t' || c == '\r' || c == '\n'; };
  for (const auto &s : img.sections) {
    if (s.flags & executable)
      continue;
    const size_t begin = s.raw, end = size_t(s.raw) + s.rawSize;
    for (size_t i = begin; i < end;) {
      size_t j = i;
      while (j < end && plain((unsigned char)d[j]))
        ++j;
      if (j < end && !d[j] && j - i >= 5 && j - i <= 1048576) {
        auto text = d.substr(i, j - i);
        if (validUtf8(text))
          add(s.va + uint32_t(i - begin), std::move(text));
      }
      i = j + 1;
    }
    for (size_t i = begin; i + 1 < end;) {
      size_t j = i;
      std::wstring wide;
      while (j + 1 < end && !d[j + 1] && plain((unsigned char)d[j]) && wide.size() < 65536)
        wide += wchar_t((unsigned char)d[j]), j += 2;
      if (j + 1 < end && !d[j] && !d[j + 1] && wide.size() >= 5)
        add(s.va + uint32_t(i - begin), narrowText(wide.data(), (int)wide.size(), CP_UTF8));
      i = j + 2;
    }
  }
  return out;
}

static bool readsAsWords(const std::string &s, size_t minimum) {
  if (s.size() < minimum || s.find(' ') == s.npos)
    return false;
  size_t letters = 0;
  for (unsigned char c : s)
    letters += std::isalpha(c) || c == ' ' || c >= 0x80;
  return letters * 10 >= s.size() * 6;
}

// --- JSON, enough for a settings schema --------------------------------------
struct Json {
  enum Type { null, string, object, array, other } type = null;
  std::string text;
  std::vector<std::pair<std::string, Json>> members; // object members, or array items
};
struct JsonParser {
  const std::string &s;
  size_t p = 0;
  int depth = 0;
  void space() {
    while (p < s.size() && std::isspace((unsigned char)s[p]))
      ++p;
  }
  bool string(std::string &out) {
    if (p >= s.size() || s[p] != '"')
      return false;
    for (++p; p < s.size(); ++p) {
      char c = s[p];
      if (c == '"') {
        ++p;
        return true;
      }
      if (c != '\\') {
        out += c;
        continue;
      }
      if (++p >= s.size())
        return false;
      switch (c = s[p]) {
      case 'n': out += '\n'; break;
      case 't': out += '\t'; break;
      case 'r': case 'b': case 'f': break;
      case 'u': {
        if (p + 4 >= s.size())
          return false;
        const wchar_t w = (wchar_t)std::strtoul(s.substr(p + 1, 4).c_str(), nullptr, 16);
        out += narrowText(&w, 1, CP_UTF8);
        p += 4;
        break;
      }
      default: out += c;
      }
    }
    return false;
  }
  bool value(Json &out) {
    space();
    if (p >= s.size() || ++depth > 64)
      return false;
    bool ok = true;
    if (s[p] == '"') {
      out.type = Json::string;
      ok = string(out.text);
    } else if (s[p] == '{' || s[p] == '[') {
      const bool object = s[p] == '{';
      out.type = object ? Json::object : Json::array;
      const char close = object ? '}' : ']';
      ++p;
      space();
      if (p < s.size() && s[p] == close)
        ++p;
      else
        for (;;) {
          std::pair<std::string, Json> item;
          space();
          if (object && (!string(item.first) || (space(), p >= s.size() || s[p++] != ':'))) {
            ok = false;
            break;
          }
          if (!value(item.second) || out.members.size() > 100000) {
            ok = false;
            break;
          }
          out.members.push_back(std::move(item));
          space();
          if (p < s.size() && s[p] == ',') {
            ++p;
            continue;
          }
          ok = p < s.size() && s[p++] == close;
          break;
        }
    } else {
      out.type = Json::other;
      while (p < s.size() && (std::isalnum((unsigned char)s[p]) || strchr("+-.", s[p])))
        ++p;
    }
    --depth;
    return ok;
  }
};
// A schema text: a string, or the English one of a set of translations.
static std::string jsonText(const Json &v) {
  if (v.type == Json::string)
    return trimConfigLine(v.text);
  if (v.type != Json::object)
    return {};
  for (const auto &[name, m] : v.members)
    if (lower(name) == "en" && m.type == Json::string)
      return trimConfigLine(m.text);
  for (const auto &[name, m] : v.members)
    if (m.type == Json::string)
      return trimConfigLine(m.text);
  return {};
}
static void schemaEntries(const Json &v, std::vector<std::string> &path,
                          std::vector<BinaryInfo::SchemaEntry> &out) {
  if (v.type != Json::object && v.type != Json::array)
    return;
  for (const auto &[name, m] : v.members) {
    if (m.type == Json::object && !name.empty()) {
      std::string label, help;
      for (const auto &[field, f] : m.members) {
        const auto k = lower(field);
        if (k == "label" || k == "name" || k == "title")
          label = label.empty() ? jsonText(f) : label;
        else if (k == "help" || k == "description" || k == "desc" || k == "tooltip")
          help = help.empty() ? jsonText(f) : help;
      }
      if (!help.empty() || !label.empty())
        out.push_back({lower(name), path,
                       label.empty() || help.empty() ? label + help : label + "\n" + help});
    }
    path.push_back(lower(name));
    schemaEntries(m, path, out);
    path.pop_back();
    if (out.size() > 20000)
      return;
  }
}

// --- the words around the input code ------------------------------------------
static int kindFromWords(const std::vector<const std::string *> &words) {
  static const std::regex menu(
      R"(\b(?:opened|closed)\b|\bopen\s*/\s*close\b|\b(?:open|close|show|hide|toggle)s?\b[^.]{0,40}?\b(?:menu|window|overlay|editor|wiki|panel|ui|gui|journal)\b)",
      std::regex::icase | std::regex::optimize);
  static const std::regex toggle(R"(\b(?:enabled|disabled|activated|deactivated)\b)",
                                 std::regex::icase | std::regex::optimize);
  static const std::regex onOff(R"(\b(?:ON|OFF)\b)", std::regex::optimize);
  int menus = 0, toggles = 0;
  for (const auto *s : words) {
    // Assertions and code, as compiled into ImGui and the like.
    if (s->size() > 200 || s->find("&&") != s->npos || s->find("==") != s->npos ||
        s->find("()") != s->npos || s->find("->") != s->npos)
      continue;
    menus += std::regex_search(*s, menu);
    toggles += std::regex_search(*s, toggle) || std::regex_search(*s, onOff);
  }
  return menus && !toggles ? 1 : toggles && !menus ? 2 : 0;
}

static int inputKind(const Image &img, const Strings &strs) {
  const auto slots = inputSlots(img);
  const auto funcs = functions(img);
  if (slots.empty() || funcs.empty())
    return 0;
  const auto &d = img.d;
  // RIP-relative operands: the ModRM byte's mod 00 r/m 101, then 32 bits.
  std::vector<std::pair<uint32_t, uint32_t>> stringRefs; // site, string
  std::vector<uint32_t> inputSites;
  for (const auto &s : img.sections) {
    if (!(s.flags & executable))
      continue;
    for (size_t o = size_t(s.raw) + 2; o + 4 <= size_t(s.raw) + s.rawSize; ++o) {
      if (((unsigned char)d[o - 1] & 0xC7) != 0x05)
        continue;
      int32_t disp;
      memcpy(&disp, d.data() + o, 4);
      const uint32_t site = s.va + uint32_t(o - s.raw);
      const uint32_t target = uint32_t(int64_t(site) + 4 + disp);
      if (const auto it = strs.at.find(target); it != strs.at.end())
        stringRefs.emplace_back(site, it->second);
      else if (slots.count(target) &&
               ((unsigned char)d[o - 2] == 0xFF || (unsigned char)d[o - 2] == 0x8B))
        inputSites.push_back(site);
    }
  }
  std::sort(stringRefs.begin(), stringRefs.end());
  const auto functionAt = [&](uint32_t rva) -> const std::pair<uint32_t, uint32_t> * {
    auto it = std::upper_bound(funcs.begin(), funcs.end(), std::make_pair(rva, UINT32_MAX));
    if (it == funcs.begin() || rva >= (--it)->second)
      return nullptr;
    return &*it;
  };
  const auto inCode = [&](uint32_t rva) {
    for (const auto &s : img.sections)
      if ((s.flags & executable) && rva >= s.va && rva - s.va < s.rawSize)
        return true;
    return false;
  };
  std::set<std::pair<uint32_t, uint32_t>> readers, nearby;
  for (uint32_t site : inputSites)
    if (const auto *f = functionAt(site))
      readers.insert(*f);
  if (readers.size() > 64)
    return 0; // reads input everywhere: an overlay, not a shortcut
  for (const auto &f : readers) {
    nearby.insert(f);
    size_t o = 0;
    if (!img.offset(f.first, o))
      continue;
    // What it calls directly.
    for (uint32_t r = 0; r + 5 <= f.second - f.first && o + r + 5 <= d.size() && nearby.size() < 2048; ++r) {
      if ((unsigned char)d[o + r] != 0xE8)
        continue;
      int32_t rel;
      memcpy(&rel, d.data() + o + r + 1, 4);
      const uint32_t target = uint32_t(int64_t(f.first) + r + 5 + rel);
      const auto it = std::lower_bound(funcs.begin(), funcs.end(), std::make_pair(target, 0u));
      if (it != funcs.end() && it->first == target)
        nearby.insert(*it);
      else if (!functionAt(target) && inCode(target))
        // A leaf function has no .pdata entry: it runs to the next one.
        nearby.insert({target, it == funcs.end() ? target + 0x400 : std::min(it->first, target + 0x400)});
    }
  }
  std::vector<const std::string *> words;
  for (const auto &f : nearby)
    for (auto it = std::lower_bound(stringRefs.begin(), stringRefs.end(), std::make_pair(f.first, 0u));
         it != stringRefs.end() && it->first < f.second; ++it)
      if (readsAsWords(strs.text[it->second], 8))
        words.push_back(&strs.text[it->second]);
  return kindFromWords(words);
}

static std::shared_ptr<BinaryInfo> analyse(const std::string &data) {
  auto info = std::make_shared<BinaryInfo>();
  Image img(data);
  if (!img.parse())
    return info;
  info->product = versionString(img, "ProductName");
  info->about = versionString(img, "FileDescription");
  if (lower(info->about) == lower(info->product))
    info->about.clear();
  const auto strs = strings(img);
  static const std::regex single(R"(^[;#]\s*([A-Za-z_][A-Za-z0-9_.]*)\s*=.*\S)", std::regex::optimize);
  for (const auto &s : strs.text) {
    if (s.size() > 8 && s.find('=') != s.npos) {
      if (s.find('\n') != s.npos && (s.find(';') != s.npos || s.find('#') != s.npos)) {
        for (auto &[key, note] : bindingCommentsFromText(s, true, false, true))
          info->templates.emplace(key, std::move(note));
      } else if ((s[0] == ';' || s[0] == '#') && s.size() < 1000) {
        std::smatch m;
        if (std::regex_search(s, m, single)) {
          const auto text = trimConfigLine(s.substr(1));
          info->templates.emplace(std::make_pair(std::string(), lower(m[1].str())),
                                  BindingComment{text, text});
        }
      }
    }
    if (s[0] == '{' && s.size() > 40 &&
        (s.find("\"help\"") != s.npos || s.find("\"label\"") != s.npos ||
         s.find("\"description\"") != s.npos)) {
      Json root;
      JsonParser parser{s};
      if (parser.value(root) || !root.members.empty()) {
        std::vector<std::string> path;
        schemaEntries(root, path, info->schema);
      }
    }
    if (readsAsWords(s, 10) && info->texts.size() < 60000)
      info->texts.push_back(s.size() > 4000 ? s.substr(0, 4000) : s);
  }
  info->kindHint = inputKind(img, strs);
  return info;
}

} // namespace binary

// The analysis of the file at path, from the cache while the file is unchanged.
static std::shared_ptr<const BinaryInfo> binaryInfo(const std::filesystem::path &path) {
  static std::mutex mutex;
  static std::map<std::wstring, std::tuple<uintmax_t, std::filesystem::file_time_type,
                                           std::shared_ptr<const BinaryInfo>>> cache;
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec || size < 0x200 || size > 64u * 1024 * 1024)
    return nullptr;
  const auto time = std::filesystem::last_write_time(path, ec);
  if (ec)
    return nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex);
    const auto it = cache.find(path.native());
    if (it != cache.end() && std::get<0>(it->second) == size && std::get<1>(it->second) == time)
      return std::get<2>(it->second);
  }
  std::ifstream file(path, std::ios::binary);
  std::string data(size_t(size), '\0');
  if (!file.read(data.data(), data.size()))
    return nullptr;
  std::shared_ptr<const BinaryInfo> info;
  try {
    info = binary::analyse(data);
  } catch (const std::exception &e) {
    LOG_ERR("Could not read %s: %s", path.filename().string().c_str(), e.what());
    info = std::make_shared<BinaryInfo>();
  }
  std::lock_guard<std::mutex> lock(mutex);
  cache[path.native()] = {size, time, info};
  return info;
}

// A sentence in the mod that names the setting, for a key name long enough not
// to be an ordinary word: "Stance keys: %s goes round ... (StanceCycleKey)".
// The part of it about the key, with printf fields as "...".
static std::string binarySentence(const BinaryInfo &info, const std::string &key) {
  if (key.size() < 5)
    return {};
  const auto wordChar = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };
  std::string best;
  int bestRank = -1;
  for (const auto &s : info.texts) {
    size_t at = s.find(key);
    while (at != s.npos && ((at && wordChar(s[at - 1])) ||
                            (at + key.size() < s.size() && wordChar(s[at + key.size()]))))
      at = s.find(key, at + 1);
    if (at == s.npos)
      continue;
    // Config dumps and complaints: "Key=%d", "Invalid ActivationKeyVK.".
    static const std::regex complaint(
        R"(\b(?:invalid|failed|error|warning|unknown|missing|could not|cannot|can't|not found|unsupported)\b)",
        std::regex::icase | std::regex::optimize);
    const auto after = s.find_first_not_of(' ', at + key.size());
    if ((after != s.npos && s[after] == '=') || std::count(s.begin(), s.end(), '=') > 2 ||
        std::regex_search(s, complaint))
      continue;
    // The segment holding the key.
    size_t begin = 0, end = s.size();
    for (const char *sep : {". ", "\n", "; ", " | "}) {
      const auto n = strlen(sep);
      const auto b = s.rfind(sep, at);
      if (b != s.npos && b + n > begin && b + n <= at)
        begin = b + (sep[0] == '.' ? 2 : n);
      const auto e = s.find(sep, at + key.size());
      if (e != s.npos && e < end)
        end = e + (sep[0] == '.' ? 1 : 0);
    }
    // Still long: a list, "... (StanceCycleKey), ... pick one (StanceKliffKey,
    // StanceDamianeKey)". Its item, cut at commas outside brackets.
    if (end - begin > 160) {
      int depth = 0;
      size_t itemBegin = begin, itemEnd = end;
      for (size_t i = begin; i < end; ++i) {
        const char c = s[i];
        depth += c == '(' || c == '[' ? 1 : c == ')' || c == ']' ? -1 : 0;
        if (c == ',' && depth <= 0 && i + 1 < end && s[i + 1] == ' ') {
          if (i < at)
            itemBegin = i + 2;
          else {
            itemEnd = i;
            break;
          }
        }
      }
      begin = itemBegin, end = itemEnd;
    }
    auto part = trimConfigLine(s.substr(begin, end - begin));
    // A leading "TAG: " is the mod's log prefix.
    if (const auto colon = part.find(": "); colon != part.npos && colon < 12 && colon < at - begin &&
                                             std::all_of(part.begin(), part.begin() + colon, [](char c) {
                                               return std::isupper((unsigned char)c) || c == '_';
                                             }))
      part = trimConfigLine(part.substr(colon + 2));
    static const std::regex field(R"(%[-+ #0]*[0-9*]*(?:\.[0-9*]+)?(?:hh|h|ll|l|z|I64)?[diouxXfFeEgGcsSpn])",
                                  std::regex::optimize);
    part = std::regex_replace(part, field, "...");
    if (part.size() < 8)
      continue;
    const int rank = (s.find("(" + key) != s.npos ? 2 : 0) + (part.size() <= 300);
    if (rank > bestRank || (rank == bestRank && part.size() < best.size()))
      best = part, bestRank = rank;
  }
  return best.size() > 300 ? best.substr(0, 297) + "..." : best;
}

static std::string binarySchemaText(const BinaryInfo &info, const std::string &section,
                                    const std::string &key) {
  const auto k = lower(key), sec = lower(section);
  const BinaryInfo::SchemaEntry *any = nullptr;
  for (const auto &e : info.schema) {
    if (e.key != k)
      continue;
    if (std::find(e.path.begin(), e.path.end(), sec) != e.path.end())
      return e.text;
    any = any ? any : &e;
  }
  return any ? any->text : std::string();
}
