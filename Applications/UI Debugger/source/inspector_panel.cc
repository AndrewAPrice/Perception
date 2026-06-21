// Copyright 2026

#include "inspector_panel.h"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "inspector_panel_input_components.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/checkbox.h"
#include "perception/ui/components/color_picker_dialog.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/group_box.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/scroll_container.h"
#include "perception/ui/components/resizable_container.h"
#include "perception/ui/font.h"
#include "perception/ui/layout.h"

namespace {

std::string GetComponentDisplayName(std::string_view prefix) {
  if (prefix == "block") return "Block";
  if (prefix == "label") return "Label";
  if (prefix == "button") return "Button";
  if (prefix == "checkbox") return "Checkbox";
  if (prefix == "text_field") return "TextField";
  if (prefix == "scroll_bar") return "ScrollBar";
  if (prefix == "scroll_container") return "ScrollContainer";
  if (prefix == "image_view") return "ImageView";
  if (prefix == "tree_view") return "TreeView";
  if (prefix == "other") return "Other Properties";

  std::string result;
  bool cap_next = true;
  for (char c : prefix) {
    if (c == '_') {
      cap_next = true;
    } else {
      if (cap_next && c >= 'a' && c <= 'z') {
        result.push_back(c - 'a' + 'A');
      } else {
        result.push_back(c);
      }
      cap_next = false;
    }
  }
  return result.empty() ? "Component" : result;
}

}  // namespace

InspectorPanel::InspectorPanel(OnTweakPropertyFunc on_tweak_property)
    : on_tweak_property_(std::move(on_tweak_property)), is_live_(false) {
  using namespace ::perception::ui;
  using namespace ::perception::ui::components;

  inspector_container_ = Container::VerticalContainer([](Layout& layout) {
    layout.SetWidthPercent(100.0f);
    layout.SetGap(12.0f);
  });

  ui_node_ = Container::VerticalContainer(
      [](ResizableContainerItem& item) {
        item.SetBehavior(ResizableContainerItem::Behavior::Fixed);
      },
      [](Layout& layout) {
        layout.SetWidth(280.0f);
        layout.SetHeightPercent(100.0f);
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetMinHeight(0.0f);
        layout.SetPadding(YGEdgeAll, 12.0f);
        layout.SetGap(12.0f);
      },
      Label::BasicLabel("Node properties",
                        [](Label& l) {
                          l.SetFont(GetBold12UiFont());
                          l.SetColor(0xFF111827);
                        }),
      ScrollContainer::VerticalScrollContainer(inspector_container_,
                                               [](Layout& layout) {
                                                 layout.SetFlexGrow(1.0f);
                                                 layout.SetFlexShrink(1.0f);
                                                 layout.SetMinHeight(0.0f);
                                               }));
}

std::shared_ptr<::perception::ui::Node> InspectorPanel::GetUiNode() {
  return ui_node_;
}

