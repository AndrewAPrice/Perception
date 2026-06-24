// Copyright 2024 Google LLC
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

#include "applications_tab.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "applications.h"
#include "include/core/SkColor.h"
#include "launcher_window.h"
#include "perception/loader.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "perception/time.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/focusable.h"
#include "perception/ui/components/image_view.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/scroll_container.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/font.h"
#include "perception/ui/keyboard.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/text_alignment.h"
#include "perception/ui/theme.h"
#include "perception/window/keyboard_key_event.h"
#include "tabs.h"

using ::perception::GetService;
using ::perception::LoadApplicationRequest;
using ::perception::Loader;
using ::perception::ui::GetBold12UiFont;
using ::perception::ui::kBackgroundWindowColor;
using ::perception::ui::kContainerPadding;
using ::perception::ui::KeyCode;
using ::perception::ui::kLabelOnDarkTextColor;
using ::perception::ui::kLabelTextColor;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::Point;
using ::perception::ui::ResizeMethod;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::Container;
using ::perception::ui::components::Focusable;
using ::perception::ui::components::ImageView;
using ::perception::ui::components::Label;
using ::perception::ui::components::ScrollContainer;
using ::perception::ui::components::UiWindow;
using ::perception::window::KeyboardKeyEvent;
using ::perception::window::MouseButton;

