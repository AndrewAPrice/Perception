#include <sys/time.h>
#include <time.h>

#include <chrono>
#include <iostream>

#include "include/core/SkFont.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/time.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/font.h"
#include "perception/ui/layout.h"

using ::perception::HandOverControl;
using ::perception::TerminateProcess;
using ::perception::ui::GetBold12UiFont;
using ::perception::ui::Layout;
using ::perception::ui::components::Block;
using ::perception::ui::components::Label;
using ::perception::ui::components::UiWindow;

using ::perception::ui::Node;

std::shared_ptr<Node> clock_label;
std::unique_ptr<SkFont> clock_font;

void UpdateClock() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);

  time_t seconds = tv.tv_sec;
  struct tm* tm_info = gmtime(&seconds);

  char time_str[9];
  sprintf(time_str, "%02d:%02d:%02d", tm_info->tm_hour, tm_info->tm_min,
          tm_info->tm_sec);

  clock_label->Get<Label>()->SetText(time_str);

  // Align updates with the next second boundary.
  long delay_micros = 1000000 - tv.tv_usec;
  if (delay_micros <= 0) delay_micros = 1000000;

  perception::AfterDuration(std::chrono::microseconds(delay_micros),
                            UpdateClock);
}

int main(int argc, char* argv[]) {
  // Create large font for the digital clock
  clock_font = std::make_unique<SkFont>(*GetBold12UiFont());
  clock_font->setSize(36.0f);

  auto window = UiWindow::ResizableWindowWithTitleBar(
      "Clock",
      [](UiWindow& window) {
        window.SetBackgroundColor(0xFF000000);
        window.OnClose([]() { TerminateProcess(); });
      },
      [](Layout& layout) {
        layout.SetWidth(250);
        layout.SetHeight(150);
      },
      Block::SolidColor(
          SkColorSetARGB(0xFF, 0x00, 0x00, 0x00),  // Black background.
          [](Layout& layout) {
            layout.SetAlignSelf(YGAlignStretch);
            layout.SetJustifyContent(YGJustifyCenter);
            layout.SetAlignItems(YGAlignCenter);
            layout.SetFlexGrow(1.0f);
            layout.SetWidthPercent(100);
          },
          Label::BasicLabel(
              "00:00:00",
              [](Label& label) {
                label.SetFont(clock_font.get());
                label.SetColor(
                    SkColorSetARGB(0xFF, 0x00, 0xE5, 0xFF));  // Cyan neon text.
                label.SetTextAlignment(
                    perception::ui::TextAlignment::MiddleCenter);
              },
              [](Layout& layout) { layout.SetAlignSelf(YGAlignStretch); },
              &clock_label)));

  // Start the clock update loop.
  UpdateClock();

  HandOverControl();
  return 0;
}
