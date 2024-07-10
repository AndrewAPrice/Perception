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
#if 0
#include "applications_tab.h"

#include <iostream>
#include <map>
#include <memory>
#include <string>

#include "applications.h"
#include "launcher_window.h"
#include "perception/scheduler.h"
#include "perception/ui/button.h"
#include "perception/ui/container.h"
#include "perception/ui/parentless_widget.h"
#include "perception/ui/scroll_bar.h"
#include "perception/ui/scroll_container.h"
#include "perception/ui/text_alignment.h"
#include "perception/ui/theme.h"
#include "perception/ui/ui_window.h"
#include "perception/ui/node.h"
#include "permebuf/Libraries/perception/loader.permebuf.h"
#include "tabs.h"

using ::perception::MessageId;
using ::perception::ui::Button;
using ::perception::ui::Container;
using ::perception::ui::kContainerPadding;
using ::perception::ui::Label;
using ::perception::ui::ParentlessWidget;
using ::perception::ui::ScrollBar;
using ::perception::ui::ScrollContainer;
using ::perception::ui::TextAlignment;
using ::perception::ui::UiWindow;
using ::perception::ui::Node;
using LoaderService = ::permebuf::perception::LoaderService;

namespace {

// The width of the selected application panel.
double kSelectedApplicationPanelWidth = 150.0f;

// The widget containing the applications tab.
std::shared_ptr<Widget> applications_tab;

// The selected application title.
std::shared_ptr<Label> selected_application_title;

// The selected application description.
std::shared_ptr<Label> selected_application_description;

// The selected application path.
std::shared_ptr<Label> selected_application_path;

// Launches an application based on its index in the list of applications.
void LaunchApplication(int index) {
  const std::vector<Application>& applications = GetApplications();
  if (index < 0 || index >= applications.size()) return;

  Permebuf<LoaderService::LaunchApplicationRequest> request;
  request->SetName(applications[index].path);

  (void)LoaderService::FindFirstInstance()->CallLaunchApplication(
      std::move(request));
}

// Selects an application based on its index in the list of applications.
void SelectApplication(int index) {
  const std::vector<Application>& applications = GetApplications();
  if (index < 0 || index >= applications.size()) {
    selected_application_title->SetLabel("");
    selected_application_description->SetLabel("");
    selected_application_path->SetLabel("");
  } else {
    const Application& application = applications[index];
    selected_application_title->SetLabel(application.name);
    selected_application_description->SetLabel(application.description);
    selected_application_path->SetLabel(application.path);
  }
}

// Creates a widget representing this application to use as a line in the scrollview.
std::shared_ptr<Widget> CreateApplication(std::string_view application_name,
                                          int index) {
  return std::make_shared<Widget>()
      ->SetWidthPercent(100)
      ->SetFlexDirection(YGFlexDirectionRow)
      ->AddChildren({std::make_shared<Label>()
                         ->SetLabel(application_name)
                         ->SetTextAlignment(TextAlignment::MiddleLeft)
                         ->SetFlexGrow(1.0f)
                         ->ToSharedPtr(),
                     Button::Create()
                         ->SetLabel("Launch")
                         ->OnClick([index] { LaunchApplication(index); })
                         ->ToSharedPtr(),
                     Button::Create()
                         ->SetLabel("About")
                         ->OnClick([index] { SelectApplication(index); })
                         ->ToSharedPtr()})
      ->ToSharedPtr();
}

// Builds the container containing the list of applications.
std::shared_ptr<Widget> BuildApplicationsList() {
  std::shared_ptr<Widget> container = std::make_shared<Widget>();

  std::vector<std::shared_ptr<Widget>> application_widgets;
  const std::vector<Application>& applications = GetApplications();
  for (int i = 0; i < applications.size(); i++)
    application_widgets.push_back(CreateApplication(applications[i].name, i));
  container->AddChildren(application_widgets);
  return container;
}

}  // namespace

// Gets or constructs the applications tab of the launcher.
std::shared_ptr<Widget> GetOrConstructApplicationsTab() {
  if (applications_tab) {
    SelectApplication(-1);
    return applications_tab;
  }

  selected_application_title = std::make_shared<Label>();
  selected_application_title->SetLabel("")
      ->SetTextAlignment(TextAlignment::TopCenter)
      ->SetFlexGrow(1.f)
      ->SetPadding(YGEdgeBottom, kContainerPadding);

  selected_application_description = std::make_shared<Label>();
  selected_application_description->SetLabel("")
      ->SetTextAlignment(TextAlignment::TopCenter)
      ->SetFlexGrow(1.f)
      ->SetPadding(YGEdgeBottom, kContainerPadding);

  selected_application_path = std::make_shared<Label>();

  selected_application_path->SetLabel("")
      ->SetTextAlignment(TextAlignment::TopCenter)
      ->SetFlexGrow(1.f);

  applications_tab =
      Container::Create()
          ->SetBorderRadius(0.0f)
          ->SetBorderWidth(0.0f)
          ->SetBackgroundColor(0x2518E9FF)
          ->SetFlexDirection(YGFlexDirectionRow)
          ->SetFlexGrow(1.0)
          ->SetHeightPercent(100.0f)
          ->AddChildren({ScrollContainer::Create(
                             ParentlessWidget::Create(BuildApplicationsList()),
                             /*show_vertical_scroll_bar=*/true,
                             /*show_horizontal_scroll_bar=*/false)
                             ->SetFlexGrow(1.0)
                             ->SetHeightPercent(100.0f)
                             ->ToSharedPtr(),
                         Container::Create()
                             ->SetBorderRadius(0.0f)
                             ->SetBorderWidth(0.0f)
                             ->SetBackgroundColor(0x1508D9FF)
                             ->SetFlexDirection(YGFlexDirectionColumn)
                             ->SetWidth(kSelectedApplicationPanelWidth)
                             ->SetHeightPercent(100.0f)
                             ->SetHeightPercent(100.0f)
                             ->AddChildren({selected_application_title,
                                            selected_application_description,
                                            selected_application_path})
                             ->ToSharedPtr()})
          ->ToSharedPtr();

  return applications_tab;
}
#endif