namespace {

std::vector<std::weak_ptr<Node>> application_tile_nodes;

// The width of the selected application panel.
double kSelectedApplicationPanelWidth = 280.0f;

// The overlay node.
std::shared_ptr<Node> launcher_overlay;

std::shared_ptr<Node> applications_list_container;

// Active error dialog window nodes to keep them alive.
std::vector<std::shared_ptr<Node>> active_error_dialogs;

struct ErrorDialogState {
  std::weak_ptr<Node> dialog_node;
};

// Returns an error message when an application fails to load.
std::string ErrorMessageForApplicationLoading(std::string_view request_name,
                                              Status status) {
  bool is_directory = !request_name.empty() && request_name.back() == '/';
  std::string error_message =
      is_directory ? "Failed to open " : "Failed to launch ";
  error_message += std::string(request_name);

  switch (status) {
    case Status::FILE_NOT_FOUND:
      error_message +=
          is_directory ? "\nDirectory not found." : "\nFile not found.";
      break;
    case Status::NOT_ALLOWED:
      error_message += "\nPermission denied.";
      break;
    case Status::OUT_OF_MEMORY:
      error_message += "\nOut of memory.";
      break;
    default:
      error_message +=
          "\nInternal error (" + std::to_string((int)status) + ").";
      break;
  }
  return error_message;
}

// Shows an error dialog.
void ShowErrorDialogForApplicationLoading(std::string_view request_name,
                                          Status status) {
  auto state = std::make_shared<ErrorDialogState>();
  std::string message = ErrorMessageForApplicationLoading(request_name, status);

  auto window_node = UiWindow::DialogWithTitleBar(
      "Error",
      [state](UiWindow& window) {
        window.OnClose([state]() {
          ::perception::Defer([state]() {
            auto strong_node = state->dialog_node.lock();
            if (strong_node) {
              auto it = std::find(active_error_dialogs.begin(),
                                  active_error_dialogs.end(), strong_node);
              if (it != active_error_dialogs.end()) {
                active_error_dialogs.erase(it);
              }
            }
          });
        });
      },
      [](Layout& layout) {
        layout.SetWidth(320.0f);
        layout.SetHeight(140.0f);
      },
      Container::VerticalContainer(
          [](Layout& layout) {
            layout.SetFlexGrow(1.0f);
            layout.SetPadding(YGEdgeAll, 16.0f);
            layout.SetGap(16.0f);
            layout.SetAlignItems(YGAlignStretch);
          },
          [](Block& block) {
            block.SetFillColor(0xFFFFFFFF);  // White background
          },
          Label::BasicLabel(
              message,
              [](Layout& layout) {
                layout.SetFlexGrow(1.0f);
                layout.SetFlexShrink(1.0f);
              },
              [](Label& label) {
                label.SetTextAlignment(TextAlignment::MiddleLeft);
                label.SetColor(0xFF1F2937);  // Dark gray text
              }),
          Container::HorizontalContainer(
              [](Layout& layout) {
                layout.SetJustifyContent(YGJustifyFlexEnd);
                layout.SetGap(12.0f);
              },
              Button::TextButton(
                  "OK",
                  [state]() {
                    auto strong_node = state->dialog_node.lock();
                    if (strong_node) {
                      auto window = strong_node->Get<UiWindow>();
                      if (window) window->Close();
                    }
                  },
                  [](Layout& layout) {
                    layout.SetWidth(80.0f);
                    layout.SetHeight(32.0f);
                  },
                  [](Button& button) {
                    button.SetButtonStyle(Button::ButtonStyle::PRIMARY);
                  }))),
      &state->dialog_node);

  active_error_dialogs.push_back(window_node);
}

// Show the launcher overlay.
void ShowOverlay() {
  if (launcher_overlay) {
    launcher_overlay->GetLayout().SetDisplay(YGDisplayFlex);
    launcher_overlay->Invalidate();
  }
}

// Hide the launcher overlay.
void HideOverlay() {
  if (launcher_overlay) {
    launcher_overlay->GetLayout().SetDisplay(YGDisplayNone);
    launcher_overlay->Invalidate();
  }
}

// The widget containing the applications tab.
std::shared_ptr<Node> applications_tab;

// The selected application title.
std::shared_ptr<Node> selected_application_title;

// The selected application description.
std::shared_ptr<Node> selected_application_description;

// The selected application path.
std::shared_ptr<Node> selected_application_path;

// The selected application icon.
std::shared_ptr<Node> selected_application_icon;

// The button to launch an application.
std::shared_ptr<Node> launch_button_node;

// The button to open an application's folder.
std::shared_ptr<Node> open_folder_button_node;

// The index of the currently selected application.
int selected_application_index = -1;

// Launches an application.
void LaunchApplication(const LoadApplicationRequest& request) {
  ShowOverlay();
  GetService<Loader>().LaunchApplication(
      request,
      [request](StatusOr<::perception::LoadApplicationResponse> response) {
        HideOverlay();
        if (!response.Ok())
          ShowErrorDialogForApplicationLoading(request.name, response.Status());
      });
}

// Launches an application based on its index in the list of applications.
void LaunchApplication(int index) {
  const std::vector<Application>& applications = GetApplications();
  if (index < 0 || index >= applications.size()) return;

  LoadApplicationRequest request;
  request.name = applications[index].path;

  LaunchApplication(request);
}

// Opens the containing folder of the application.
void OpenContainingFolder(int index) {
  const std::vector<Application>& applications = GetApplications();
  if (index < 0 || index >= applications.size()) return;

  std::filesystem::path app_path(applications[index].path);
  std::string containing_folder = app_path.parent_path().string();
  if (containing_folder.empty() || containing_folder.back() != '/') {
    containing_folder += '/';
  }

  LoadApplicationRequest request;
  request.name = containing_folder;

  LaunchApplication(request);
}

// Creates a beautiful premium colored letter-icon dynamically.
std::shared_ptr<Node> CreateApplicationIcon(const Application& application,
                                            float size, float border_radius) {
  uint32 colors[] = {
      SkColorSetARGB(0xFF, 0xEF, 0x44, 0x44),  // Red-500
      SkColorSetARGB(0xFF, 0xF9, 0x73, 0x16),  // Orange-500
      SkColorSetARGB(0xFF, 0xF5, 0x9E, 0x0B),  // Amber-500
      SkColorSetARGB(0xFF, 0x10, 0xB9, 0x81),  // Emerald-500
      SkColorSetARGB(0xFF, 0x06, 0xB6, 0xD4),  // Cyan-500
      SkColorSetARGB(0xFF, 0x3B, 0x82, 0xF6),  // Blue-500
      SkColorSetARGB(0xFF, 0x63, 0x66, 0xF1),  // Indigo-500
      SkColorSetARGB(0xFF, 0x8B, 0x5C, 0xF6),  // Violet-500
      SkColorSetARGB(0xFF, 0xD9, 0x46, 0xEF),  // Fuchsia-500
      SkColorSetARGB(0xFF, 0xEC, 0x48, 0x99)   // Pink-500
  };

  size_t hash = 0;
  for (char c : application.name) {
    hash = c + (hash << 6) + (hash << 16) - hash;
  }
  uint32 bg_color = colors[hash % (sizeof(colors) / sizeof(uint32))];

  auto container = Node::Empty(
      [size](Layout& layout) {
        layout.SetWidth(size);
        layout.SetHeight(size);
        layout.SetAlignItems(YGAlignCenter);
        layout.SetJustifyContent(YGJustifyCenter);
      },
      [bg_color, border_radius](Block& block) {
        block.SetFillColor(bg_color);
        block.SetBorderRadius(border_radius);
      });

  std::string first_letter = "";
  if (!application.name.empty()) {
    first_letter += std::toupper(application.name[0]);
  }

  auto letter_label = Label::BasicLabel(
      first_letter, [](Layout& layout) { layout.SetFlexGrow(1.0f); },
      [](Label& label) {
        label.SetTextAlignment(TextAlignment::MiddleCenter);
        label.SetColor(kLabelOnDarkTextColor);
        label.SetFont(GetBold12UiFont());
      });

  container->AddChild(letter_label);
  return container;
}

// Selects an application based on its index in the list of applications.
void SelectApplication(int index) {
  if (selected_application_index == index) return;
  int old_index = selected_application_index;
  selected_application_index = index;
  const std::vector<Application>& applications = GetApplications();

  selected_application_icon->RemoveChildren();

  if (index < 0 || index >= applications.size()) {
    selected_application_title->Get<Label>()->SetText("");
    selected_application_description->Get<Label>()->SetText("");
    selected_application_path->Get<Label>()->SetText("");
    if (launch_button_node) {
      launch_button_node->GetLayout().SetDisplay(YGDisplayNone);
      launch_button_node->Invalidate();
    }
    if (open_folder_button_node) {
      open_folder_button_node->GetLayout().SetDisplay(YGDisplayNone);
      open_folder_button_node->Invalidate();
    }
  } else {
    const Application& application = applications[index];
    selected_application_title->Get<Label>()->SetText(application.name);
    selected_application_description->Get<Label>()->SetText(
        application.description);
    selected_application_path->Get<Label>()->SetText(application.path);
    if (launch_button_node) {
      launch_button_node->GetLayout().SetDisplay(YGDisplayFlex);
      launch_button_node->Invalidate();
    }
    if (open_folder_button_node) {
      open_folder_button_node->GetLayout().SetDisplay(YGDisplayFlex);
      open_folder_button_node->Invalidate();
    }
    if (application.icon) {
      selected_application_icon->AddChild(ImageView::BasicImage(
          application.icon,
          [](Layout& layout) {
            layout.SetWidthPercent(100.0f);
            layout.SetHeightPercent(100.0f);
          },
          [](ImageView& image_view) {
            image_view.SetAlignment(TextAlignment::MiddleCenter);
            image_view.SetResizeMethod(ResizeMethod::Contain);
          }));
    } else {
      selected_application_icon->AddChild(
          CreateApplicationIcon(application, 80.0f, 16.0f));
    }
  }
  selected_application_icon->Invalidate();

  if (old_index >= 0 && old_index < application_tile_nodes.size()) {
    if (auto node = application_tile_nodes[old_index].lock()) {
      if (auto block = node->Get<Block>()) {
        block->SetFillColor(0);
        block->SetBorderWidth(0.0f);
        node->Invalidate();
      }
    }
  }
  if (index >= 0 && index < application_tile_nodes.size()) {
    if (auto node = application_tile_nodes[index].lock()) {
      if (auto block = node->Get<Block>()) {
        block->SetFillColor(0xFFEFF6FF);    // Light blue background
        block->SetBorderColor(0xFF2563EB);  // Primary blue border
        block->SetBorderWidth(1.5f);
        node->Invalidate();
      }
      if (auto parent = applications_list_container->GetParent().lock()) {
        if (auto sc = parent->Get<ScrollContainer>()) {
          sc->ScrollIntoView(node);
        }
      }
    }
  }
}

// Creates a tile widget representing this application in the grid.
std::shared_ptr<Node> CreateApplicationGridTile(const Application& application,
                                                int index) {
  std::shared_ptr<Node> icon_view;
  if (application.icon) {
    icon_view = ImageView::BasicImage(
        application.icon,
        [](Layout& layout) {
          layout.SetWidth(56.0f);
          layout.SetHeight(56.0f);
          layout.SetMargin(YGEdgeBottom, 8.0f);
        },
        [](ImageView& image_view) {
          image_view.SetAlignment(TextAlignment::MiddleCenter);
          image_view.SetResizeMethod(ResizeMethod::Contain);
        });
  } else {
    icon_view = CreateApplicationIcon(application, 56.0f, 12.0f);
    icon_view->GetLayout().SetMargin(YGEdgeBottom, 8.0f);
  }

  bool is_selected = (selected_application_index == index);
  auto tile = Container::VerticalContainer(
      [](Layout& layout) {
        layout.SetWidth(96.0f);
        layout.SetAlignItems(YGAlignCenter);
      },
      [is_selected](Block& block) {
        block.SetBorderRadius(8.0f);
        if (is_selected) {
          block.SetFillColor(0xFFEFF6FF);
          block.SetBorderColor(0xFF2563EB);
          block.SetBorderWidth(1.5f);
        } else {
          block.SetFillColor(0);
          block.SetBorderWidth(0.0f);
        }
      },
      icon_view,
      Label::BasicLabel(
          application.name,
          [](Layout& layout) { layout.SetWidthPercent(100.0f); },
          [](Label& label) {
            label.SetTextAlignment(TextAlignment::TopCenter);
            label.SetColor(kLabelTextColor);
          }));

  tile->OnMouseHover([tile, index](const Point& point) {
    if (selected_application_index == index) return;
    uint32 hover_color = SkColorSetARGB(0xFF, 0xF0, 0xF0, 0xF0);
    if (tile->Get<Block>()->GetFillColor() != hover_color) {
      tile->Get<Block>()->SetFillColor(hover_color);
      tile->Invalidate();
    }
  });

  tile->OnMouseLeave([tile, index]() {
    if (selected_application_index == index) return;
    tile->Get<Block>()->SetFillColor(0);
    tile->Invalidate();
  });

  tile->OnMouseButtonUp([index](const Point& point, MouseButton button) {
    if (button == MouseButton::Left) {
      static size_t last_click_micros = 0;
      static int last_click_index = -1;
      auto now = perception::GetTimeSinceKernelStarted();
      size_t current_micros = now.count();
      size_t elapsed = (current_micros > last_click_micros)
                           ? (current_micros - last_click_micros)
                           : 0;

      SelectApplication(index);

      if (last_click_index == index && elapsed < 400000) {
        LaunchApplication(index);
        last_click_index = -1;
        last_click_micros = 0;
      } else {
        last_click_micros = current_micros;
        last_click_index = index;
      }
    }
  });

  return tile;
}

// // Builds the container containing the grid of applications.
std::shared_ptr<Node> BuildApplicationsList() {
  application_tile_nodes.clear();
  std::vector<std::shared_ptr<Node>> application_widgets;
  const std::vector<Application>& applications = GetApplications();
  for (int i = 0; i < applications.size(); i++) {
    auto tile = CreateApplicationGridTile(applications[i], i);
    application_tile_nodes.push_back(tile);
    application_widgets.push_back(tile);
  }

  auto container = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetFlexWrap(YGWrapWrap);
        layout.SetGap(8.0f);
        layout.SetPadding(YGEdgeAll, 12.0f);
        layout.SetAlignContent(YGAlignFlexStart);
      },
      application_widgets, &applications_list_container);

  container->OnMouseButtonUp([](const Point&, MouseButton button) {
    if (button == MouseButton::Left) SelectApplication(-1);
  });

  return container;
}

