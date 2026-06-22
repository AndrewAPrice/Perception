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

#include "setting_components.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "perception/registry.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/checkbox.h"
#include "perception/ui/components/color_picker_dialog.h"
#include "perception/ui/components/combo_box.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/tooltip.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "schema.h"
#include "settings_window.h"
#include "staged_changes.h"

using ::perception::RegistryCorpus;
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
using ::perception::ui::components::Tooltip;

float GetTableColumnWidth(SettingType type) {
  switch (type) {
    case SettingType::PERMISSION:
      return 220.0f;
    case SettingType::BOOLEAN:
      return 80.0f;
    default:
      return 160.0f;
  }
}

std::shared_ptr<Node> BuildTableCellComponent(
    RegistryCorpus corpus, const std::string& ns_name, const std::string& key,
    int row_idx, int col_idx, const TableColumn& col, const Value& cell_val) {
  auto cell_wrapper = Container::HorizontalContainer([col](Layout& layout) {
    layout.SetWidth(GetTableColumnWidth(col.type));
    layout.SetFlexShrink(0.0f);
    layout.SetAlignItems(YGAlignCenter);
    layout.SetPadding(YGEdgeRight, 4.0f);
  });

  switch (col.type) {
    case SettingType::BOOLEAN: {
      cell_wrapper->AddChild(Checkbox::BasicCheckbox(
          "", cell_val.BoolValue().value_or(false),
          [corpus, ns_name, key, row_idx, col_idx](bool checked) {
            Value val;
            val.SetBool(checked);
            UpdateTableCell(corpus, ns_name, key, row_idx, col_idx, val);
          }));
      break;
    }
    case SettingType::PERMISSION: {
      std::vector<std::string> options = {
          "CanReadAllFiles", "CanLaunchPrograms",
          "CanViewAndModifyEntireRegistry", "CanUseNetworkDevice",
          "CanContinueRunningAfterWindowsClose"};
      std::string curr_str = std::string(cell_val.StringValue().value_or(""));
      int selection = 0;
      for (int i = 0; i < static_cast<int>(options.size()); ++i) {
        if (options[i] == curr_str) {
          selection = i;
          break;
        }
      }
      cell_wrapper->AddChild(ComboBox::BasicComboBox(
          options, selection,
          [corpus, ns_name, key, row_idx, col_idx, options](int idx) {
            if (idx >= 0 && idx < static_cast<int>(options.size())) {
              Value val;
              val.SetString(options[idx]);
              UpdateTableCell(corpus, ns_name, key, row_idx, col_idx, val);
            }
          },
          [](Layout& layout) { layout.SetWidthPercent(100.0f); }));
      break;
    }
    case SettingType::APPLICATION: {
      std::vector<std::string> options = GetInstalledApplications();
      std::string curr_str = std::string(cell_val.StringValue().value_or(""));
      int selection = -1;
      for (int i = 0; i < static_cast<int>(options.size()); ++i) {
        if (options[i] == curr_str) {
          selection = i;
          break;
        }
      }
      if (selection == -1 && !curr_str.empty()) {
        options.push_back(curr_str);
        selection = options.size() - 1;
      }
      if (selection == -1) selection = 0;
      cell_wrapper->AddChild(ComboBox::BasicComboBox(
          options, selection,
          [corpus, ns_name, key, row_idx, col_idx, options](int idx) {
            if (idx >= 0 && idx < static_cast<int>(options.size())) {
              Value val;
              val.SetString(options[idx]);
              UpdateTableCell(corpus, ns_name, key, row_idx, col_idx, val);
            }
          },
          [](Layout& layout) { layout.SetWidthPercent(100.0f); }));
      break;
    }
    default: {
      std::string text = std::string(cell_val.StringValue().value_or(""));
      auto input = InputBox::BasicInputBox(text, [](Layout& layout) {
        layout.SetHeight(22.0f);
        layout.SetWidthPercent(100.0f);
      });
      auto input_comp = input->Get<InputBox>();
      if (input_comp) {
        input_comp->OnTextChanged([corpus, ns_name, key, row_idx,
                                   col_idx](std::string_view text_str) {
          Value val;
          val.SetString(std::string(text_str));
          UpdateTableCell(corpus, ns_name, key, row_idx, col_idx, val);
        });
      }
      cell_wrapper->AddChild(input);
      break;
    }
  }

  return cell_wrapper;
}

