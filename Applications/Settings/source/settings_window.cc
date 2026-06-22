// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "settings_window.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "perception/processes.h"
#include "perception/registry.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/checkbox.h"
#include "perception/ui/components/color_picker_dialog.h"
#include "perception/ui/components/combo_box.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/resizable_container.h"
#include "perception/ui/components/scroll_container.h"
#include "perception/ui/components/tooltip.h"
#include "perception/ui/components/tree_view.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "schema.h"
#include "setting_components.h"
#include "staged_changes.h"

using ::perception::DeleteRegistryValue;
using ::perception::GetRegistryKeys;
using ::perception::GetRegistryValue;
using ::perception::RegistryCorpus;
using ::perception::TerminateProcess;
using ::perception::serialization::Value;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::Checkbox;
using ::perception::ui::components::ComboBox;
using ::perception::ui::components::Container;
using ::perception::ui::components::InputBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::ResizableContainer;
using ::perception::ui::components::ResizableContainerItem;
using ::perception::ui::components::ScrollContainer;
using ::perception::ui::components::Tooltip;
using ::perception::ui::components::TreeView;
using ::perception::ui::components::TreeViewItem;
using ::perception::ui::components::UiWindow;

namespace {

std::shared_ptr<Node> left_list_container;
std::shared_ptr<Node> right_container;
std::shared_ptr<Node> right_scroll_node;
std::shared_ptr<Label> status_label;
std::shared_ptr<Button> apply_button;
std::shared_ptr<Button> revert_button;
std::shared_ptr<Node> settings_window;

struct PageTreeNode {
  std::string name;
  std::string full_path;
  std::map<std::string, std::shared_ptr<PageTreeNode>> children;
  bool has_settings = false;
};

std::vector<std::string> SplitPagePath(std::string_view page_str) {
  std::vector<std::string> parts;
  std::string current;
  for (char c : page_str) {
    if (c == '>' || c == '/') {
      if (!current.empty()) {
        parts.push_back(current);
        current.clear();
      }
    } else {
      current += c;
    }
  }
  if (!current.empty()) parts.push_back(current);
  return parts;
}

std::vector<std::shared_ptr<Node>> BuildTreeNodes(
    std::shared_ptr<PageTreeNode> node,
    std::shared_ptr<TreeViewItem>& item_to_select) {
  std::vector<std::shared_ptr<Node>> item_nodes;
  for (const auto& [child_name, child_node] : node->children) {
    auto child_item_nodes = BuildTreeNodes(child_node, item_to_select);
    std::shared_ptr<Node> tv_item_node;
    if (child_item_nodes.empty()) {
      tv_item_node = TreeViewItem::Item(child_name);
    } else {
      tv_item_node = TreeViewItem::Item(child_name, child_item_nodes);
    }
    auto tv_item_comp = tv_item_node->Get<TreeViewItem>();
    if (tv_item_comp) {
      if (!child_item_nodes.empty()) tv_item_comp->SetExpanded(true);
      std::string path = child_node->full_path;
      tv_item_comp->OnSelect([path]() {
        selected_page_path = path;
        RefreshRightPanel(false);
      });
      if (path == selected_page_path) item_to_select = tv_item_comp;
    }
    item_nodes.push_back(tv_item_node);
  }
  return item_nodes;
}

// Constructs a list of options for the dropdown to filter settings by packages.
std::vector<std::string> PackageDropdownOptions() {
  std::vector<std::string> pkg_options;
  pkg_options.push_back("Everything");
  for (const auto& pkg : all_packages) pkg_options.push_back(pkg.display_name);
  return pkg_options;
}

struct OrphanedKey {
  RegistryCorpus corpus;
  std::string ns_name;
  std::string key;
  Value val;
};
std::vector<OrphanedKey> orphaned_keys;

void ScanForOrphanedKeys() {
  orphaned_keys.clear();
  auto check_pkg = [&](RegistryCorpus corpus, const std::string& ns_name) {
    auto keys_or = GetRegistryKeys(corpus, ns_name);
    if (!keys_or.Ok()) return;
    for (const auto& k : *keys_or) {
      std::string ck = (corpus == RegistryCorpus::APPLICATIONS ? "a:" : "l:") +
                       ns_name + ":" + k;
      if (all_settings.find(ck) == all_settings.end()) {
        Value val;
        auto val_or = GetRegistryValue(corpus, ns_name, k);
        if (val_or.Ok()) val = *val_or;
        orphaned_keys.push_back({corpus, ns_name, k, val});
      }
    }
  };
  for (const auto& pkg : all_packages) check_pkg(pkg.corpus, pkg.ns_name);
}

void PrepopulateOriginalValues() {
  for (const auto& [change_key, setting] : all_settings) {
    if (original_values.find(change_key) == original_values.end()) {
      Value val;
      auto val_or =
          GetRegistryValue(setting.corpus, setting.ns_name, setting.key);
      if (val_or.Ok()) {
        val = *val_or;
      } else {
        val = setting.default_val;
      }
      original_values[change_key] = val;
    }
  }
}

}  // namespace

