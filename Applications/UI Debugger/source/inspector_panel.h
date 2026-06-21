// Copyright 2026

#pragma once

#include <functional>
#include <memory>
#include <string_view>

#include "inspected_node.h"
#include "perception/ui/node.h"

class InspectorPanel {
 public:
  using OnTweakPropertyFunc = std::function<void(std::string_view property_name,
                                                 std::string_view property_value)>;

  explicit InspectorPanel(OnTweakPropertyFunc on_tweak_property);

  std::shared_ptr<::perception::ui::Node> GetUiNode();

  void Update(std::shared_ptr<InspectedNode> selected_node, bool is_live);

 private:
  OnTweakPropertyFunc on_tweak_property_;
  std::shared_ptr<::perception::ui::Node> ui_node_;
  std::shared_ptr<::perception::ui::Node> inspector_container_;
  std::weak_ptr<InspectedNode> selected_node_;
  bool is_live_;
};