void AddApplicationToUI(const Application& application) {
  if (!applications_list_container) return;

  int index = (int)GetApplications().size() - 1;
  auto tile = CreateApplicationGridTile(application, index);
  application_tile_nodes.push_back(tile);
  applications_list_container->AddChild(tile);
  applications_list_container->Invalidate();

  if (GetApplications().size() == 1) {
    SelectApplication(0);
  }
}

}  // namespace

// Gets or constructs the applications tab of the launcher.
std::shared_ptr<Node> GetOrConstructApplicationsTab() {
  static bool callback_registered = false;
  if (!callback_registered) {
    RegisterApplicationFoundCallback(
        [](const Application& app) { AddApplicationToUI(app); });
    callback_registered = true;
  }

  if (applications_tab) {
    if (!GetApplications().empty()) {
      SelectApplication(0);
    } else {
      SelectApplication(-1);
    }
    if (auto focusable = applications_tab->Get<Focusable>()) focusable->Focus();
    return applications_tab;
  };

  launcher_overlay = Node::Empty(
      [](Layout& layout) {
        layout.SetPositionType(YGPositionTypeAbsolute);
        layout.SetPositionPercent(YGEdgeLeft, 0.0f);
        layout.SetPositionPercent(YGEdgeTop, 0.0f);
        layout.SetWidthPercent(100.0f);
        layout.SetHeightPercent(100.0f);
        layout.SetDisplay(YGDisplayNone);
      },
      [](Block& block) {
        block.SetFillColor(SkColorSetARGB(0x80, 0x80, 0x80, 0x80));
      },
      [](Node& node) { node.SetBlocksHitTest(true); });

  applications_tab = Container::VerticalContainer(
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetMinHeight(0.0f);
        layout.SetHeightPercent(100.0f);
      },
      Container::HorizontalContainer(
          [](Block& block) {
            block.SetBorderRadius(0.0f);
            block.SetBorderWidth(0.0f);
            block.SetFillColor(kBackgroundWindowColor);
          },
          [](Layout& layout) {
            layout.SetFlexGrow(1.0f);
            layout.SetFlexShrink(1.0f);
            layout.SetMinHeight(0.0f);
            layout.SetGap(12.0f);
            layout.SetPadding(YGEdgeAll, 12.0f);
          },
          ScrollContainer::VerticalScrollContainer(
              BuildApplicationsList(),
              [](Block& block) {
                block.SetFillColor(0xFFFFFFFF);    // White background
                block.SetBorderColor(0xFFCCCCCC);  // Subtle border
                block.SetBorderWidth(1.0f);        // Outline width
                block.SetBorderRadius(4.0f);       // Rounded corners
              },
              [](Layout& layout) {
                layout.SetFlexGrow(1.0f);
                layout.SetFlexShrink(1.0f);
                layout.SetMinHeight(0.0f);
              },
              [](Node& node) {
                node.OnMouseButtonUp([](const Point&, MouseButton button) {
                  if (button == MouseButton::Left) SelectApplication(-1);
                });
              }),
          Container::VerticalContainer(
              [](Layout& layout) {
                layout.SetWidth(kSelectedApplicationPanelWidth);
                layout.SetHeightPercent(100.0f);
                layout.SetAlignItems(YGAlignCenter);
                layout.SetJustifyContent(YGJustifyCenter);
                layout.SetPadding(YGEdgeAll, 20.0f);
              },
              [](Node& node) {
                node.OnMouseButtonUp([](const Point&, MouseButton) {});
              },
              Node::Empty(
                  [](Layout& layout) {
                    layout.SetWidth(80.0f);
                    layout.SetHeight(80.0f);
                    layout.SetMargin(YGEdgeBottom, 20.0f);
                  },
                  &selected_application_icon),
              Label::BasicLabel(
                  "",
                  [](Layout& layout) { layout.SetMargin(YGEdgeBottom, 8.0f); },
                  [](Label& label) {
                    label.SetTextAlignment(TextAlignment::MiddleCenter);
                    label.SetColor(kLabelTextColor);
                    label.SetFont(GetBold12UiFont());
                  },
                  &selected_application_title),
              Label::BasicLabel(
                  "",
                  [](Layout& layout) {
                    layout.SetMargin(YGEdgeBottom, 16.0f);
                    layout.SetMaxWidth(240.0f);
                  },
                  [](Label& label) {
                    label.SetTextAlignment(TextAlignment::MiddleCenter);
                    label.SetColor(SkColorSetARGB(0xFF, 0x55, 0x55, 0x55));
                  },
                  &selected_application_description),
              Label::BasicLabel(
                  "",
                  [](Layout& layout) {
                    layout.SetMargin(YGEdgeBottom, 24.0f);
                    layout.SetMaxWidth(240.0f);
                  },
                  [](Label& label) {
                    label.SetTextAlignment(TextAlignment::MiddleCenter);
                    label.SetColor(SkColorSetARGB(0xFF, 0x77, 0x77, 0x77));
                  },
                  &selected_application_path),
              Button::TextButton(
                  "Launch",
                  []() { LaunchApplication(selected_application_index); },
                  [](Layout& layout) {
                    layout.SetWidth(180.0f);
                    layout.SetHeight(32.0f);
                  },
                  [](Button& button) {
                    button.SetButtonStyle(Button::ButtonStyle::PRIMARY);
                  },
                  &launch_button_node),
              Button::TextButton(
                  "Open containing folder",
                  []() { OpenContainingFolder(selected_application_index); },
                  [](Layout& layout) {
                    layout.SetWidth(180.0f);
                    layout.SetHeight(32.0f);
                    layout.SetMargin(YGEdgeTop, 8.0f);
                  },
                  [](Button& button) {
                    button.SetButtonStyle(Button::ButtonStyle::SECONDARY);
                  },
                  &open_folder_button_node))),
      launcher_overlay);

  applications_tab->OnMouseButtonUp([](const Point&, MouseButton button) {
    if (button == MouseButton::Left) SelectApplication(-1);
  });

  auto focusable = applications_tab->GetOrAdd<Focusable>();
  focusable->OnKeyDown([](const ::perception::window::KeyboardKeyEvent& event) {
    if (GetApplications().empty()) return;
    int num_apps = (int)GetApplications().size();
    KeyCode key = static_cast<KeyCode>(event.key);

    if (selected_application_index < 0) {
      if (key == KeyCode::DownArrow || key == KeyCode::RightArrow ||
          key == KeyCode::UpArrow || key == KeyCode::LeftArrow) {
        SelectApplication(0);
      }
      return;
    }

    int cols = 1;
    if (applications_list_container) {
      float w = applications_list_container->GetLayout().GetCalculatedWidth();
      cols = std::max(1, (int)((w + 8.0f) / 104.0f));
    }

    if (key == KeyCode::RightArrow) {
      SelectApplication((selected_application_index + 1) % num_apps);
    } else if (key == KeyCode::LeftArrow) {
      SelectApplication((selected_application_index - 1 + num_apps) % num_apps);
    } else if (key == KeyCode::DownArrow) {
      int next_idx = selected_application_index + cols;
      if (next_idx < num_apps) {
        SelectApplication(next_idx);
      } else {
        SelectApplication(num_apps - 1);
      }
    } else if (key == KeyCode::UpArrow) {
      int prev_idx = selected_application_index - cols;
      if (prev_idx >= 0) {
        SelectApplication(prev_idx);
      } else {
        SelectApplication(0);
      }
    } else if (key == KeyCode::Enter) {
      LaunchApplication(selected_application_index);
    }
  });

  applications_tab->OnMouseButtonDown(
      [focusable](const Point&, MouseButton button) {
        if (button == MouseButton::Left) focusable->Focus();
      });

  if (!GetApplications().empty()) {
    SelectApplication(0);
  } else {
    SelectApplication(-1);
  }
  focusable->Focus();

  return applications_tab;
}