void UpdateButtonStates() {
  bool has_changes = !staged_changes.empty();
  if (apply_button && revert_button) {
    if (has_changes) {
      apply_button->SetButtonStyle(Button::ButtonStyle::PRIMARY);
      revert_button->SetButtonStyle(Button::ButtonStyle::LIGHT);
    } else {
      apply_button->SetButtonStyle(Button::ButtonStyle::DISABLED);
      revert_button->SetButtonStyle(Button::ButtonStyle::DISABLED);
    }
  }
  if (status_label) {
    if (has_changes) {
      status_label->SetText(std::to_string(staged_changes.size()) +
                            " pending change(s)");
      status_label->SetColor(0xFFD97706);
    } else {
      status_label->SetText("No pending changes");
      status_label->SetColor(0xFF9CA3AF);
    }
  }
}

void RefreshLeftPanel() {
  auto tree_view = left_list_container->Get<TreeView>();
  if (tree_view && tree_view->GetContentContainer()) {
    tree_view->GetContentContainer()->RemoveChildren();
  }

  auto root = std::make_shared<PageTreeNode>();

  for (const auto& [change_key, setting] : all_settings) {
    if (selected_package_index > 0) {
      if (selected_package_index <= static_cast<int>(all_packages.size())) {
        const auto& pkg = all_packages[selected_package_index - 1];
        if (setting.corpus != pkg.corpus || setting.ns_name != pkg.ns_name)
          continue;
      }
    }

    auto parts = SplitPagePath(setting.page);
    if (parts.empty()) continue;

    auto curr = root;
    std::string curr_path;
    for (const auto& part : parts) {
      if (curr_path.empty()) {
        curr_path = part;
      } else {
        curr_path += ">" + part;
      }
      if (curr->children.find(part) == curr->children.end()) {
        auto child = std::make_shared<PageTreeNode>();
        child->name = part;
        child->full_path = curr_path;
        curr->children[part] = child;
      }
      curr = curr->children[part];
    }
    curr->has_settings = true;
  }

  std::shared_ptr<TreeViewItem> item_to_select;
  auto root_items = BuildTreeNodes(root, item_to_select);
  if (tree_view && tree_view->GetContentContainer()) {
    auto content = tree_view->GetContentContainer();
    for (const auto& node : root_items) content->AddChild(node);
  }
  if (tree_view) {
    if (!item_to_select && !root_items.empty()) {
      item_to_select = root_items[0]->Get<TreeViewItem>();
      if (item_to_select && !root->children.empty())
        selected_page_path = root->children.begin()->second->full_path;
    }
    if (item_to_select) tree_view->SetSelectedItem(item_to_select, false);
  }

  left_list_container->Invalidate();
  RefreshRightPanel(false);
}

