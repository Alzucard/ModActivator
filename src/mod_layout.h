#pragma once

struct ModPlacement {
  std::string binding;
  int tab = 0;
};
struct ModLayout {
  std::map<int, std::string> tabs;
  std::vector<ModPlacement> placements;
};
static ModLayout modLayout;
static constexpr int firstCustomTab = 3;

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
static void loadLayout() {
  const auto path = folder + "ModActivator.ini";
  char value[128]{};
  GetPrivateProfileStringA("General", "OpenShortcut", "", value, sizeof value,
                           path.c_str());
  const auto code = shortcutCode(parseShortcut(value));
  opener.store(code && (code & 0xff) != VK_ESCAPE ? code : defaultOpener);
  std::vector<char> section(32768);
  DWORD size = 0;
  while (true) {
    size = GetPrivateProfileSectionA("ModLayout", section.data(),
                                     (DWORD)section.size(), path.c_str());
    if (size < section.size() - 2)
      break;
    section.resize(section.size() * 2);
  }
  ModLayout loaded;
  std::map<unsigned long, ModPlacement> ordered;
  for (const char *p = section.data(); *p; p += strlen(p) + 1) {
    const std::string line = p;
    const auto eq = line.find('=');
    if (eq < 2 || eq == line.npos)
      continue;
    const auto index = strtoul(line.c_str() + 1, nullptr, 10);
    if (line[0] == 'T' && index >= firstCustomTab && index < 65536) {
      auto name = unhexText(line.substr(eq + 1));
      if (!name.empty())
        loaded.tabs[(int)index] = std::move(name);
    } else if (line[0] == 'M') {
      const auto sep = line.find(':', eq + 1);
      if (sep == line.npos)
        continue;
      auto binding = unhexText(line.substr(sep + 1));
      if (!binding.empty())
        ordered[index] = {std::move(binding), atoi(line.c_str() + eq + 1)};
    }
  }
  for (auto &[index, place] : ordered)
    loaded.placements.push_back(std::move(place));
  modLayout = std::move(loaded);
}
static bool saveLayout(ModLayout next) {
  std::string section;
  for (const auto &[id, name] : next.tabs)
    section += "T" + std::to_string(id) + "=" + hexText(name) + '\0';
  size_t index = 0;
  for (const auto &place : next.placements)
    section += "M" + std::to_string(index++) + "=" + std::to_string(place.tab) +
               ":" + hexText(place.binding) + '\0';
  if (section.empty())
    section += '\0';
  if (!WritePrivateProfileSectionA("ModLayout", section.c_str(),
                                   (folder + "ModActivator.ini").c_str()))
    return false;
  modLayout = std::move(next);
  return true;
}
static int assignedTab(const std::string &binding) {
  for (const auto &place : modLayout.placements)
    if (place.binding == binding && modLayout.tabs.count(place.tab))
      return place.tab;
  return 0;
}
static int actionTab(const Action &action) {
  const int assigned = assignedTab(identity(action));
  return assigned ? assigned : action.toggle ? 1 : 0;
}
static void orderActions(std::vector<Action> &actions) {
  if (modLayout.placements.empty())
    return;
  std::map<std::string, size_t> rank;
  for (size_t i = 0; i < modLayout.placements.size(); ++i)
    rank.emplace(modLayout.placements[i].binding, i);
  const auto position = [&](const Action &action) {
    const auto it = rank.find(identity(action));
    return it == rank.end() ? rank.size() : it->second;
  };
  std::stable_sort(actions.begin(), actions.end(), [&](const Action &a, const Action &b) {
    return position(a) < position(b);
  });
}
static ModPlacement &placementFor(ModLayout &layout, const std::string &id) {
  for (auto &place : layout.placements)
    if (place.binding == id)
      return place;
  return layout.placements.emplace_back(ModPlacement{id, 0});
}
static bool saveOpener(const std::vector<int> &keys) {
  const auto code = shortcutCode(keys);
  if (!code || (code & 0xff) == VK_ESCAPE)
    return false;
  std::string value;
  for (int key : keys) {
    if (!value.empty())
      value += '+';
    char hex[8]{};
    snprintf(hex, sizeof hex, "0x%02X", key);
    value += hex;
  }
  if (!WritePrivateProfileStringA("General", "OpenShortcut", value.c_str(),
                                  (folder + "ModActivator.ini").c_str()))
    return false;
  opener.store(code);
  refreshRequested.store(true);
  return true;
}
static void drawSettings(const std::vector<Action> &actions) {
  static char tabName[64]{};
  static int editingTab = 0;
  static std::string status;
  static std::vector<int> proposed;
  static bool acknowledge = false;
  ImGui::BeginChild("SettingsScroll");
  ImGui::Text("ModActivator shortcut: %s", shortcutLabel(openerKeys()).c_str());
  if (auto keys = UI::recordShortcut("Record opener"); !keys.empty()) {
    proposed = std::move(keys);
    acknowledge = false;
    status.clear();
  }
  ImGui::SameLine();
  if (ImGui::Button("Use default")) {
    proposed = {VK_CONTROL, VK_SHIFT, VK_BACK};
    UI::recording = 0;
    acknowledge = false;
  }
  if (!proposed.empty()) {
    ImGui::Text("New shortcut: %s", shortcutLabel(proposed).c_str());
    bool clashes = false;
    {
      std::lock_guard<std::mutex> lock(catalogMutex);
      for (const auto &a : allBindings)
        if (conflict(proposed, a.cachedKeys)) {
          clashes = true;
          ImGui::TextWrapped("Overlaps %s / %s", UI::name(a).c_str(),
                             UI::actionName(a).c_str());
        }
    }
    if (clashes)
      ImGui::Checkbox("Save opener despite overlap", &acknowledge);
    ImGui::BeginDisabled(clashes && !acknowledge);
    if (ImGui::Button("Save opener")) {
      const bool saved = saveOpener(proposed);
      status = saved ? "Shortcut updated." : "Could not save the shortcut.";
      if (saved)
        proposed.clear();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Discard shortcut"))
      proposed.clear();
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Custom tabs");
  if (ImGui::BeginCombo("##TabToEdit", editingTab && modLayout.tabs.count(editingTab)
                                           ? modLayout.tabs.at(editingTab).c_str()
                                           : "New tab")) {
    if (ImGui::Selectable("New tab", !editingTab)) {
      editingTab = 0;
      tabName[0] = 0;
    }
    for (const auto &[id, name] : modLayout.tabs) {
      ImGui::PushID(id);
      if (ImGui::Selectable(name.c_str(), editingTab == id)) {
        editingTab = id;
        snprintf(tabName, sizeof tabName, "%s", name.c_str());
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  ImGui::InputText("Tab name", tabName, sizeof tabName);
  if (ImGui::Button(editingTab ? "Rename tab" : "Add tab")) {
    const auto name = trim(tabName);
    auto next = modLayout;
    bool used = name.empty() || name.find("##") != name.npos ||
                lower(name) == "mod menus" || lower(name) == "toggles" ||
                lower(name) == "hotkeys" || lower(name) == "settings";
    for (const auto &[id, existing] : next.tabs)
      used |= id != editingTab && lower(existing) == lower(name);
    if (used) {
      status = "Enter a unique tab name.";
    } else {
      int id = editingTab ? editingTab : firstCustomTab;
      while (!editingTab && next.tabs.count(id))
        ++id;
      next.tabs[id] = name;
      if (saveLayout(std::move(next))) {
        editingTab = id;
        status = "Tab saved.";
      } else {
        status = "Could not save the tab.";
      }
    }
  }
  if (editingTab) {
    ImGui::SameLine();
    if (ImGui::Button("Delete tab")) {
      auto next = modLayout;
      next.tabs.erase(editingTab);
      for (auto &place : next.placements)
        if (place.tab == editingTab)
          place.tab = 0;
      if (saveLayout(std::move(next))) {
        editingTab = 0;
        tabName[0] = 0;
        status = "Tab deleted. Its mods returned to their default tabs.";
      } else {
        status = "Could not delete the tab.";
      }
    }
  }
  if (!status.empty())
    ImGui::TextWrapped("%s", status.c_str());

  ImGui::Separator();
  ImGui::TextUnformatted("Mod placement");
  std::map<int, std::vector<size_t>> rows;
  for (size_t i = 0; i < actions.size(); ++i)
    rows[actionTab(actions[i])].push_back(i);
  if (ImGui::BeginTable("Placement", 3, ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Mod");
    ImGui::TableSetupColumn("Tab");
    ImGui::TableSetupColumn("Order", ImGuiTableColumnFlags_WidthFixed, 110 * UI::scale);
    ImGui::TableHeadersRow();
    for (const auto &[tab, indices] : rows) {
      for (size_t row = 0; row < indices.size(); ++row) {
        const auto &action = actions[indices[row]];
        const auto binding = identity(action);
        ImGui::PushID(binding.c_str());
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(UI::name(action).c_str());
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%s", UI::actionName(action).c_str());
        ImGui::TableSetColumnIndex(1);
        const int current = assignedTab(binding);
        const char *defaultName = action.toggle ? "Toggles" : "Mod menus";
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##Destination", current ? modLayout.tabs.at(current).c_str()
                                                        : defaultName)) {
          int destination = current;
          if (ImGui::Selectable(defaultName, current == 0))
            destination = 0;
          for (const auto &[id, name] : modLayout.tabs)
            if (ImGui::Selectable(name.c_str(), current == id))
              destination = id;
          ImGui::EndCombo();
          if (destination != current) {
            auto next = modLayout;
            placementFor(next, binding).tab = destination;
            status = saveLayout(std::move(next)) ? "Placement saved."
                                                  : "Could not save placement.";
          }
        }
        ImGui::TableSetColumnIndex(2);
        ImGui::BeginDisabled(row == 0);
        const bool up = ImGui::SmallButton("Up");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(row + 1 == indices.size());
        const bool down = ImGui::SmallButton("Down");
        ImGui::EndDisabled();
        if (up || down) {
          auto next = modLayout;
          for (const auto &a : actions)
            placementFor(next, identity(a));
          const auto other = identity(actions[indices[up ? row - 1 : row + 1]]);
          auto &first = placementFor(next, binding);
          auto &second = placementFor(next, other);
          std::swap(first, second);
          status = saveLayout(std::move(next)) ? "Order saved." : "Could not save order.";
        }
        ImGui::PopID();
      }
    }
    ImGui::EndTable();
  }
  ImGui::EndChild();
}
