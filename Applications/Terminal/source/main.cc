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

#include <memory>
#include <string>
#include <string_view>

#include "perception/loader.h"
#include "perception/processes.h"
#include "perception/registry.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "perception/shared_memory_pipe.h"
#include "perception/terminal_service.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/scroll_bar.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "status.h"
#include "terminal_buffer.h"
#include "terminal_widget.h"

namespace {

// Default window title for the Terminal application.
constexpr std::string_view kDefaultWindowTitle = "Terminal";

// Message displayed when the terminal is opened without a program argument.
constexpr std::string_view kNoProgramMessage =
    "No program to display in the terminal.\r\n";

// Registry key storing the active terminal color theme.
constexpr std::string_view kThemeRegistryKey = "theme";

// Buffer size in bytes when draining child stdout/stderr from SharedMemoryPipe.
constexpr size_t kReadChunkSize = 4096;

// Default POSIX termios c_iflag (ICRNL).
constexpr uint32 kDefaultTermiosIflag = 0x0100u;

// Default POSIX termios c_oflag (OPOST | ONLCR).
constexpr uint32 kDefaultTermiosOflag = 0x0005u;

// Default POSIX termios c_cflag (CS8 | CREAD).
constexpr uint32 kDefaultTermiosCflag = 0x00BFu;

// Default POSIX termios c_lflag (ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN).
constexpr uint32 kDefaultTermiosLflag = 0x8A3Bu;

// Reads the configured terminal theme from the Perception registry.
TerminalTheme ReadThemeFromRegistry() {
  auto val_or = ::perception::GetRegistryValue(
      ::perception::RegistryCorpus::APPLICATIONS, "Terminal",
      kThemeRegistryKey);
  if (!val_or.Ok())
    return TerminalTheme::Dark;

  if (val_or->GetType() == ::perception::serialization::Value::Type::INTEGER) {
    auto int_val = val_or->IntegerValue();
    return (int_val && *int_val == 1) ? TerminalTheme::Light
                                      : TerminalTheme::Dark;
  }
  if (val_or->GetType() == ::perception::serialization::Value::Type::FLOAT) {
    auto float_val = val_or->FloatValue();
    return (float_val && *float_val == 1.0) ? TerminalTheme::Light
                                            : TerminalTheme::Dark;
  }
  if (val_or->GetType() == ::perception::serialization::Value::Type::STRING) {
    auto str_val = val_or->StringValue();
    if (str_val && (*str_val == "Light" || *str_val == "light" ||
                    *str_val == "1"))
      return TerminalTheme::Light;
  }
  return TerminalTheme::Dark;
}

// Server implementation of perception.TerminalService for a single terminal window.
class TerminalServiceImpl : public ::perception::TerminalService::Server {
 public:
  TerminalServiceImpl() {
    window_size_.rows = static_cast<uint16>(kDefaultTerminalRows);
    window_size_.cols = static_cast<uint16>(kDefaultTerminalCols);
    window_size_.width_pixels = static_cast<uint16>(kDefaultTerminalCols * 8);
    window_size_.height_pixels = static_cast<uint16>(kDefaultTerminalRows * 16);

    attributes_.c_iflag = kDefaultTermiosIflag;
    attributes_.c_oflag = kDefaultTermiosOflag;
    attributes_.c_cflag = kDefaultTermiosCflag;
    attributes_.c_lflag = kDefaultTermiosLflag;
  }

  StatusOr<::perception::TerminalWindowSize> GetWindowSize()
      override {
    return window_size_;
  }

  Status SetWindowSize(
      const ::perception::TerminalWindowSize& size) override {
    window_size_ = size;
    return Status::OK;
  }

  StatusOr<::perception::TerminalAttributes> GetAttributes()
      override {
    return attributes_;
  }

  Status SetAttributes(
      const ::perception::TerminalAttributes& attributes) override {
    attributes_ = attributes;
    return Status::OK;
  }