void RefreshRightPanel(bool preserve_scroll) {
  ::perception::ui::Point current_scroll{0.0f, 0.0f};
  if (right_scroll_node) {
    auto sc = right_scroll_node->Get<ScrollContainer>();
    if (sc) current_scroll = sc->ContentPosition();
  }

  ::perception::ui::Point target_scroll =
      preserve_scroll ? current_scroll : ::perception::ui::Point{0.0f, 0.0f};

  if (selected_page_path.empty()) {
    right_container->RemoveChildren();
    if (!(target_scroll == current_scroll) && right_scroll_node) {
      auto sc = right_scroll_node->Get<ScrollContainer>();
      if (sc) sc->SetContentPosition(target_scroll);
    }
    return;
  }

  std::vector<std::shared_ptr<Node>> new_children;

  std::map<std::string, std::vector<std::string>> page_cards;

  for (const auto& [change_key, setting] : all_settings) {
    if (selected_package_index > 0) {
      if (selected_package_index <= static_cast<int>(all_packages.size())) {
        const auto& pkg = all_packages[selected_package_index - 1];
        if (setting.corpus != pkg.corpus || setting.ns_name != pkg.ns_name)
          continue;
      }
    }

    bool matches = false;
    if (setting.page == selected_page_path) {
      matches = true;
    } else if (setting.page.size() > selected_page_path.size() &&
               setting.page[selected_page_path.size()] == '>' &&
               setting.page.compare(0, selected_page_path.size(),
                                    selected_page_path) == 0) {
      matches = true;
    }

    if (matches) page_cards[setting.page].push_back(change_key);
  }

  float max_page_width = 520.0f;
  for (const auto& [page_str, change_keys] : page_cards) {
    for (const auto& change_key : change_keys) {
      const auto& setting = all_settings[change_key];
      if (setting.type == SettingType::TABLE) {
        float tw = 70.0f;
        for (const auto& col : setting.table_columns) {
          tw += GetTableColumnWidth(col.type);
        }
        max_page_width = std::max(max_page_width, tw + 32.0f);
      }
    }
  }
  right_container->GetLayout().SetMinWidth(max_page_width);

  for (const auto& [page_str, change_keys] : page_cards) {
    float min_card_width = 520.0f;
    for (const auto& change_key : change_keys) {
      const auto& setting = all_settings[change_key];
      if (setting.type == SettingType::TABLE) {
        float tw = 70.0f;
        for (const auto& col : setting.table_columns) {
          tw += GetTableColumnWidth(col.type);
        }
        min_card_width = std::max(min_card_width, tw + 32.0f);
      }
    }

    auto card_container = Container::VerticalContainer(
        [](Block& block) {
          block.SetFillColor(0xFFFFFFFF);
          block.SetBorderColor(0xFFE5E7EB);
          block.SetBorderWidth(1.0f);
          block.SetBorderRadius(8.0f);
          block.SetClipContents(true);
        },
        [min_card_width](Layout& layout) {
          layout.SetPadding(YGEdgeAll, 8.0f);
          layout.SetAlignItems(YGAlignStretch);
          layout.SetMinWidth(min_card_width);
        });

    std::string card_title;
    if (page_str == selected_page_path) {
      auto pos = page_str.rfind('>');
      card_title =
          (pos == std::string::npos) ? page_str : page_str.substr(pos + 1);
    } else {
      card_title = page_str.substr(selected_page_path.size() + 1);
      size_t pos = 0;
      while ((pos = card_title.find('>', pos)) != std::string::npos) {
        card_title.replace(pos, 1, " > ");
        pos += 3;
      }
    }

    card_container->AddChild(Label::BasicLabel(
        card_title, [](Label& label) { label.SetColor(0xFF111827); }));

    for (const auto& change_key : change_keys) {
      const auto& setting = all_settings[change_key];
      RegistryCorpus corpus = setting.corpus;
      std::string ns_name = setting.ns_name;
      std::string key = setting.key;

      if (original_values.find(change_key) == original_values.end()) {
        Value val;
        auto val_or = GetRegistryValue(corpus, ns_name, key);
        if (val_or.Ok()) {
          val = *val_or;
        } else {
          val = setting.default_val;
        }
        original_values[change_key] = val;
      }

      Value display_val = original_values[change_key];
      auto staged_it = staged_changes.find(change_key);
      if (staged_it != staged_changes.end())
        display_val = staged_it->second.value;

      if (setting.type == SettingType::TABLE) {
        card_container->AddChild(
            BuildTableSetting(corpus, ns_name, key, change_key, setting));
      } else {
        std::shared_ptr<Node> name_label;
        card_container->AddChild(Container::HorizontalContainer(
            [](Layout& layout) {
              layout.SetJustifyContent(YGJustifySpaceBetween);
              layout.SetAlignItems(YGAlignCenter);
              layout.SetPadding(YGEdgeVertical, 8.0f);
              layout.SetBorder(YGEdgeBottom, 1.0f);
            },
            [](Block& block) { block.SetBorderColor(0xFFF3F4F6); },
            Container::VerticalContainer(
                [](Layout& layout) {
                  layout.SetFlexShrink(1.0f);
                  layout.SetMinWidth(220.0f);
                  layout.SetMargin(YGEdgeRight, 16.0f);
                },
                Label::BasicLabel(
                    setting.name.empty() ? key : setting.name,
                    [](Label& label) { label.SetColor(0xFF1F2937); },
                    &name_label),
                Label::BasicLabel(
                    setting.description,
                    [](Label& label) { label.SetColor(0xFF6B7280); },
                    [](Layout& layout) { layout.SetMargin(YGEdgeTop, 2.0f); })),
            BuildSettingComponent(corpus, ns_name, key, change_key, setting,
                                  display_val)));

        Tooltip::Attach(
            name_label,
            "Default value: " + RegistryValueToString(setting.default_val));
      }
    }

    new_children.push_back(card_container);
  }

  if (!orphaned_keys.empty()) {
    new_children.push_back(Label::BasicLabel(
        "Orphaned Settings (Registry Clean Up)",
        [](Label& label) { label.SetColor(0xFF990000); },
        [](Layout& layout) {
          layout.SetMargin(YGEdgeTop, 24.0f);
          layout.SetMargin(YGEdgeBottom, 8.0f);
        }));

    for (const auto& ok : orphaned_keys) {
      new_children.push_back(Label::BasicLabel(
          ok.ns_name + ":" + ok.key + " = " + RegistryValueToString(ok.val),
          [](Label& label) { label.SetColor(0xFF666666); },
          [](Layout& layout) { layout.SetMargin(YGEdgeBottom, 4.0f); }));
    }

    new_children.push_back(Button::TextButton(
        "Clean Up Orphaned Keys",
        []() {
          for (const auto& ok : orphaned_keys) {
            DeleteRegistryValue(ok.corpus, ok.ns_name, ok.key);
          }
          ScanForOrphanedKeys();
          RefreshRightPanel();
        },
        [](Layout& layout) {
          layout.SetMargin(YGEdgeTop, 12.0f);
          layout.SetHeight(28.0f);
          layout.SetWidth(200.0f);
        }));
  }

  right_container->ReplaceChildren(new_children);

  if (right_scroll_node && (!(target_scroll == current_scroll) ||
                            target_scroll.x > 0 || target_scroll.y > 0)) {
    auto sc = right_scroll_node->Get<ScrollContainer>();
    if (sc) {
      if (settings_window) {
        auto win_layout = settings_window->GetLayout();
        win_layout.CalculateIfDirty(win_layout.GetCalculatedWidth(),
                                    win_layout.GetCalculatedHeight());
      }
      sc->SetContentPosition(target_scroll);
    }
  }
}