std::shared_ptr<Node> BuildSettingComponent(RegistryCorpus corpus,
                                            const std::string& ns_name,
                                            const std::string& key,
                                            const std::string& change_key,
                                            const ActiveSetting& setting,
                                            const Value& display_val) {
  switch (setting.type) {
    case SettingType::OPTIONS: {
      int default_sel = 0;
      std::string curr_str =
          std::string(display_val.StringValue().value_or(""));
      for (int i = 0; i < static_cast<int>(setting.options.size()); ++i) {
        if (setting.options[i] == curr_str) {
          default_sel = i;
          break;
        }
      }
      return ComboBox::BasicComboBox(
          setting.options, default_sel,
          [corpus, ns_name, key, setting, change_key](int idx) {
            if (idx >= 0 && idx < static_cast<int>(setting.options.size())) {
              Value val;
              val.SetString(setting.options[idx]);
              StageChange(corpus, ns_name, key, val,
                          original_values[change_key]);
            }
          },
          [](Layout& layout) {
            layout.SetMinWidth(140.0f);
            layout.SetFlexShrink(0.0f);
          });
    }
    case SettingType::BOOLEAN: {
      auto checkbox = Checkbox::BasicCheckbox(
          "Enabled", display_val.BoolValue().value_or(false),
          [corpus, ns_name, key, change_key](bool checked) {
            Value val;
            val.SetBool(checked);
            StageChange(corpus, ns_name, key, val, original_values[change_key]);
          });
      checkbox->GetLayout().SetFlexShrink(0.0f);
      return checkbox;
    }
    case SettingType::COLOR: {
      char hex_buf[10];
      auto color = display_val.ColorRGBValue().value_or(0);
      std::sprintf(hex_buf, "#%02x%02x%02x", (color >> 16) & 0xFF,
                   (color >> 8) & 0xFF, color & 0xFF);

      return Container::HorizontalContainer(
          [](Layout& layout) {
            layout.SetAlignItems(YGAlignCenter);
            layout.SetFlexShrink(0.0f);
          },
          Node::Empty(
              [](Layout& layout) {
                layout.SetWidth(24.0f);
                layout.SetHeight(24.0f);
                layout.SetMargin(YGEdgeRight, 8.0f);
              },
              [display_val](Block& block) {
                auto color = display_val.ColorRGBValue().value_or(0);
                block.SetFillColor(color);
                block.SetBorderColor(0xFFD1D5DB);
                block.SetBorderWidth(1.0f);
                block.SetBorderRadius(4.0f);
              }),
          Label::BasicLabel(
              hex_buf,
              [](Layout& layout) { layout.SetMargin(YGEdgeRight, 12.0f); },
              [](Label& label) { label.SetColor(0xFF374151); }),
          Button::TextButton(
              "Choose...",
              [corpus, ns_name, key, change_key, color]() {
                ::perception::ui::components::ShowColorPickerDialog(
                    key, color,
                    [corpus, ns_name, key, change_key](bool succeeded,
                                                       uint32 selected_color) {
                      if (succeeded) {
                        StageChange(corpus, ns_name, key, Value(selected_color),
                                    original_values[change_key]);
                        RefreshRightPanel();
                      }
                    });
              },
              [](Layout& layout) {
                layout.SetWidth(80.0f);
                layout.SetHeight(28.0f);
              }));
    }
    case SettingType::INTEGER:
    case SettingType::FLOAT:
    case SettingType::STRING:
    default: {
      std::string text_val = RegistryValueToString(display_val);
      auto input = InputBox::BasicInputBox(text_val, [](Layout& layout) {
        layout.SetHeight(24.0f);
        layout.SetWidth(200.0f);
        layout.SetFlexShrink(0.0f);
      });
      auto input_comp = input->Get<InputBox>();
      if (input_comp) {
        input_comp->OnTextChanged([corpus, ns_name, key, setting,
                                   change_key](std::string_view text) {
          std::string text_str{text};
          Value val;
          if (setting.type == SettingType::INTEGER) {
            try {
              int i_val = std::stoi(text_str);
              val.SetInteger(i_val);
            } catch (...) {
              return;
            }
          } else if (setting.type == SettingType::FLOAT) {
            try {
              float f_val = std::stof(text_str);
              val.SetFloat(f_val);
            } catch (...) {
              return;
            }
          } else {
            val.SetString(text_str);
          }
          StageChange(corpus, ns_name, key, val, original_values[change_key]);
        });
      }
      return input;
    }
  }
}