void InspectorPanel::Update(std::shared_ptr<InspectedNode> selected_node,
                            bool is_live) {
  selected_node_ = selected_node;
  is_live_ = is_live;
  if (!inspector_container_) return;
  inspector_container_->RemoveChildren();

  using namespace ::perception::ui;
  using namespace ::perception::ui::components;

  if (!selected_node) {
    inspector_container_->AddChild(Label::BasicLabel(
        "Select a view on the canvas or tree to inspect.", [](Label& label) {
          label.SetColor(0xFF6B7280);
        }));
    inspector_container_->Invalidate();
    return;
  }

  // 1. Node Section
  auto node_gb = GroupBox::VerticalGroupBox("Node", [](Layout& l) {
    l.SetWidthPercent(100.0f);
  });
  node_gb->AddChild(Label::BasicLabel("ID: " + selected_node->id));
  node_gb->AddChild(Label::BasicLabel("Type: " + selected_node->name));

  if (is_live_) {
    node_gb->AddChild(Checkbox::BasicCheckbox(
        "Hidden", selected_node->is_hidden, [this](bool checked) {
          if (on_tweak_property_) {
            on_tweak_property_("hidden", checked ? "true" : "false");
          }
        }));
  } else {
    node_gb->AddChild(Label::BasicLabel(
        "Hidden: " + std::string(selected_node->is_hidden ? "True" : "False")));
  }

  // Frame subheader
  node_gb->AddChild(ui_debugger::BuildSectionHeader("Calculated Frame"));
  node_gb->AddChild(Label::BasicLabel(
      "Position: (" +
      std::to_string(static_cast<int>(selected_node->global_position.x)) + ", " +
      std::to_string(static_cast<int>(selected_node->global_position.y)) + ")"));
  node_gb->AddChild(
      Label::BasicLabel("Size: " +
                        std::to_string(static_cast<int>(selected_node->size.width)) +
                        " x " +
                        std::to_string(static_cast<int>(selected_node->size.height))));

  // Handlers subheader
  node_gb->AddChild(ui_debugger::BuildSectionHeader("Event Handlers"));
  if (selected_node->handlers.empty()) {
    node_gb->AddChild(Label::BasicLabel("None", [](Label& l) {
      l.SetColor(0xFF6B7280);
    }));
  } else {
    for (const auto& h : selected_node->handlers) {
      node_gb->AddChild(Label::BasicLabel("• " + h));
    }
  }

  // Components subheader
  node_gb->AddChild(ui_debugger::BuildSectionHeader("Attached Components"));
  if (selected_node->components.empty()) {
    node_gb->AddChild(Label::BasicLabel("None", [](Label& l) {
      l.SetColor(0xFF6B7280);
    }));
  } else {
    for (const auto& c : selected_node->components) {
      node_gb->AddChild(Label::BasicLabel("• " + c));
    }
  }

  inspector_container_->AddChild(node_gb);

  // 2. Layout Section
  std::map<std::string, std::string> props;
  for (const auto& lp : selected_node->layout_properties) {
    props[lp.first] = lp.second;
  }

  auto get_prop = [&props](std::string_view k,
                           std::string_view def = "auto") -> std::string {
    auto it = props.find(std::string(k));
    if (it != props.end() && !it->second.empty()) return it->second;
    return std::string(def);
  };

  auto make_changer = [this](std::string_view prop_key) {
    return [this, prop_key = std::string(prop_key)](std::string_view val) {
      if (on_tweak_property_) {
        on_tweak_property_("layout." + prop_key, val);
      }
    };
  };

  auto layout_gb = GroupBox::VerticalGroupBox("Layout", [](Layout& l) {
    l.SetWidthPercent(100.0f);
  });

  // Position & Display Section
  layout_gb->AddChild(ui_debugger::BuildSectionHeader("Position & Display"));
  layout_gb->AddChild(ui_debugger::BuildEnumDropdown(
      "Position Type", {"Static", "Relative", "Absolute"},
      get_prop("position_type", "0"), make_changer("position_type"),
      is_live_));
  layout_gb->AddChild(ui_debugger::BuildEnumDropdown(
      "Display", {"Flex", "None"}, get_prop("display", "0"),
      make_changer("display"), is_live_));
  layout_gb->AddChild(ui_debugger::BuildEnumDropdown(
      "Direction", {"Inherit", "LTR", "RTL"}, get_prop("direction", "0"),
      make_changer("direction"), is_live_));
  layout_gb->AddChild(ui_debugger::BuildEnumDropdown(
      "Overflow", {"Visible", "Hidden", "Scroll"}, get_prop("overflow", "0"),
      make_changer("overflow"), is_live_));

  // Flex Container Section
  layout_gb->AddChild(ui_debugger::BuildSectionHeader("Flex Container"));
  layout_gb->AddChild(ui_debugger::BuildEnumDropdown(
      "Flex Direction",
      {"Column", "Column Reverse", "Row", "Row Reverse"},
      get_prop("flex_direction", "0"), make_changer("flex_direction"),
      is_live_));
  layout_gb->AddChild(ui_debugger::BuildEnumDropdown(
      "Flex Wrap", {"No Wrap", "Wrap", "Wrap Reverse"},
      get_prop("flex_wrap", "0"), make_changer("flex_wrap"), is_live_));
  layout_gb->AddChild(ui_debugger::BuildEnumDropdown(
      "Justify Content",
      {"Flex Start", "Center", "Flex End", "Space Between", "Space Around",
       "Space Evenly"},
      get_prop("justify_content", "0"), make_changer("justify_content"),
      is_live_));
  layout_gb->AddChild(ui_debugger::BuildEnumDropdown(
      "Align Content",
      {"Auto", "Flex Start", "Center", "Flex End", "Stretch", "Baseline",
       "Space Between", "Space Around"},
      get_prop("align_content", "0"), make_changer("align_content"),
      is_live_));
  layout_gb->AddChild(ui_debugger::BuildEnumDropdown(
      "Align Items",
      {"Auto", "Flex Start", "Center", "Flex End", "Stretch", "Baseline",
       "Space Between", "Space Around"},
      get_prop("align_items", "0"), make_changer("align_items"), is_live_));

  // Flex Item Section
  layout_gb->AddChild(ui_debugger::BuildSectionHeader("Flex Item"));
  layout_gb->AddChild(ui_debugger::BuildEnumDropdown(
      "Align Self",
      {"Auto", "Flex Start", "Center", "Flex End", "Stretch", "Baseline",
       "Space Between", "Space Around"},
      get_prop("align_self", "0"), make_changer("align_self"), is_live_));
  layout_gb->AddChild(ui_debugger::BuildSingleInput(
      "Flex", get_prop("flex", "0"), make_changer("flex"), is_live_));
  layout_gb->AddChild(ui_debugger::BuildSingleInput(
      "Flex Grow", get_prop("flex_grow", "0"), make_changer("flex_grow"),
      is_live_));
  layout_gb->AddChild(ui_debugger::BuildSingleInput(
      "Flex Shrink", get_prop("flex_shrink", "0"), make_changer("flex_shrink"),
      is_live_));
  layout_gb->AddChild(ui_debugger::BuildSingleInput(
      "Flex Basis", get_prop("flex_basis", "auto"), make_changer("flex_basis"),
      is_live_));

  // Dimensions Section
  layout_gb->AddChild(ui_debugger::BuildSectionHeader("Dimensions"));
  layout_gb->AddChild(ui_debugger::BuildPairInput(
      "Size", get_prop("width", "auto"), get_prop("height", "auto"),
      make_changer("width"), make_changer("height"), is_live_));
  layout_gb->AddChild(ui_debugger::BuildPairInput(
      "Min Size", get_prop("min_width", "auto"),
      get_prop("min_height", "auto"), make_changer("min_width"),
      make_changer("min_height"), is_live_));
  layout_gb->AddChild(ui_debugger::BuildPairInput(
      "Max Size", get_prop("max_width", "auto"),
      get_prop("max_height", "auto"), make_changer("max_width"),
      make_changer("max_height"), is_live_));
  layout_gb->AddChild(ui_debugger::BuildSingleInput(
      "Aspect Ratio", get_prop("aspect_ratio", "0"),
      make_changer("aspect_ratio"), is_live_));

  // Position Offsets Section
  layout_gb->AddChild(ui_debugger::BuildSectionHeader("Position Offsets"));
  layout_gb->AddChild(ui_debugger::BuildPairInput(
      "Pos (L x T)", get_prop("left", "auto"), get_prop("top", "auto"),
      make_changer("left"), make_changer("top"), is_live_));
  layout_gb->AddChild(ui_debugger::BuildPairInput(
      "Pos (R x B)", get_prop("right", "auto"), get_prop("bottom", "auto"),
      make_changer("right"), make_changer("bottom"), is_live_));

  // Spacing Section
  layout_gb->AddChild(ui_debugger::BuildSectionHeader("Spacing"));
  layout_gb->AddChild(ui_debugger::BuildPairInput(
      "Pad (L x T)", get_prop("padding_left", "0"),
      get_prop("padding_top", "0"), make_changer("padding_left"),
      make_changer("padding_top"), is_live_));
  layout_gb->AddChild(ui_debugger::BuildPairInput(
      "Pad (R x B)", get_prop("padding_right", "0"),
      get_prop("padding_bottom", "0"), make_changer("padding_right"),
      make_changer("padding_bottom"), is_live_));
  layout_gb->AddChild(ui_debugger::BuildPairInput(
      "Mar (L x T)", get_prop("margin_left", "0"), get_prop("margin_top", "0"),
      make_changer("margin_left"), make_changer("margin_top"), is_live_));
  layout_gb->AddChild(ui_debugger::BuildPairInput(
      "Mar (R x B)", get_prop("margin_right", "0"),
      get_prop("margin_bottom", "0"), make_changer("margin_right"),
      make_changer("margin_bottom"), is_live_));
  layout_gb->AddChild(ui_debugger::BuildPairInput(
      "Border (L x T)", get_prop("border_left", "0"),
      get_prop("border_top", "0"), make_changer("border_left"),
      make_changer("border_top"), is_live_));
  layout_gb->AddChild(ui_debugger::BuildPairInput(
      "Border (R x B)", get_prop("border_right", "0"),
      get_prop("border_bottom", "0"), make_changer("border_right"),
      make_changer("border_bottom"), is_live_));
  layout_gb->AddChild(ui_debugger::BuildPairInput(
      "Gap (Col x Row)", get_prop("gap_column", "0"), get_prop("gap_row", "0"),
      make_changer("gap_column"), make_changer("gap_row"), is_live_));

  inspector_container_->AddChild(layout_gb);

  // 3. Attached Components Section
  std::map<std::string, std::vector<std::pair<std::string, std::string>>>
      comp_grouped_props;
  for (const auto& kv : selected_node->component_properties) {
    size_t dot_pos = kv.first.find('.');
    std::string prefix =
        (dot_pos != std::string::npos) ? kv.first.substr(0, dot_pos) : "other";
    comp_grouped_props[prefix].push_back(kv);
  }

  for (const auto& group : comp_grouped_props) {
    std::string title = GetComponentDisplayName(group.first);
    auto comp_gb = GroupBox::VerticalGroupBox(title, [](Layout& l) {
      l.SetWidthPercent(100.0f);
    });

    for (const auto& kv : group.second) {
      std::string full_key = kv.first;
      std::string v = kv.second;
      size_t dot_pos = full_key.find('.');
      std::string prop_name = (dot_pos != std::string::npos)
                                  ? full_key.substr(dot_pos + 1)
                                  : full_key;

      if (prop_name.size() >= 5 &&
          (prop_name.substr(prop_name.size() - 5) == "color" ||
           (prop_name.size() >= 6 &&
            prop_name.substr(prop_name.size() - 6) == "_color"))) {
        uint32 color = 0xFFFFFFFF;
        if (!v.empty()) {
          char* end_ptr = nullptr;
          std::string s(v);
          color = static_cast<uint32>(std::strtoul(s.c_str(), &end_ptr, 0));
        }
        std::shared_ptr<::perception::ui::Node> swatch;
        if (is_live_) {
          swatch = Button::BasicButton(
              [this, full_key, prop_name, color]() {
                ShowColorPickerDialog(
                    "Select " + prop_name, color,
                    [this, full_key](bool succeeded, uint32 new_color) {
                      if (succeeded) {
                        std::stringstream ss;
                        ss << "0x" << std::hex << new_color;
                        if (on_tweak_property_) {
                          on_tweak_property_(full_key, ss.str());
                        }
                        if (auto n = selected_node_.lock()) {
                          n->component_properties[full_key] = ss.str();
                          Update(n, is_live_);
                        }
                      }
                    });
              },
              [](Layout& l) {
                l.SetWidth(40.0f);
                l.SetHeight(24.0f);
                l.SetMinHeight(24.0f);
                l.SetMinWidth(40.0f);
              },
              [color](Block& b) {
                b.SetFillColor(color);
                b.SetBorderColor(0xFFD1D5DB);
                b.SetBorderWidth(1.0f);
                b.SetBorderRadius(4.0f);
              });
        } else {
          swatch = Container::HorizontalContainer(
              [](Layout& l) {
                l.SetWidth(40.0f);
                l.SetHeight(24.0f);
                l.SetMinHeight(24.0f);
                l.SetMinWidth(40.0f);
              },
              [color](Block& b) {
                b.SetFillColor(color);
                b.SetBorderColor(0xFFD1D5DB);
                b.SetBorderWidth(1.0f);
                b.SetBorderRadius(4.0f);
              });
        }

        comp_gb->AddChild(Container::HorizontalContainer(
            [](Layout& l) {
              l.SetWidthPercent(100.0f);
              l.SetAlignItems(YGAlignCenter);
              l.SetJustifyContent(YGJustifySpaceBetween);
            },
            Label::BasicLabel(prop_name), swatch));
      } else if (full_key == "checkbox.checked" || prop_name == "checked") {
        bool checked = (v == "true" || v == "True" || v == "1");
        if (is_live_) {
          comp_gb->AddChild(Checkbox::BasicCheckbox(
              "Checked", checked, [this, full_key](bool new_checked) {
                if (on_tweak_property_) {
                  on_tweak_property_(full_key,
                                     new_checked ? "true" : "false");
                }
                if (auto n = selected_node_.lock()) {
                  n->component_properties[full_key] =
                      new_checked ? "true" : "false";
                }
              }));
        } else {
          comp_gb->AddChild(Label::BasicLabel(
              "Checked: " + std::string(checked ? "True" : "False"),
              [](Label& l) { l.SetColor(0xFF6B7280); }));
        }
      } else {
        std::shared_ptr<::perception::ui::Node> val_node;
        if (is_live_) {
          val_node = InputBox::BasicInputBox(
              v,
              [](Layout& l) {
                l.SetWidth(140.0f);
                l.SetFlexGrow(1.0f);
              },
              [this, full_key](InputBox& ib) {
                ib.OnTextChanged([this, full_key](std::string_view text) {
                  if (on_tweak_property_) {
                    on_tweak_property_(full_key, text);
                  }
                  if (auto n = selected_node_.lock()) {
                    n->component_properties[full_key] = text;
                  }
                });
              });
        } else {
          val_node =
              Label::BasicLabel(v, [](Label& l) { l.SetColor(0xFF6B7280); });
        }

        comp_gb->AddChild(Container::HorizontalContainer(
            [](Layout& l) {
              l.SetWidthPercent(100.0f);
              l.SetAlignItems(YGAlignCenter);
              l.SetGap(8.0f);
            },
            Label::BasicLabel(prop_name), val_node));
      }
    }

    inspector_container_->AddChild(comp_gb);
  }

  inspector_container_->Invalidate();
}