void InitializeSettingsWindow() {
  PrepopulateOriginalValues();
  ScanForOrphanedKeys();

  left_list_container = TreeView::Create();

  right_container = Container::VerticalContainer([](Layout& layout) {
    layout.SetAlignItems(YGAlignStretch);
    layout.SetMinWidth(520.0f);
  });

  right_scroll_node = ScrollContainer::BidirectionalScrollContainer(
      right_container, [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetMinHeight(0.0f);
        layout.SetMinWidth(0.0f);
      });

  settings_window = UiWindow::ResizableWindowWithTitleBar(
      "Settings",
      [](UiWindow& window) { window.OnClose([]() { TerminateProcess(); }); },
      ResizableContainer::HorizontalContainer(
          [](Layout& layout) {
            layout.SetFlexGrow(1.0f);
            layout.SetFlexShrink(1.0f);
            layout.SetMinHeight(0.0f);
            layout.SetWidthPercent(100.0f);
            layout.SetHeightPercent(100.0f);
          },
          Container::VerticalContainer(
              [](ResizableContainerItem& item) {
                item.SetBehavior(ResizableContainerItem::Behavior::Fixed);
              },
              [](Layout& layout) {
                layout.SetWidth(240.0f);
                layout.SetHeightPercent(100.0f);
                layout.SetMinHeight(0.0f);
              },
              Container::HorizontalContainer(
                  [](Layout& layout) {
                    layout.SetWidthPercent(100.0f);
                    layout.SetPadding(YGEdgeAll, 8.0f);
                    layout.SetAlignItems(YGAlignCenter);
                    layout.SetBorder(YGEdgeBottom, 1.0f);
                  },
                  [](Block& block) {
                    block.SetFillColor(0xFFF9FAFB);
                    block.SetBorderColor(0xFFE5E7EB);
                  },
                  ComboBox::BasicComboBox(
                      PackageDropdownOptions(), selected_package_index,
                      [](int idx) {
                        selected_package_index = idx;
                        RefreshLeftPanel();
                      },
                      [](Layout& layout) { layout.SetWidthPercent(100.0f); })),
              left_list_container),
          Container::VerticalContainer(
              [](ResizableContainerItem& item) {
                item.SetBehavior(ResizableContainerItem::Behavior::Flex);
              },
              [](Layout& layout) {
                layout.SetFlexGrow(1.0f);
                layout.SetFlexShrink(1.0f);
                layout.SetMinHeight(0.0f);
                layout.SetHeightPercent(100.0f);
                layout.SetAlignItems(YGAlignStretch);
              },
              right_scroll_node,
              Container::HorizontalContainer(
                  [](Layout& layout) { layout.SetAlignItems(YGAlignCenter); },
                  Label::BasicLabel(
                      "No pending changes",
                      [](Label& label) { label.SetColor(0xFF9CA3AF); },
                      [](Layout& layout) { layout.SetFlexGrow(1.0f); },
                      &status_label),
                  Button::TextButton(
                      "Revert", []() { RevertStagedChanges(); },
                      &revert_button),
                  Button::TextButton(
                      "Apply", []() { ApplyStagedChanges(); },
                      &apply_button)))));

  UpdateButtonStates();
  RefreshLeftPanel();
}
