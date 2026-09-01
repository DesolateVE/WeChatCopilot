#include "console_ui.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace wechat::chat_exporter {
namespace {

using namespace ftxui;

const Color accent = Color::RGB(43, 196, 138);
const Color accentDark = Color::RGB(20, 73, 58);
const Color muted = Color::RGB(132, 144, 152);

std::string valueOrDash(const std::string &value) {
  return value.empty() ? "—" : value;
}

std::string displayName(const ContactMatch &contact) {
  if (!contact.nickname.empty()) {
    return contact.nickname;
  }
  if (!contact.alias.empty()) {
    return contact.alias;
  }
  return contact.username;
}

bool isGroup(const ContactMatch &contact) {
  return contact.username.ends_with("@chatroom");
}

std::string primaryName(const ContactMatch &contact) {
  return contact.remark.empty() ? displayName(contact) : contact.remark;
}

std::string menuLabel(const ContactMatch &contact) {
  const std::string primary = primaryName(contact);
  const std::string secondary = displayName(contact);
  std::string label = isGroup(contact) ? "群聊  " : "联系人  ";
  label += primary;
  if (secondary != primary) {
    label += "  ·  " + secondary;
  }
  return label;
}

std::string asciiLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](char character) {
    const auto byte = static_cast<unsigned char>(character);
    return byte < 0x80 ? static_cast<char>(std::tolower(byte)) : character;
  });
  return value;
}

bool containsQuery(const ContactMatch &contact, const std::string &query) {
  if (query.empty()) {
    return true;
  }
  const std::string needle = asciiLower(query);
  return asciiLower(contact.remark).find(needle) != std::string::npos ||
         asciiLower(contact.nickname).find(needle) != std::string::npos ||
         asciiLower(contact.alias).find(needle) != std::string::npos ||
         asciiLower(contact.username).find(needle) != std::string::npos;
}

Element detailRow(const std::string &label, const std::string &value) {
  return vbox({text(label) | color(muted), paragraph(valueOrDash(value))});
}

} // namespace

std::optional<ContactMatch>
selectExportContact(const std::vector<ContactMatch> &contacts) {
  if (contacts.empty()) {
    throw std::runtime_error(
        "no exportable conversations have a matching Msg_* table");
  }

  auto screen = ScreenInteractive::Fullscreen();
  std::string query;
  std::vector<size_t> filteredIndices;
  std::vector<std::string> entries;
  int selectedIndex = 0;
  bool accepted = false;

  const auto rebuildFilter = [&] {
    filteredIndices.clear();
    entries.clear();
    for (size_t index = 0; index < contacts.size(); ++index) {
      if (containsQuery(contacts[index], query)) {
        filteredIndices.push_back(index);
        entries.push_back(menuLabel(contacts[index]));
      }
    }
    selectedIndex = 0;
  };
  rebuildFilter();

  const auto chooseAndExit = [&] {
    if (!filteredIndices.empty()) {
      accepted = true;
      screen.ExitLoopClosure()();
    }
  };

  InputOption inputOption = InputOption::Spacious();
  inputOption.multiline = false;
  inputOption.on_change = rebuildFilter;
  inputOption.on_enter = chooseAndExit;
  auto searchInput =
      Input(&query, "输入备注、显示名、微信号或 username", inputOption);

  MenuOption menuOption = MenuOption::VerticalAnimated();
  menuOption.on_enter = chooseAndExit;
  menuOption.entries_option.transform = [](const EntryState &state) {
    Element item = hbox({text(state.active ? "  ›  " : "     "),
                         text(state.label) | (state.active ? bold : dim)});
    if (state.active) {
      return item | color(accent) | bgcolor(accentDark);
    }
    return item;
  };
  auto menu = Menu(&entries, &selectedIndex, menuOption);

  auto controls = Container::Vertical({searchInput, menu});
  auto renderer = Renderer(controls, [&] {
    const std::string count = std::to_string(filteredIndices.size()) + " / " +
                              std::to_string(contacts.size()) + " 个会话";

    Element details;
    if (filteredIndices.empty()) {
      details = vbox({filler(),
                      text("没有匹配的会话") | bold | color(muted) | center,
                      text("请尝试其他关键词") | color(muted) | center,
                      filler()});
    } else {
      const size_t safeIndex = std::min(
          static_cast<size_t>(std::max(selectedIndex, 0)),
          filteredIndices.size() - 1);
      const ContactMatch &contact = contacts[filteredIndices[safeIndex]];
      details = vbox({
          hbox({text(isGroup(contact) ? " 群聊 " : " 联系人 ") | bold |
                    color(Color::Black) | bgcolor(accent),
                filler()}),
          text(primaryName(contact)) | bold | color(accent),
          separator(),
          detailRow("备注名", contact.remark),
          text(""),
          detailRow("显示名", displayName(contact)),
          text(""),
          detailRow("微信号 / alias", contact.alias),
          text(""),
          detailRow("内部 username", contact.username),
          filler(),
          text("按 Enter 导出这个会话") | color(accent),
      });
    }

    const Element searchBox =
        hbox({text(" 搜索  ") | bold | color(accent),
              searchInput->Render() | flex}) |
        borderRounded;
    const Element listPanel =
        vbox({hbox({text(" 会话列表 ") | bold, filler(),
                    text(count) | color(muted)}),
              separator(), menu->Render() | frame | flex}) |
        borderRounded | flex;
    const Element detailPanel =
        vbox({text(" 会话详情 ") | bold, separator(), details | flex}) |
        borderRounded | size(WIDTH, EQUAL, 42);

    return vbox({
               hbox({text(" WeChat Chat Exporter ") | bold | color(accent),
                     filler(), text("聊天记录导出助手") | color(muted)}),
               separator(), searchBox,
               hbox({listPanel, detailPanel}) | flex,
               separator(),
               hbox({text(" ↑↓ / PgUp PgDn ") | color(muted),
                     text("浏览") | dim, text("   / ") | color(muted),
                     text("搜索") | dim, text("   Tab ") | color(muted),
                     text("切换") | dim,
                     filler(), text(" Enter ") | color(accent),
                     text("导出") | bold, text("   Esc ") | color(muted),
                     text("取消") | dim}),
           }) |
           borderRounded;
  });

  auto app = CatchEvent(renderer, [&](const Event &event) {
    if (event == Event::Escape ||
        (!searchInput->Focused() &&
         (event == Event::Character('q') || event == Event::Character('Q')))) {
      screen.ExitLoopClosure()();
      return true;
    }
    if (event == Event::Character('/') && !searchInput->Focused()) {
      searchInput->TakeFocus();
      return true;
    }
    if (event == Event::Tab) {
      if (searchInput->Focused()) {
        menu->TakeFocus();
      } else {
        searchInput->TakeFocus();
      }
      return true;
    }
    if (event == Event::Return) {
      chooseAndExit();
      return true;
    }
    return false;
  });

  menu->TakeFocus();
  screen.Loop(app);
  if (!accepted || filteredIndices.empty()) {
    return std::nullopt;
  }
  const size_t safeIndex =
      std::min(static_cast<size_t>(std::max(selectedIndex, 0)),
               filteredIndices.size() - 1);
  return contacts[filteredIndices[safeIndex]];
}

} // namespace wechat::chat_exporter
