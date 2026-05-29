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
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/image_view.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/scroll_container.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/font.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/text_alignment.h"
#include "perception/ui/theme.h"
#include "tabs.h"

using ::perception::GetService;
using ::perception::LoadApplicationRequest;
using ::perception::Loader;
using ::perception::ui::GetBold12UiFont;
using ::perception::ui::kBackgroundWindowColor;
using ::perception::ui::kContainerPadding;
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
using ::perception::ui::components::ImageView;
using ::perception::ui::components::Label;
using ::perception::ui::components::ScrollContainer;
using ::perception::ui::components::UiWindow;
using ::perception::window::MouseButton;

namespace {

// The width of the selected application panel.
double kSelectedApplicationPanelWidth = 280.0f;

// The overlay node.
std::shared_ptr<Node> launcher_overlay;

// Active error dialog window nodes to keep them alive.
std::vector<std::shared_ptr<Node>> active_error_dialogs;

struct ErrorDialogState {
  std::weak_ptr<Node> dialog_node;
};

// Returns an error message when an application fails to load.
std::string ErrorMessageForApplicationLoading(std::string_view request_name,
                                              ::perception::Status status) {
  bool is_directory = !request_name.empty() && request_name.back() == '/';
  std::string error_message =
      is_directory ? "Failed to open " : "Failed to launch ";
  error_message += std::string(request_name);

  switch (status) {
    case ::perception::Status::FILE_NOT_FOUND:
      error_message +=
          is_directory ? "\nDirectory not found." : "\nFile not found.";
      break;
    case ::perception::Status::NOT_ALLOWED:
      error_message += "\nPermission denied.";
      break;
    case ::perception::Status::OUT_OF_MEMORY:
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
                                          ::perception::Status status) {
  auto state = std::make_shared<ErrorDialogState>();
  std::string message = ErrorMessageForApplicationLoading(request_name, status);

  auto main_container = Container::VerticalContainer(
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
              })));

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
      main_container, &state->dialog_node);

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

// The index of the currently selected application.
int selected_application_index = -1;

// Launches an application.
void LaunchApplication(const LoadApplicationRequest& request) {
  ShowOverlay();
  GetService<Loader>().LaunchApplication(
      request,
      [request](::StatusOr<::perception::LoadApplicationResponse> response) {
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
  selected_application_index = index;
  const std::vector<Application>& applications = GetApplications();

  selected_application_icon->RemoveChildren();

  if (index < 0 || index >= applications.size()) {
    selected_application_title->Get<Label>()->SetText("");
    selected_application_description->Get<Label>()->SetText("");
    selected_application_path->Get<Label>()->SetText("");
  } else {
    const Application& application = applications[index];
    selected_application_title->Get<Label>()->SetText(application.name);
    selected_application_description->Get<Label>()->SetText(
        application.description);
    selected_application_path->Get<Label>()->SetText(application.path);
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
}

// Creates a widget representing this application to use as a line in the
// scrollview.
std::shared_ptr<Node> CreateApplicationRow(const Application& application,
                                           int index) {
  auto row = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetAlignItems(YGAlignCenter);
        layout.SetPadding(YGEdgeHorizontal, 12.0f);
        layout.SetPadding(YGEdgeVertical, 8.0f);
      },
      [](Block& block) {
        block.SetBorderRadius(8.0f);
        block.SetFillColor(0);  // Transparent initially
      });

  std::shared_ptr<Node> icon_view;
  if (application.icon) {
    icon_view = ImageView::BasicImage(
        application.icon,
        [](Layout& layout) {
          layout.SetWidth(32.0f);
          layout.SetHeight(32.0f);
          layout.SetMargin(YGEdgeRight, 12.0f);
        },
        [](ImageView& image_view) {
          image_view.SetAlignment(TextAlignment::MiddleCenter);
          image_view.SetResizeMethod(ResizeMethod::Contain);
        });
  } else {
    icon_view = CreateApplicationIcon(application, 32.0f, 6.0f);
    icon_view->GetLayout().SetMargin(YGEdgeRight, 12.0f);
  }

  auto label = Label::BasicLabel(
      application.name, [](Layout& layout) { layout.SetFlexGrow(1.0f); },
      [](Label& label) {
        label.SetTextAlignment(TextAlignment::MiddleLeft);
        label.SetColor(kLabelTextColor);
      });

  row->AddChild(icon_view);
  row->AddChild(label);

  row->OnMouseHover([row, index](const Point& point) {
    uint32 hover_color = SkColorSetARGB(0xFF, 0xF0, 0xF0, 0xF0);
    if (row->Get<Block>()->GetFillColor() != hover_color) {
      row->Get<Block>()->SetFillColor(hover_color);
      row->Invalidate();
    }
    SelectApplication(index);
  });

  row->OnMouseLeave([row]() {
    row->Get<Block>()->SetFillColor(0);
    row->Invalidate();
  });

  row->OnMouseButtonDown([index](const Point& point, MouseButton button) {
    if (button == MouseButton::Left) {
      LaunchApplication(index);
    }
  });

  return row;
}

// Builds the container containing the list of applications.
std::shared_ptr<Node> BuildApplicationsList() {
  auto container = Container::VerticalContainer([](Layout& layout) {
    layout.SetWidthPercent(100.0f);
    layout.SetGap(6.0f);
    layout.SetPadding(YGEdgeVertical, 12.0f);
  });

  std::vector<std::shared_ptr<Node>> application_widgets;
  const std::vector<Application>& applications = GetApplications();
  for (int i = 0; i < applications.size(); i++) {
    application_widgets.push_back(CreateApplicationRow(applications[i], i));
  }
  container->AddChildren(application_widgets);
  return container;
}

}  // namespace

