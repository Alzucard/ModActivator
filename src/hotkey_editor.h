#pragma once
#include "ui_helpers.h"
#include <fstream>
#include <sstream>
static std::string trim(std::string s) {
  auto a = s.find_first_not_of(" \t\r\n");
  if (a == s.npos)
    return {};
  return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
static std::string readBytes(const std::string &p) {
  std::ifstream f(p, std::ios::binary);
  if (!f)
    throw std::runtime_error("Cannot read config.");
  return std::string(std::istreambuf_iterator<char>(f), {});
}
static std::string encodeBinding(const Action &a, std::string input) {
  input = trim(input);
  if (input.empty() || input.find_first_of("\r\n|") != input.npos ||
      bindingComment(input) != input.npos)
    throw std::runtime_error(
        "Enter one key or combination, such as F6 or Ctrl+F6.");
  if (lower(a.file) == "reshade.ini")
    throw std::runtime_error("Change ReShade shortcuts in its own settings.");
  std::string original = read(a);
  const auto comment = bindingComment(original);
  const auto setting = trim(original.substr(0, comment));
  const auto bar = setting.find('|');
  const auto base = trim(setting.substr(0, bar));
  const bool numeric = !base.empty() && isdigit((unsigned char)base[0]) &&
                       base.find(',') == base.npos && !standaloneDigit(a, base);
  input = compactShortcut(std::move(input));
  auto separator = input.find_last_of('+');
  if (input == "+")
    separator = input.npos;
  else if (input.size() >= 2 && input.compare(input.size() - 2, 2, "++") == 0)
    --separator;
  const size_t start = separator == input.npos ? 0 : separator + 1;
  const auto token = trim(input.substr(start));
  const auto symbol = keySymbol(token);
  const bool numericLiteral = symbol >= L'0' && symbol <= L'9';
  if (symbol && (!numeric || (!numericLiteral && separator == input.npos))) {
    const auto layout = gameKeyboardLayout();
    auto scan = scanForSymbol(symbol, layout);
    if (!scan)
      scan = scanForLocalizedSymbol(token, layout);
    const auto canonical = usSymbolForScan(scan);
    if (canonical) {
      if (numeric) {
        input = std::to_string(vkForScan(scan, layout));
      } else {
        input.replace(start, input.size() - start,
                      scan == 0x29 ? "Grave" : std::string(1, canonical));
      }
    }
  }
  std::string converted = input;
  if (!numeric && !input.empty() && isdigit((unsigned char)input[0]) &&
      !standaloneDigit(a, input))
    throw std::runtime_error(
        "Use a key name such as F8 for a named-key setting.");
  if (numeric) {
    if (parseShortcut(input).size() != 1)
      throw std::runtime_error(
          "This setting stores a plain key code. Enter a single key; existing "
          "modifier flags are preserved.");
    int k = vk(input);
    if (k < 8 || k > 254 || k == 16 || k == 17 || k == 18)
      throw std::runtime_error("Unsupported key.");
    if (base.size() > 2 && base[0] == '0' &&
        (base[1] == 'x' || base[1] == 'X')) {
      char b[16];
      snprintf(b, sizeof b, "0x%X", k);
      converted = b;
    } else {
      converted = std::to_string(k);
      if (standaloneDigit(a, converted))
        converted.insert(converted.begin(), '0');
    }
  }
  if (bar != setting.npos)
    converted += setting.substr(bar);
  auto ks = keysFrom(a, converted);
  if (!validShortcut(ks))
    throw std::runtime_error("Unsupported shortcut.");
  if (!numeric && configKeyName(ks.back()).empty())
    throw std::runtime_error("Unsupported key name.");
  return converted;
}
static std::string replaceValue(const std::string &bytes, const Action &a,
                                const std::string &value) {
  if (bytes.size() > 1024 * 1024 || bytes.find('\0') != bytes.npos)
    throw std::runtime_error("Config format unsupported by editor.");
  std::string result = bytes, section;
  size_t pos = 0;
  int matches = 0;
  size_t begin = 0, finish = 0;
  while (pos < bytes.size()) {
    auto end = bytes.find('\n', pos);
    if (end == bytes.npos)
      end = bytes.size();
    std::string line = bytes.substr(pos, end - pos);
    if (pos == 0 && line.substr(0, 3) == "\xef\xbb\xbf")
      line.erase(0, 3);
    auto text = trim(line);
    if (!text.empty() && text[0] == '[') {
      auto close = text.find(']');
      if (close != text.npos)
        section = text.substr(1, close - 1);
    } else if (lower(section) == lower(a.section) && !text.empty() &&
               text[0] != ';' && text[0] != '#') {
      auto eq = line.find('=');
      if (eq != line.npos && lower(trim(line.substr(0, eq))) == lower(a.key)) {
        ++matches;
        size_t start = eq + 1;
        while (start < line.size() &&
               (line[start] == ' ' || line[start] == '\t'))
          ++start;
        size_t stop = bindingComment(line, start);
        if (stop == line.npos)
          stop = line.size();
        while (stop > start &&
               (line[stop - 1] == ' ' || line[stop - 1] == '\t' ||
                line[stop - 1] == '\r'))
          --stop;
        begin = pos + start;
        finish = pos + stop;
      }
    }
    pos = end + 1;
  }
  if (matches != 1)
    throw std::runtime_error(
        "Missing or duplicate setting; edit the config manually.");
  result.replace(begin, finish - begin, value);
  return result;
}
static bool saveBinding(const Action &a, const std::string &expected,
                        const std::string &value) {
  auto path =
      std::filesystem::weakly_canonical(std::filesystem::path(folder) / a.file);
  auto root = std::filesystem::weakly_canonical(folder);
  auto relative = path.lexically_relative(root);
  if (relative.empty() || *relative.begin() == ".." ||
      !std::filesystem::is_regular_file(path))
    throw std::runtime_error("Config path is outside mod folder.");
  auto current = readBytes(path.string());
  if (current != expected)
    throw std::runtime_error(
        "Config changed since editing began. Select it again.");
  auto updated = replaceValue(current, a, value);
  if (updated == current)
    return false;
  auto backup = path.string() + ".modactivator.bak";
  if (!std::filesystem::exists(backup) &&
      !CopyFileA(path.string().c_str(), backup.c_str(), TRUE))
    throw std::runtime_error("Could not create backup.");
  auto temporary = path.string() + ".modactivator.tmp";
  HANDLE f = CreateFileA(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                         CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE)
    throw std::runtime_error("Cannot create temporary config; check "
                             "permissions or stale temp file.");
  DWORD written = 0;
  bool ok =
      WriteFile(f, updated.data(), (DWORD)updated.size(), &written, nullptr) &&
      written == updated.size() && FlushFileBuffers(f);
  CloseHandle(f);
  if (!ok) {
    DeleteFileA(temporary.c_str());
    throw std::runtime_error("Config write failed.");
  }
  try {
    if (readBytes(path.string()) != expected)
      throw std::runtime_error("Config changed while saving.");
  } catch (...) {
    DeleteFileA(temporary.c_str());
    throw;
  }
  if (!ReplaceFileA(path.string().c_str(), temporary.c_str(), nullptr, 0,
                    nullptr, nullptr)) {
    DeleteFileA(temporary.c_str());
    throw std::runtime_error("Could not replace config. Original preserved.");
  }
  WritePrivateProfileStringA(nullptr, nullptr, nullptr, path.string().c_str());
  return true;
}
static void drawHotkeys() {
  static Action editing;
  static bool selected = false;
  static char entry[128] = "";
  static std::string snapshot, message;
  static bool acknowledge = false;
  static std::string lastInput, encodedCache, validationError;
  static HKL lastLayout = nullptr;
  static std::vector<int> proposedCache;
  std::vector<Action> list;
  {
    std::lock_guard<std::mutex> lock(catalogMutex);
    list = allBindings;
  }
  int exact = 0, overlap = 0;
  for (auto &a : list) {
    if (a.severity == 2)
      ++exact;
    else if (a.severity == 1)
      ++overlap;
  }
  ImGui::Spacing();
  UI::badge((std::to_string(list.size()) + " bindings").c_str(), UI::muted);
  ImGui::SameLine();
  UI::badge((std::to_string(exact) + " conflicts").c_str(),
            exact ? ImVec4(.95f, .43f, .40f, 1) : UI::muted);
  ImGui::SameLine();
  UI::badge((std::to_string(overlap) + " possible overlaps").c_str(),
            overlap ? UI::gold : UI::muted);
  ImGui::Spacing();
  ImGui::TextDisabled(
      "Select a binding to edit. Changes apply after restarting the game.");
  if (!message.empty())
    ImGui::TextWrapped("%s", message.c_str());
  float available = ImGui::GetContentRegionAvail().y;
  float listHeight =
      (std::max)(70.0f, available - (selected ? 300.0f * UI::scale : 0.0f));
  if (ImGui::BeginTable("Bindings", 3,
                        ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_Resizable,
                        ImVec2(0, listHeight))) {
    ImGui::TableSetupColumn("MOD / ACTION", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("SHORTCUT", ImGuiTableColumnFlags_WidthFixed,
                            160 * UI::scale);
    ImGui::TableSetupColumn("STATUS", ImGuiTableColumnFlags_WidthFixed,
                            118 * UI::scale);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    for (size_t i = 0; i < list.size(); ++i) {
      auto &a = list[i];
      if (!UI::matches(a))
        continue;
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::PushID((int)i);
      const std::string rowLabel = UI::name(a) + " / " + UI::actionName(a);
      if (ImGui::Selectable(rowLabel.c_str(),
                            selected && identity(a) == identity(editing),
                            ImGuiSelectableFlags_SpanAllColumns)) {
        try {
          auto selectedSnapshot = readBytes(folder + a.file);
          editing = a;
          snapshot = std::move(selectedSnapshot);
          selected = true;
          entry[0] = 0;
          lastInput = "\x01";
          acknowledge = false;
          message.clear();
        } catch (const std::exception &e) {
          message = e.what();
        }
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s\n[%s] %s", a.file.c_str(), a.section.c_str(),
                          a.key.c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(UI::shortcut(a).c_str());
      ImGui::TableSetColumnIndex(2);
      UI::badge(a.severity == 2   ? "Conflict"
                : a.severity == 1 ? "Overlap"
                                  : "Clear",
                a.severity == 2   ? ImVec4(.95f, .43f, .40f, 1)
                : a.severity == 1 ? UI::gold
                                  : ImVec4(.47f, .70f, .58f, 1));
      if (a.severity && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", a.overlapNames.c_str());
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (selected) {
    ImGui::BeginChild("ShortcutEditor");
    const auto layout = gameKeyboardLayout();
    if (layout != lastLayout) {
      lastLayout = layout;
      lastInput = "\x01";
      acknowledge = false;
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(UI::gold, "EDIT SHORTCUT");
    ImGui::SameLine();
    ImGui::TextUnformatted(UI::name(editing).c_str());
    ImGui::TextDisabled("Current: %s", UI::shortcut(editing).c_str());
    ImGui::SetNextItemWidth(260 * UI::scale);
    if (ImGui::InputTextWithHint("New shortcut", "Ctrl+F6", entry, sizeof entry))
      acknowledge = false;
    try {
      if (lastInput != entry) {
        lastInput = entry;
        validationError.clear();
        try {
          encodedCache = encodeBinding(editing, entry);
          proposedCache = keysFrom(editing, encodedCache);
        } catch (const std::exception &e) {
          validationError = e.what();
        }
      }
      if (!validationError.empty())
        throw std::runtime_error(validationError);
      const auto &encoded = encodedCache;
      const auto &proposed = proposedCache;
      ImGui::TextDisabled("New binding: %s", shortcutLabel(proposed).c_str());
      const auto ownKeys = openerKeys();
      bool clashes = conflict(proposed, ownKeys) != 0;
      for (auto &a : list)
        if (identity(a) != identity(editing) &&
            conflict(proposed, a.cachedKeys)) {
          clashes = true;
          ImGui::TextColored(ImVec4(1, .6f, .2f, 1), "Overlaps: %s",
                             a.label.c_str());
        }
      if (conflict(proposed, ownKeys))
        ImGui::TextWrapped("Overlaps ModActivator (%s).",
                           shortcutLabel(ownKeys).c_str());
      if (clashes)
        ImGui::Checkbox("Save despite overlap", &acknowledge);
      ImGui::BeginDisabled(clashes && !acknowledge);
      if (ImGui::Button("Save hotkey")) {
        try {
          bool changed;
          {
            std::lock_guard<std::mutex> lock(catalogMutex);
            changed = saveBinding(editing, snapshot, encoded);
            if (changed)
              restartRequired.insert(lower(editing.module));
          }
          message = changed
                        ? "Saved with .modactivator.bak backup. Restart the game to apply."
                        : "Shortcut is already set to that value.";
          selected = false;
          if (changed)
            refreshRequested.store(true);
        } catch (const std::exception &e) {
          message = e.what();
        }
      }
      ImGui::EndDisabled();
    } catch (const std::exception &e) {
      if (entry[0])
        ImGui::TextWrapped("%s", e.what());
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
      selected = false;
    ImGui::EndChild();
  }
}