std::shared_ptr<Node> BuildTableSetting(RegistryCorpus corpus,
                                        const std::string& ns_name,
                                        const std::string& key,
                                        const std::string& change_key,
                                        const ActiveSetting& setting) {
  auto row = Container::VerticalContainer([](Layout& layout) {
    layout.SetMargin(YGEdgeBottom, 12.0f);
    layout.SetAlignItems(YGAlignStretch);
  });

  std::shared_ptr<Node> name_label;
  row->AddChild(Label::BasicLabel(
      setting.name.empty() ? key : setting.name,
      [](Label& label) { label.SetColor(0xFF1F2937); }, &name_label));
  row->AddChild(Label::BasicLabel(
      setting.description, [](Label& label) { label.SetColor(0xFF6B7280); }));

  Tooltip::Attach(name_label, "Default value: " +
                                  RegistryValueToString(setting.default_val));

  float table_width = 70.0f;
  for (const auto& col : setting.table_columns) {
    table_width += GetTableColumnWidth(col.type);
  }

  auto table_container = Container::VerticalContainer(
      [](Block& block) {
        block.SetBorderColor(0xFFE5E7EB);
        block.SetBorderWidth(1.0f);
        block.SetBorderRadius(4.0f);
        block.SetFillColor(0xFFFFFFFF);
        block.SetClipContents(true);
      },
      [table_width](Layout& layout) {
        layout.SetMargin(YGEdgeTop, 4.0f);
        layout.SetMargin(YGEdgeBottom, 12.0f);
        layout.SetAlignItems(YGAlignStretch);
        layout.SetMinWidth(table_width);
      });

  auto header_row = Container::HorizontalContainer(
      [](Block& block) { block.SetFillColor(0xFFF3F4F6); },
      [](Layout& layout) {
        layout.SetPadding(YGEdgeVertical, 4.0f);
        layout.SetPadding(YGEdgeHorizontal, 6.0f);
        layout.SetAlignItems(YGAlignStretch);
        layout.SetBorder(YGEdgeBottom, 1.0f);
      });

  for (const auto& col : setting.table_columns) {
    auto col_header = Label::BasicLabel(
        col.name, [](Label& label) { label.SetColor(0xFF374151); },
        [col](Layout& layout) {
          layout.SetWidth(GetTableColumnWidth(col.type));
          layout.SetFlexShrink(0.0f);
        });
    header_row->AddChild(col_header);
  }

  header_row->AddChild(Label::BasicLabel(
      "Actions", [](Label& label) { label.SetColor(0xFF374151); },
      [](Layout& layout) {
        layout.SetWidth(70.0f);
        layout.SetFlexShrink(0.0f);
      }));
  table_container->AddChild(header_row);

  Value curr_val;
  auto staged_it = staged_changes.find(change_key);
  if (staged_it != staged_changes.end()) {
    curr_val = staged_it->second.value;
  } else {
    curr_val = original_values[change_key];
  }

  std::vector<Value> current_rows;
  if (curr_val.GetType() == Value::Type::ARRAY) {
    if (auto* arr = curr_val.ArrayValue()) current_rows = *arr;
  }

  for (int r = 0; r < static_cast<int>(current_rows.size()); ++r) {
    auto row_container = Container::HorizontalContainer([](Layout& layout) {
      layout.SetPadding(YGEdgeVertical, 2.0f);
      layout.SetPadding(YGEdgeHorizontal, 6.0f);
      layout.SetAlignItems(YGAlignCenter);
    });

    std::vector<Value> row_cells;
    if (current_rows[r].GetType() == Value::Type::ARRAY) {
      if (auto* arr = current_rows[r].ArrayValue()) row_cells = *arr;
    }

    for (int c = 0; c < static_cast<int>(setting.table_columns.size()); ++c) {
      const auto& col = setting.table_columns[c];
      Value cell_val;
      if (c < static_cast<int>(row_cells.size())) cell_val = row_cells[c];

      row_container->AddChild(
          BuildTableCellComponent(corpus, ns_name, key, r, c, col, cell_val));
    }
    row_container->AddChild(Button::TextButton(
        "Remove",
        [corpus, ns_name, key, r]() {
          RemoveTableRow(corpus, ns_name, key, r, nullptr);
        },
        [](Layout& layout) {
          layout.SetWidth(70.0f);
          layout.SetHeight(22.0f);
          layout.SetFlexShrink(0.0f);
        }));
    table_container->AddChild(row_container);
  }

  table_container->AddChild(Button::TextButton(
      "Add Row",
      [corpus, ns_name, key, setting]() {
        AddTableRow(corpus, ns_name, key, setting, nullptr);
      },
      [](Layout& layout) {
        layout.SetMargin(YGEdgeVertical, 4.0f);
        layout.SetMargin(YGEdgeHorizontal, 6.0f);
        layout.SetWidth(80.0f);
        layout.SetHeight(24.0f);
      }));

  row->AddChild(table_container);
  return row;
}
