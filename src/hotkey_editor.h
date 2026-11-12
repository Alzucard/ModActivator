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
static std::string encodeBinding(const Action &a, std::string input,
                                 std::vector<int> recorded = {}) {
  input = trim(input);
  if (recorded.empty() &&
      (input.empty() || input.find_first_of("\r\n|") != input.npos ||
       bindingComment(input) != input.npos))
    throw std::runtime_error(
        "Enter one key or combination, such as F6 or Ctrl+F6.");
  if (lower(a.file) == "reshade.ini")
    throw std::runtime_error("Change ReShade shortcuts in its own settings.");
  std::string original = read(a);
  const auto comment = bindingComment(original);
  const auto setting = trim(original.substr(0, comment));
  const auto bar = setting.find('|');
  const auto base = trim(setting.substr(0, bar));
  const bool numeric = (a.hexNumbers && !base.empty() &&
                        base.find_first_not_of("0123456789abcdefABCDEF") == base.npos) ||
                       (!base.empty() && isdigit((unsigned char)base[0]) &&
                        base.find(',') == base.npos && !standaloneDigit(a, base));
  if (!recorded.empty()) {
    if (!validShortcut(recorded))
      throw std::runtime_error("Unsupported shortcut.");
    // Preserve modifier flags stored separately from the main key.
    for (int modifier : keysFrom(a, "")) {
      auto end = recorded.end() - 1;
      recorded.erase(std::remove(recorded.begin(), end, modifier), end);
    }
    if (numeric && recorded.size() != 1)
      throw std::runtime_error("This mod stores a single key code with separate modifier settings.");
    if (numeric) {
      input = std::to_string(recorded.back());
    } else {
      for (int key : recorded) {
        const auto name = configKeyName(key);
        if (name.empty())
          throw std::runtime_error("This mod's shortcut format cannot represent this key.");
        if (!input.empty())
          input += '+';
        input += name;
      }
    }
  } else {
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
    if (a.hexNumbers) {
      char b[16];
      snprintf(b, sizeof b, "%02X", k);
      converted = b;
    } else if (base.size() > 2 && base[0] == '0' &&
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
  if (!recorded.empty() && ks.back() != recorded.back())
    throw std::runtime_error("This mod's shortcut format cannot represent the recorded key.");
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
  // The name is ours alone, and every save happens under catalogMutex, so a
  // file already there is left over from a save that died part way (the game
  // closing mid-write). Remove it rather than refuse every later save.
  if (GetFileAttributesA(temporary.c_str()) != INVALID_FILE_ATTRIBUTES &&
      !DeleteFileA(temporary.c_str()))
    throw std::runtime_error("A leftover " + std::filesystem::path(temporary).filename().string() +
                             " could not be removed; delete it and save again.");
  HANDLE f = CreateFileA(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                         CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE)
    throw std::runtime_error("Cannot create temporary config; check permissions.");
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
// The menu's copy of allBindings, taken once per change rather than every
// frame. Render thread only. The copies keep their display strings cached
// between frames, and new text is scanned for glyphs the atlas lacks.
static unsigned bindingsSnapshotVersion = 0;
// Which mods a Send button can reach, worked out with the snapshot: loaded,
// not waiting for a restart after an edit, and not managed by ModHub (which
// switches a managed mod's own menu key off). Lower-case module names.
struct SendTargets {
  std::set<std::string> ready, managed;
};
static SendTargets sendTargets;
static bool anyPadBinding = false;
static const std::vector<Action> &bindingsSnapshot() {
  static std::vector<Action> list;
  if (const unsigned current = catalogVersion.load(); current != bindingsSnapshotVersion) {
    std::set<std::string> restarts;
    {
      std::lock_guard<std::mutex> lock(catalogMutex);
      list = allBindings;
      restarts = restartRequired;
    }
    bindingsSnapshotVersion = current;
    sendTargets = {};
    anyPadBinding = false;
    const HMODULE modHub = GetModuleHandleA("ModHub.asi");
    for (const auto &a : list) {
      UI::noteGlyphs(a);
      anyPadBinding |= a.padButtons != 0;
      const auto module = lower(a.module);
      const HMODULE handle = GetModuleHandleA(a.module.c_str());
      if ((handle || previewMode) && !restarts.count(module))
        sendTargets.ready.insert(module);
      if (handle && modHub && module != "modhub.asi" && GetProcAddress(handle, "ModHubMenuV1"))
        sendTargets.managed.insert(module);
    }
  }
  return list;
}
// The binding that makes a mod read its config again, from the known-mods
// list's @reload (Key, or Section/Key, in the same config file). Null when
// the mod has none, or "live" (it needs no key).
static const Action *reloadBinding(const Action &edited, const std::vector<Action> &list) {
  const auto spec = lower(trimConfigLine(edited.reload));
  if (spec.empty() || spec == "live")
    return nullptr;
  const auto slash = spec.find('/');
  const auto section = slash == spec.npos ? std::string() : spec.substr(0, slash);
  const auto key = slash == spec.npos ? spec : spec.substr(slash + 1);
  for (const auto &a : list)
    if (lower(a.file) == lower(edited.file) && lower(a.key) == key &&
        (section.empty() || lower(a.section) == section))
      return &a;
  return nullptr;
}
static void drawHotkeys() {
  static Action editing;
  static bool selected = false;
  static std::string snapshot, message, encoded, validationError;
  static bool acknowledge = false;
  static HKL lastLayout = nullptr;
  static std::vector<int> recorded, proposed;
  // Your own name and note for the selected binding, as typed.
  static char userName[96]{}, userNote[1024]{};
  const auto &list = bindingsSnapshot();
  // After a refresh (saving your note causes one), the selected binding is
  // taken again from the new list, so its title and description are current.
  // What you typed and the config snapshot stay.
  static unsigned editingVersion = 0;
  if (selected && editingVersion != bindingsSnapshotVersion) {
    editingVersion = bindingsSnapshotVersion;
    const auto id = cachedIdentity(editing); // a copy: editing is replaced below
    for (const auto &a : list)
      if (cachedIdentity(a) == id) {
        editing = a;
        break;
      }
  }
  int exact = 0, overlap = 0;
  for (const auto &a : list) {
    if (a.severity == 2)
      ++exact;
    else if (a.severity == 1)
      ++overlap;
  }
  ImGui::Spacing();
  const auto count = [](size_t n, const char *one, const char *many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
  };
  UI::badge(count(list.size(), "binding", "bindings").c_str(), UI::muted);
  ImGui::SameLine();
  UI::badge(count(exact, "conflict", "conflicts").c_str(),
            exact ? ImVec4(.95f, .43f, .40f, 1) : UI::muted);
  ImGui::SameLine();
  UI::badge(count(overlap, "possible overlap", "possible overlaps").c_str(),
            overlap ? UI::gold : UI::muted);
  ImGui::Spacing();
  ImGui::TextDisabled(
      "Expand a mod and select a binding. Most mods take a changed shortcut after a restart.");
  if (!message.empty())
    ImGui::TextWrapped("%s", message.c_str());
  float available = ImGui::GetContentRegionAvail().y;
  float listHeight =
      (std::max)(70.0f, available - (selected ? 420.0f * UI::scale : 0.0f));
  // A Controller column only when some mod has a controller binding.
  const bool padColumn = anyPadBinding;
  const int padAt = 2, describeAt = padColumn ? 3 : 2, statusAt = describeAt + 1,
            sendAt = statusAt + 1;
  if (ImGui::BeginTable("Bindings", sendAt + 1,
                        ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_Resizable,
                        ImVec2(0, listHeight))) {
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 1.25f);
    ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthStretch, .8f);
    if (padColumn)
      ImGui::TableSetupColumn("Controller", ImGuiTableColumnFlags_WidthStretch, .8f);
    ImGui::TableSetupColumn("What it does", ImGuiTableColumnFlags_WidthStretch, 1.8f);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 90 * UI::scale);
    ImGui::TableSetupColumn("##Send", ImGuiTableColumnFlags_WidthFixed, 64 * UI::scale);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    UI::groupedBindings(list, [&](const Action &a) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::PushID(cachedIdentity(a).c_str());
      const auto rowLabel = UI::actionName(a);
      if (ImGui::Selectable(rowLabel.c_str(),
                            selected && cachedIdentity(a) == cachedIdentity(editing),
                            ImGuiSelectableFlags_SpanAllColumns)) {
        try {
          auto selectedSnapshot = readBytes(folder + a.file);
          editing = a;
          editingVersion = bindingsSnapshotVersion;
          snapshot = std::move(selectedSnapshot);
          selected = true;
          recorded.clear();
          proposed.clear();
          validationError.clear();
          UI::recording = 0;
          acknowledge = false;
          message.clear();
          const auto mine = userInfo(a);
          snprintf(userName, sizeof userName, "%s", mine.name.c_str());
          snprintf(userNote, sizeof userNote, "%s", mine.description.c_str());
        } catch (const std::exception &e) {
          message = e.what();
        }
      }
      if (ImGui::IsItemHovered())
        UI::bindingTooltip(a);
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(UI::shortcut(a).c_str());
      if (padColumn) {
        ImGui::TableSetColumnIndex(padAt);
        if (a.padButtons)
          ImGui::TextUnformatted(padLabel(a.padButtons).c_str());
        else
          ImGui::TextDisabled("-");
      }
      ImGui::TableSetColumnIndex(describeAt);
      UI::descriptionCell(a);
      ImGui::TableSetColumnIndex(statusAt);
      UI::badge(a.severity == 2   ? "Conflict"
                : a.severity == 1 ? "Overlap"
                                  : "Clear",
                a.severity == 2   ? ImVec4(.95f, .43f, .40f, 1)
                : a.severity == 1 ? UI::gold
                                  : ImVec4(.47f, .70f, .58f, 1));
      if (a.severity && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", displayText(a.overlapNames).c_str());
      // Press this shortcut now. A menu closes the panel so its window gets the
      // input; anything else leaves the panel open.
      ImGui::TableSetColumnIndex(sendAt);
      const auto module = lower(a.module);
      const bool managedMenu = sendTargets.managed.count(module) && !a.toggle && a.menuRank;
      ImGui::BeginDisabled(!sendTargets.ready.count(module) || managedMenu);
      if (ImGui::SmallButton("Send"))
        queueAction(a, a.menuRank > 0 && !a.toggle);
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", managedMenu ? "Open it through ModHub."
                                : !sendTargets.ready.count(module)
                                    ? "The mod is not loaded, or waits for a restart after an edit."
                                    : "Press this shortcut now.");
      ImGui::PopID();
    }, statusAt);
    ImGui::EndTable();
  }
  if (selected) {
    ImGui::BeginChild("ShortcutEditor", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
    const auto layout = gameKeyboardLayout();
    if (layout != lastLayout) {
      lastLayout = layout;
      if (!recorded.empty())
        message = "Keyboard layout changed. Set the shortcut again.";
      recorded.clear();
      proposed.clear();
      validationError.clear();
      UI::recording = 0;
      acknowledge = false;
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(UI::gold, "EDIT SHORTCUT");
    ImGui::SameLine();
    const auto title = UI::name(editing) + " / " + UI::actionName(editing);
    ImGui::TextUnformatted(title.c_str());
    ImGui::TextDisabled("Current: %s", UI::shortcut(editing).c_str());
    if (!editing.description.empty())
      UI::descriptionCell(editing);
    const float width = (std::min)(260 * UI::scale,
                                  ImGui::GetContentRegionAvail().x * .55f);

    // Your own words for this binding, kept in ModActivator.User.ini. They win over
    // every other source and take effect at once, without a restart.
    ImGui::Spacing();
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputTextWithHint("Name##UserName", UI::actionName(editing).c_str(), userName,
                                 sizeof userName))
      UI::noteGlyphs(std::string(userName));
    if (ImGui::InputTextMultiline("Note##UserNote", userNote, sizeof userNote,
                                  ImVec2(width, ImGui::GetTextLineHeight() * 3.5f)))
      UI::noteGlyphs(std::string(userNote));
    const auto saveNote = [&](const std::string &name, const std::string &note) {
      // One line per value in the profile file; newlines are kept by the hex.
      if (saveUserText("BindingNames", editing, trim(name)) &&
          saveUserText("BindingNotes", editing, trim(note))) {
        message = name.empty() && note.empty() ? "Your name and note were removed."
                                                : "Saved your name and note.";
        refreshRequested.store(true);
      } else {
        message = "Could not save your name and note.";
      }
    };
    if (ImGui::Button("Save name and note"))
      saveNote(userName, userNote);
    ImGui::SameLine();
    if (ImGui::Button("Clear mine")) {
      userName[0] = userNote[0] = 0;
      saveNote("", "");
    }
    ImGui::Spacing();
    if (auto keys = UI::shortcutField(recorded, width); !keys.empty()) {
      recorded = std::move(keys);
      proposed.clear();
      validationError.clear();
      acknowledge = false;
      try {
        encoded = encodeBinding(editing, "", recorded);
        proposed = keysFrom(editing, encoded);
      } catch (const std::exception &e) {
        validationError = e.what();
      }
    }
    // A key nothing else uses that this mod's format can hold: function keys
    // first, then with Ctrl, Shift, Alt and Ctrl+Shift.
    ImGui::SameLine();
    if (ImGui::Button("Pick a free key")) {
      UI::recording = 0;
      acknowledge = false;
      validationError = "No free key found that this mod's setting can hold. Record one instead.";
      const auto free = [&](const std::vector<int> &keys) {
        if (conflict(keys, openerKeys()) || !reservedClashes(keys).empty())
          return false;
        return std::none_of(list.begin(), list.end(), [&](const Action &a) {
          return cachedIdentity(a) != cachedIdentity(editing) && conflict(keys, a.cachedKeys);
        });
      };
      bool picked = false;
      for (const std::vector<int> &mods : std::vector<std::vector<int>>{
               {}, {VK_CONTROL}, {VK_SHIFT}, {VK_MENU}, {VK_CONTROL, VK_SHIFT}}) {
        for (int key = VK_F1; key <= VK_F12 && !picked; ++key) {
          auto candidate = mods;
          candidate.push_back(key);
          try {
            const auto value = encodeBinding(editing, "", candidate);
            const auto keys = keysFrom(editing, value);
            if (!free(keys))
              continue;
            recorded = candidate;
            encoded = value;
            proposed = keys;
            validationError.clear();
            picked = true;
          } catch (const std::exception &) {
          }
        }
        if (picked)
          break;
      }
      if (!picked)
        proposed.clear();
    }
    if (!validationError.empty())
      ImGui::TextWrapped("%s", validationError.c_str());
    if (!proposed.empty()) {
      if (proposed != recorded)
        ImGui::TextDisabled("Saved binding: %s", shortcutLabel(proposed).c_str());
      const auto ownKeys = openerKeys();
      bool clashes = conflict(proposed, ownKeys) != 0;
      for (auto &a : list)
        if (cachedIdentity(a) != cachedIdentity(editing) &&
            conflict(proposed, a.cachedKeys)) {
          clashes = true;
          ImGui::TextColored(ImVec4(1, .6f, .2f, 1), "Overlaps: %s",
                             displayText(a.label).c_str());
        }
      for (const auto &[severity, who] : reservedClashes(proposed)) {
        clashes = true;
        ImGui::TextColored(ImVec4(1, .6f, .2f, 1), "%s: %s",
                           severity == 2 ? "Already used by" : "Overlaps", displayText(who).c_str());
      }
      if (conflict(proposed, ownKeys))
        ImGui::TextWrapped("Overlaps ModActivator (%s).",
                           shortcutLabel(ownKeys).c_str());
      if (clashes)
        ImGui::Checkbox("Save despite overlap", &acknowledge);
      ImGui::BeginDisabled(UI::recording || (clashes && !acknowledge));
      if (ImGui::Button("Save hotkey")) {
        try {
          // A mod that rereads its config needs no restart: it reads it live,
          // or ModActivator presses its reload key right after the save. Not
          // when the reload key itself changed: the mod still listens on the
          // old one, which the file no longer says.
          const bool live = lower(editing.reload) == "live";
          const Action *reloader = live ? nullptr : reloadBinding(editing, list);
          if (reloader && cachedIdentity(*reloader) == cachedIdentity(editing))
            reloader = nullptr;
          bool changed;
          {
            std::lock_guard<std::mutex> lock(catalogMutex);
            changed = saveBinding(editing, snapshot, encoded);
            if (changed) {
              if (!live && !reloader)
                restartRequired.insert(lower(editing.module));
              catalogChanged();
            }
          }
          if (changed)
            refreshRequested.store(true);
          bool reloaded = false;
          if (changed && reloader) {
            reloaded = queueAction(*reloader, false);
            if (!reloaded) {
              std::lock_guard<std::mutex> lock(catalogMutex);
              restartRequired.insert(lower(editing.module));
              catalogChanged();
            }
          }
          message = !changed  ? "Shortcut is already set to that value."
                    : live     ? "Saved with .modactivator.bak backup. The mod reads its config "
                                 "while the game runs, so the new shortcut works now."
                    : reloaded ? "Saved with .modactivator.bak backup. ModActivator pressed the "
                                 "mod's reload key (" + UI::shortcut(*reloader) +
                                     "), so the new shortcut works now."
                               : "Saved with .modactivator.bak backup. Restart the game to apply.";
          selected = false;
        } catch (const std::exception &e) {
          message = e.what();
        }
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
    }
    if (ImGui::Button("Cancel")) {
      selected = false;
      UI::recording = 0;
    }
    ImGui::EndChild();
  }
}
