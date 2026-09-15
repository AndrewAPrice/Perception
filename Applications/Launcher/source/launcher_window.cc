// Copyright 2021 Google LLC
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

#include "launcher_window.h"

#include <sys/time.h>
#include <time.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>

#include "perception/devices/graphics_device.h"
#include "perception/fibers.h"
#include "perception/loader.h"
#include "perception/power.h"
#include "perception/processes.h"
#include "perception/registry.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "perception/time.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/image_button.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/pop_up.h"
#include "perception/ui/components/tab_bar.h"
#include "perception/ui/components/tooltip.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/image.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/point.h"
#include "perception/ui/text_alignment.h"
#include "perception/ui/theme.h"
#include "processes_tab.h"
#include "tabs.h"

using ::perception::Defer;
using ::perception::GetService;
using ::perception::devices::GraphicsDevice;
using ::perception::ui::Image;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::Point;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::Button;
using ::perception::ui::components::Container;
using ::perception::ui::components::ImageButton;
using ::perception::ui::components::Label;
using ::perception::ui::components::PopUp;
using ::perception::ui::components::PopUpMenu;
using ::perception::ui::components::TabBar;
using ::perception::ui::components::Tooltip;
using ::perception::ui::components::UiWindow;

namespace {

// Path to the power icon asset.
constexpr std::string_view kPowerIconPath = "/Applications/Launcher/power.svg";

// Tooltip text for the power button.
constexpr std::string_view kPowerTooltip = "Power";

// Default application to launch when the clock is clicked.
constexpr std::string_view kDefaultClockProgram = "Clock";

// Time format registry key.
constexpr std::string_view kTimeFormatRegistryKey = "timeFormat";

// Clock program registry key.
constexpr std::string_view kClockProgramRegistryKey = "clockProgram";

// Header text and icon color when the window is focused.
constexpr uint32 kFocusedHeaderColor = 0xFFFFFFFF;

// Header text and icon color when the window is unfocused.
constexpr uint32 kUnfocusedHeaderColor = 0xFF000000;

enum class TimeFormat {
  TWELVE_HOUR = 0,
  TWENTY_FOUR_HOUR = 1,
};

std::shared_ptr<Node> launcher_window;
std::shared_ptr<Node> tab_content_container;
std::shared_ptr<TabBar> launcher_tab_bar;
std::shared_ptr<Node> power_button_node;
std::shared_ptr<Node> time_button_node;
std::shared_ptr<Label> time_label;
std::shared_ptr<Label> perception_label;

TimeFormat time_format = TimeFormat::TWELVE_HOUR;
std::string clock_program = std::string(kDefaultClockProgram);
uint64 time_update_generation = 0;
bool registry_listeners_initialized = false;

// Formats the current time based on the given format.
std::string GetCurrentTimeString(TimeFormat format) {
  struct timeval tv;
  gettimeofday(&tv, nullptr);

  time_t seconds = tv.tv_sec;
  struct tm* tm_info = gmtime(&seconds);
  if (!tm_info) return "";

  char time_str[16];
  if (format == TimeFormat::TWENTY_FOUR_HOUR) {
    snprintf(time_str, sizeof(time_str), "%02d:%02d", tm_info->tm_hour,
             tm_info->tm_min);
  } else {
    int hour = tm_info->tm_hour;
    bool is_pm = hour >= 12;
    hour = hour % 12;
    if (hour == 0) hour = 12;
    snprintf(time_str, sizeof(time_str), "%d:%02d %s", hour, tm_info->tm_min,
             is_pm ? "PM" : "AM");
  }
  return std::string(time_str);
}

enum class WindowState { CLOSED, OPENING, OPEN };
WindowState launcher_window_state = WindowState::CLOSED;
std::vector<::perception::Fiber*> waiting_window_fibers;

// Updates the time label text.
void UpdateTimeDisplay() {
  if (time_label) time_label->SetText(GetCurrentTimeString(time_format));
  if (time_button_node) time_button_node->Invalidate();
  if (launcher_window) launcher_window->Invalidate();
}

// Updates header text and icon colors based on window focus state.
void UpdateHeaderColors() {
  if (!launcher_window) return;
  auto ui_window = launcher_window->Get<UiWindow>();
  if (!ui_window) return;

  uint32 color =
      ui_window->IsFocused() ? kFocusedHeaderColor : kUnfocusedHeaderColor;
  if (time_button_node) {
    if (auto button = time_button_node->Get<Button>())
      button->SetLabelColor(color);
  }
  if (time_label) time_label->SetColor(color);
  if (perception_label) perception_label->SetColor(color);
  if (power_button_node) {
    if (auto image_button = power_button_node->Get<ImageButton>())
      image_button->SetColor(color);
  }
}

// Schedules the next time update aligned to the minute boundary.
void ScheduleTimeUpdate(uint64 generation) {
  struct timeval tv;
  gettimeofday(&tv, nullptr);

  long delay_micros = (60 - (tv.tv_sec % 60)) * 1000000 - tv.tv_usec;
  if (delay_micros <= 0) delay_micros = 1000000;

  ::perception::AfterDuration(std::chrono::microseconds(delay_micros),
                              [generation]() {
                                if (generation != time_update_generation)
                                  return;
                                if (launcher_window_state != WindowState::OPEN)
                                  return;
                                UpdateTimeDisplay();
                                ScheduleTimeUpdate(generation);
                              });
}

// Launches the configured clock program.
void LaunchClockProgram() {
  ::perception::LoadApplicationRequest request;
  request.name = clock_program;
  GetService<::perception::Loader>().LaunchApplication(
      request, [](StatusOr<::perception::LoadApplicationResponse> response) {
        if (!response.Ok())
          std::cout << "Failed to launch clock program: "
                    << (int)response.Status() << std::endl;
      });
}

// Reads the current time format from the registry.
void UpdateTimeFormatFromRegistry() {
  auto time_format_val =
      ::perception::GetRegistryValue(::perception::RegistryCorpus::APPLICATIONS,
                                     "Launcher", kTimeFormatRegistryKey);
  if (!time_format_val.Ok()) {
    time_format = TimeFormat::TWELVE_HOUR;
    return;
  }

  if (time_format_val->GetType() ==
      ::perception::serialization::Value::Type::INTEGER) {
    auto int_val = time_format_val->IntegerValue();
    time_format = (int_val && *int_val == 1) ? TimeFormat::TWENTY_FOUR_HOUR
                                             : TimeFormat::TWELVE_HOUR;
  } else if (time_format_val->GetType() ==
             ::perception::serialization::Value::Type::FLOAT) {
    auto float_val = time_format_val->FloatValue();
    time_format = (float_val && *float_val == 1.0)
                      ? TimeFormat::TWENTY_FOUR_HOUR
                      : TimeFormat::TWELVE_HOUR;
  } else if (time_format_val->GetType() ==
             ::perception::serialization::Value::Type::STRING) {
    auto str_val = time_format_val->StringValue();
    time_format = (str_val && (*str_val == "24-hour" || *str_val == "24" ||
                               *str_val == "1"))
                      ? TimeFormat::TWENTY_FOUR_HOUR
                      : TimeFormat::TWELVE_HOUR;
  } else {
    time_format = TimeFormat::TWELVE_HOUR;
  }
}

// Reads the current clock program from the registry.
void UpdateClockProgramFromRegistry() {
  auto clock_prog_val =
      ::perception::GetRegistryValue(::perception::RegistryCorpus::APPLICATIONS,
                                     "Launcher", kClockProgramRegistryKey);
  if (clock_prog_val.Ok() &&
      clock_prog_val->GetType() ==
          ::perception::serialization::Value::Type::STRING) {
    auto str_val = clock_prog_val->StringValue();
    if (str_val && !str_val->empty()) clock_program = std::string(*str_val);
  }
}

// Reads the current settings from the registry.
void UpdateSettingsFromRegistry() {
  UpdateTimeFormatFromRegistry();
  UpdateClockProgramFromRegistry();
}

// Initializes registry listeners for settings changes.
void InitializeRegistryListeners() {
  if (registry_listeners_initialized) return;
  registry_listeners_initialized = true;

  (void)::perception::RegisterRegistryListener(
      ::perception::RegistryCorpus::APPLICATIONS, "Launcher",
      kTimeFormatRegistryKey, []() {
        UpdateTimeFormatFromRegistry();
        if (launcher_window_state == WindowState::OPEN) UpdateTimeDisplay();
      });

  (void)::perception::RegisterRegistryListener(
      ::perception::RegistryCorpus::APPLICATIONS, "Launcher",
      kClockProgramRegistryKey, []() { UpdateClockProgramFromRegistry(); });
}

// Returns the power symbol image, loading it on first access.
std::shared_ptr<Image> GetPowerImage() {
  static auto power_img = Image::LoadImage(kPowerIconPath);
  return power_img;
}

// The current tab that is showing.
std::optional<Tab> current_tab;

}  // namespace