  void UpdateWindowSize(int rows, int cols, int width_px, int height_px) {
    window_size_.rows = static_cast<uint16>(std::max(1, rows));
    window_size_.cols = static_cast<uint16>(std::max(1, cols));
    window_size_.width_pixels = static_cast<uint16>(std::max(0, width_px));
    window_size_.height_pixels = static_cast<uint16>(std::max(0, height_px));
  }

  const ::perception::TerminalAttributes& CurrentAttributes() const {
    return attributes_;
  }

 private:
  ::perception::TerminalWindowSize window_size_;
  ::perception::TerminalAttributes attributes_;
};

}  // namespace

using ::perception::GetService;
using ::perception::HandOverControl;
using ::perception::LoadApplicationRequest;
using ::perception::Loader;
using ::perception::NotifyUponProcessTermination;
using ::perception::ProcessId;
using ::perception::SharedMemoryPipe;
using ::perception::TerminateProcess;
using ::perception::TerminateProcesss;
using ::perception::TerminalService;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::components::Container;
using ::perception::ui::components::ScrollBar;
using ::perception::ui::components::UiWindow;

int main(int argc, char* argv[]) {
  auto terminal_service = std::make_shared<TerminalServiceImpl>();
  TerminalService::Client service_client(*terminal_service);

  auto stdin_pipe = SharedMemoryPipe::Create();
  auto output_pipe = SharedMemoryPipe::Create();
  if (stdin_pipe) {
    stdin_pipe->SetTerminalService(service_client);
    stdin_pipe->AddWriter();
  }
  if (output_pipe) {
    output_pipe->SetTerminalService(service_client);
    output_pipe->AddReader();
  }

  std::shared_ptr<UiWindow> ui_window;
  std::shared_ptr<ScrollBar> scroll_bar;
  std::shared_ptr<TerminalWidget> terminal_widget;

  auto scroll_bar_node = ScrollBar::VerticalScrollBar(&scroll_bar);
  auto terminal_node = TerminalWidget::Create(scroll_bar, &terminal_widget);

  if (terminal_widget)
    terminal_widget->ApplyTheme(ReadThemeFromRegistry());

  auto window_node = UiWindow::ResizableWindowWithTitleBar(
      kDefaultWindowTitle, &ui_window,
      [&terminal_widget](UiWindow& win) {
        uint32 bg = terminal_widget
                        ? terminal_widget->GetDefaultBackgroundColor()
                        : kDefaultTerminalBackgroundColor;
        win.SetBackgroundColor(bg);
      },
      [](Layout& layout) { layout.SetGap(0.0f); },
      Container::HorizontalContainer(
          [](Layout& layout) {
            layout.SetFlexGrow(1.0f);
            layout.SetFlexShrink(1.0f);
            layout.SetMinWidth(0.0f);
            layout.SetMinHeight(0.0f);
            layout.SetGap(0.0f);
            for (auto edge : {YGEdgeLeft, YGEdgeRight, YGEdgeBottom})
              layout.SetMargin(edge, ::perception::ui::kTitleBarNegativeMargin);
          },
          terminal_node, scroll_bar_node));

  if (terminal_widget) {
    terminal_service->UpdateWindowSize(
        terminal_widget->GetRows(), terminal_widget->GetCols(),
        static_cast<int>(terminal_widget->GetCols() *
                         terminal_widget->GetCellWidth()),
        static_cast<int>(terminal_widget->GetRows() *
                         terminal_widget->GetCellHeight()));

    terminal_widget->SetSendInputCallback(
        [stdin_pipe](std::string_view input_bytes) {
          if (stdin_pipe && !input_bytes.empty())
            stdin_pipe->Write(input_bytes.data(), input_bytes.size(), true);
        });

    terminal_widget->SetOnTitleChangedCallback(
        [&ui_window](std::string_view title) {
          if (ui_window)
            ui_window->SetTitle(title.empty() ? kDefaultWindowTitle : title);
        });

    terminal_widget->SetOnBackgroundColorChangedCallback(
        [&ui_window](uint32 color) {
          if (ui_window)
            ui_window->SetBackgroundColor(color);
        });

    terminal_widget->SetOnResizeCallback(
        [terminal_service](int rows, int cols, int width_px, int height_px) {
          terminal_service->UpdateWindowSize(rows, cols, width_px, height_px);
        });

    terminal_widget->SetOnCursorChangedCallback(
        [&ui_window](::perception::window::Cursor cursor) {
          if (ui_window && ui_window->GetBaseWindow())
            ui_window->GetBaseWindow()->SetCursor(cursor);
        });

    terminal_widget->SetTerminalAttributesGetter([terminal_service]() {
      return terminal_service->CurrentAttributes();
    });

    (void)::perception::RegisterRegistryListener(
        ::perception::RegistryCorpus::APPLICATIONS, "Terminal",
        kThemeRegistryKey, [&terminal_widget]() {
          if (terminal_widget)
            terminal_widget->ApplyTheme(ReadThemeFromRegistry());
        });
  }

  if (ui_window) {
    ui_window->SetFocusedNode(terminal_node);
    ui_window->OnFocusChanged([&ui_window, &terminal_widget, terminal_node]() {
      if (ui_window && terminal_widget) {
        if (ui_window->IsFocused() && !ui_window->GetFocusedNode())
          ui_window->SetFocusedNode(terminal_node);
        terminal_widget->NotifyWindowFocusChanged(ui_window->IsFocused());
      }
    });
  }

  // Pump output bytes from the child process into the terminal widget.
  std::shared_ptr<std::function<void()>> pump_output =
      std::make_shared<std::function<void()>>();
  *pump_output = [output_pipe, &terminal_widget, pump_output]() {
    if (!output_pipe || !terminal_widget)
      return;
    char buffer[kReadChunkSize];
    while (true) {
      long bytes_read = output_pipe->Read(buffer, sizeof(buffer), true);
      if (bytes_read > 0) {
        terminal_widget->FeedOutput(
            std::string_view(buffer, static_cast<size_t>(bytes_read)));
      } else {
        break;
      }
    }
    if (!output_pipe->IsEof())
      output_pipe->OnDataAvailable(*pump_output);
  };

  if (output_pipe)
    output_pipe->OnDataAvailable(*pump_output);

  // Launch the specified command or display a message if no program is given.
  ProcessId child_pid = 0;
  bool child_running = false;
  bool has_program = (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0');
  if (has_program) {
    LoadApplicationRequest launch_req;
    launch_req.name = argv[1];
    for (int i = 2; i < argc; ++i) {
      if (argv[i] != nullptr)
        launch_req.arguments.emplace_back(argv[i]);
    }
    launch_req.create_as_child = true;
    launch_req.stdin_pipe = stdin_pipe;
    launch_req.stdout_pipe = output_pipe;
    launch_req.stderr_pipe = output_pipe;

    auto launch_status = GetService<Loader>().LaunchApplication(launch_req);
    if (launch_status.Ok()) {
      child_pid = launch_status->process;
      child_running = true;
      NotifyUponProcessTermination(
          child_pid, [&child_running, pump_output, stdin_pipe, output_pipe]() {
            child_running = false;
            if (pump_output && *pump_output)
              (*pump_output)();
            if (stdin_pipe)
              stdin_pipe->CloseWriter();
            if (output_pipe)
              output_pipe->CloseReader();
            TerminateProcess();
          });
    } else if (terminal_widget) {
      std::string err_msg =
          "\x1b[1;31mFailed to launch " + launch_req.name + ".\x1b[0m\r\n";
      terminal_widget->FeedOutput(err_msg);
    }
  } else if (terminal_widget) {
    terminal_widget->FeedOutput(kNoProgramMessage);
  }

  if (ui_window) {
    ui_window->OnClose([&child_running, &child_pid, stdin_pipe, output_pipe]() {
      if (stdin_pipe)
        stdin_pipe->CloseWriter();
      if (output_pipe)
        output_pipe->CloseReader();
      if (child_running && child_pid != 0)
        TerminateProcesss(child_pid);
      TerminateProcess();
    });
  }

  HandOverControl();
  return 0;
}