// Gets or constructs the applications tab of the launcher.
std::shared_ptr<Node> GetOrConstructApplicationsTab() {
  if (applications_tab) {
    if (!GetApplications().empty()) {
      SelectApplication(0);
    } else {
      SelectApplication(-1);
    }
    return applications_tab;
  }

  selected_application_icon = Node::Empty([](Layout& layout) {
    layout.SetWidth(80.0f);
    layout.SetHeight(80.0f);
    layout.SetMargin(YGEdgeBottom, 20.0f);
  });

  selected_application_title = Label::BasicLabel(
      "", [](Layout& layout) { layout.SetMargin(YGEdgeBottom, 8.0f); },
      [](Label& label) {
        label.SetTextAlignment(TextAlignment::MiddleCenter);
        label.SetColor(kLabelTextColor);
        label.SetFont(GetBold12UiFont());
      });

  selected_application_description = Label::BasicLabel(
      "",
      [](Layout& layout) {
        layout.SetMargin(YGEdgeBottom, 16.0f);
        layout.SetMaxWidth(240.0f);
      },
      [](Label& label) {
        label.SetTextAlignment(TextAlignment::MiddleCenter);
        label.SetColor(SkColorSetARGB(0xFF, 0x55, 0x55, 0x55));
      });

  selected_application_path = Label::BasicLabel(
      "",
      [](Layout& layout) {
        layout.SetMargin(YGEdgeBottom, 24.0f);
        layout.SetMaxWidth(240.0f);
      },
      [](Label& label) {
        label.SetTextAlignment(TextAlignment::MiddleCenter);
        label.SetColor(SkColorSetARGB(0xFF, 0x77, 0x77, 0x77));
      });

  auto launch_button = Button::TextButton(
      "Launch", []() { LaunchApplication(selected_application_index); },
      [](Layout& layout) {
        layout.SetWidth(180.0f);
        layout.SetHeight(32.0f);
      },
      [](Button& button) {
        button.SetIdleColor(SkColorSetARGB(0xFF, 0x4F, 0x46, 0xE5));
        button.SetHoverColor(SkColorSetARGB(0xFF, 0x63, 0x66, 0xF1));
        button.SetPushedColor(SkColorSetARGB(0xFF, 0x43, 0x38, 0xCA));
        button.SetLabelColor(SkColorSetARGB(0xFF, 0xFF, 0xFF, 0xFF));
      });

  auto open_folder_button = Button::TextButton(
      "Open containing folder",
      []() { OpenContainingFolder(selected_application_index); },
      [](Layout& layout) {
        layout.SetWidth(180.0f);
        layout.SetHeight(32.0f);
        layout.SetMargin(YGEdgeTop, 8.0f);
      },
      [](Button& button) {
        button.SetIdleColor(SkColorSetARGB(0xFF, 0x4B, 0x55, 0x63));
        button.SetHoverColor(SkColorSetARGB(0xFF, 0x6B, 0x72, 0x80));
        button.SetPushedColor(SkColorSetARGB(0xFF, 0x37, 0x41, 0x51));
        button.SetLabelColor(SkColorSetARGB(0xFF, 0xFF, 0xFF, 0xFF));
      });

  auto inner_tab = Container::HorizontalContainer(
      [](Block& block) {
        block.SetBorderRadius(0.0f);
        block.SetBorderWidth(0.0f);
        block.SetFillColor(kBackgroundWindowColor);
      },
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetHeightPercent(100.0f);
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
            layout.SetHeightPercent(100.0f);
          }),
      Container::VerticalContainer(
          [](Layout& layout) {
            layout.SetWidth(kSelectedApplicationPanelWidth);
            layout.SetHeightPercent(100.0f);
            layout.SetAlignItems(YGAlignCenter);
            layout.SetJustifyContent(YGJustifyCenter);
            layout.SetPadding(YGEdgeAll, 20.0f);
          },
          selected_application_icon, selected_application_title,
          selected_application_description, selected_application_path,
          launch_button, open_folder_button));

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
        layout.SetHeightPercent(100.0f);
      },
      inner_tab, launcher_overlay);

  if (!GetApplications().empty()) {
    SelectApplication(0);
  } else {
    SelectApplication(-1);
  }

  return applications_tab;
}