void SwitchToTab(Tab tab) {
  std::cout << "SwitchToTab called. tab = " << (int)tab << std::endl;
  if (current_tab && *current_tab == tab) {
    std::cout << "Already on this tab." << std::endl;
    return;
  }

  if (current_tab && *current_tab == Tab::PROCESSES)
    SetProcessesTabVisible(false);

  current_tab = tab;
  tab_content_container->RemoveChildren();
  auto tab_contents = GetOrConstructTabContents(tab);
  tab_content_container->AddChild(tab_contents);

  if (launcher_tab_bar) {
    int expected_index = (tab == Tab::APPLICATIONS ? 0 : 1);
    if (launcher_tab_bar->GetSelectedTab() != expected_index)
      launcher_tab_bar->SelectTab(expected_index);
  }

  if (tab == Tab::PROCESSES) SetProcessesTabVisible(true);
}

void ShowLauncherWindow() {
  if (launcher_window_state == WindowState::OPEN) {
    // Launcher window is already open.
    if (launcher_window) launcher_window->Get<UiWindow>()->Focus();
    return;
  }

  if (launcher_window_state == WindowState::OPENING) {
    waiting_window_fibers.push_back(::perception::GetCurrentlyExecutingFiber());
    ::perception::Sleep();
    if (launcher_window) launcher_window->Get<UiWindow>()->Focus();
    return;
  }

  launcher_window_state = WindowState::OPENING;

  // Initialize registry listeners and settings.
  InitializeRegistryListeners();
  UpdateSettingsFromRegistry();

  // Query the screen size.
  auto screen_size = GetService<GraphicsDevice>().GetScreenSize();
  if (!screen_size.Ok()) {
    launcher_window_state = WindowState::CLOSED;
    for (auto fiber : waiting_window_fibers) fiber->WakeUp();
    waiting_window_fibers.clear();
    return;
  }

  // Create the launcher that's 80% of the screen size.
  int launcher_width = screen_size->width * 8 / 10;
  int launcher_height = screen_size->height * 8 / 10;

  // Pre-load the power icon before constructing the window to avoid yielding
  // during window layout initialization.
  auto power_image = GetPowerImage();

  launcher_window = UiWindow::ResizableWindowWithTabBar(
      &launcher_tab_bar,
      [launcher_width, launcher_height](Layout& layout) {
        layout.SetWidth((float)launcher_width);
        layout.SetHeight((float)launcher_height);
      },
      [](UiWindow& window) {
        window.SetTitle("Launcher");
        window.OnClose([]() {
          Defer([]() {
            if (current_tab && *current_tab == Tab::PROCESSES)
              SetProcessesTabVisible(false);

            time_update_generation++;
            launcher_window.reset();
            tab_content_container.reset();
            launcher_tab_bar.reset();
            power_button_node.reset();
            time_button_node.reset();
            time_label.reset();
            perception_label.reset();
            launcher_window_state = WindowState::CLOSED;
          });
        });
        window.OnFocusChanged([]() { UpdateHeaderColors(); });
      },
      Node::Empty(&tab_content_container, [](Layout& layout) {
        layout.SetFlexDirection(YGFlexDirectionRow);
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetMinHeight(0.0f);
        layout.SetMargin(YGEdgeAll, -8.0f);
        layout.SetMargin(YGEdgeTop, 0.0f);
      }));

  launcher_tab_bar->SetPrefixNode(Label::BasicLabel(
      "Perception", &perception_label,
      [](Layout& layout) { layout.SetMargin(YGEdgeHorizontal, 8.0f); }));
  launcher_tab_bar->AddTab("Applications");
  launcher_tab_bar->AddTab("Running processes");
  launcher_tab_bar->OnTabSelected([](int index) {
    Tab tab = (index == 0 ? Tab::APPLICATIONS : Tab::PROCESSES);
    if (!current_tab || *current_tab != tab) SwitchToTab(tab);
  });

  time_button_node = Button::BasicButton(
      []() { LaunchClockProgram(); },
      [](Button& button) { button.SetButtonStyle(Button::ButtonStyle::GHOST); },
      [](Layout& layout) {
        layout.SetHeight(::perception::ui::kImageButtonHeight);
        layout.SetMinHeight(0.0f);
        layout.SetMinWidth(0.0f);
      },
      Label::BasicLabel(GetCurrentTimeString(time_format), &time_label));

  power_button_node = ImageButton::BasicImageButton(
      []() {
        if (!power_button_node) return;
        Point pos = power_button_node->GetAbsolutePosition();
        float height = power_button_node->GetSize().height;
        auto menu = PopUpMenu::Container(
            PopUpMenu::DropDownItem("Power Off",
                                    []() { ::perception::power::PowerOff(); }),
            PopUpMenu::DropDownItem("Sleep",
                                    []() { ::perception::power::Sleep(); }),
            PopUpMenu::DropDownItem("Reset",
                                    []() { ::perception::power::Restart(); }));
        PopUp::Show(power_button_node, Point{pos.x, pos.y + height}, menu);
      },
      power_image, Tooltip::ShowTooltip(kPowerTooltip));

  UpdateHeaderColors();

  auto suffix_container = Container::HorizontalContainer(
      [](Layout& layout) { layout.SetAlignItems(YGAlignCenter); },
      time_button_node, power_button_node);
  launcher_tab_bar->SetSuffixNode(suffix_container);

  current_tab = std::nullopt;
  SwitchToTab(Tab::APPLICATIONS);

  launcher_window_state = WindowState::OPEN;
  time_update_generation++;
  ScheduleTimeUpdate(time_update_generation);

  for (auto fiber : waiting_window_fibers) fiber->WakeUp();
  waiting_window_fibers.clear();
}