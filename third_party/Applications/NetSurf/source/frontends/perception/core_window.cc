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

#include "core_window.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

extern "C" {
#include "netsurf/keypress.h"
#include "utils/nsoption.h"
}

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "perception/scheduler.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/focusable.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/keyboard.h"
#include "perception/ui/layout.h"
#include "perception/ui/point.h"
#include "plotters.h"
#include "window.h"

namespace {

// Fallback viewport width in pixels before layout has completed.
constexpr int kDefaultViewportWidth = 600;

// Fallback viewport height in pixels before layout has completed.
constexpr int kDefaultViewportHeight = 400;

// Minimum movement in pixels before a mouse press becomes a drag.
constexpr int kDragThresholdPixels = 4;

// Maximum interval in milliseconds between clicks for a double-click.
constexpr int kDoubleClickTimeMs = 450;

// Maximum distance in pixels between clicks for a double-click.
constexpr int kDoubleClickDistancePixels = 6;

// Background fill color for core windows (opaque white).
constexpr SkColor kCoreWindowBackgroundColor = 0xFFFFFFFF;

nserror CoreWindowInvalidate(struct core_window* cw, const struct rect* r) {
  if (cw != nullptr && cw->host != nullptr)
    cw->host->InvalidateArea(r);
  return NSERROR_OK;
}

nserror CoreWindowSetExtent(struct core_window* cw, int width, int height) {
  if (cw != nullptr && cw->host != nullptr)
    cw->host->SetExtent(width, height);
  return NSERROR_OK;
}

nserror CoreWindowSetScroll(struct core_window* cw, int x, int y) {
  if (cw != nullptr && cw->host != nullptr)
    cw->host->SetScroll(x, y);
  return NSERROR_OK;
}

nserror CoreWindowGetScroll(const struct core_window* cw, int* x, int* y) {
  if (cw != nullptr && cw->host != nullptr) {
    cw->host->GetScroll(x, y);
    return NSERROR_OK;
  }
  if (x != nullptr)
    *x = 0;
  if (y != nullptr)
    *y = 0;
  return NSERROR_OK;
}

nserror CoreWindowGetDimensions(const struct core_window* cw, int* width,
                                int* height) {
  if (cw != nullptr && cw->host != nullptr) {
    cw->host->GetDimensions(width, height);
    return NSERROR_OK;
  }
  if (width != nullptr)
    *width = kDefaultViewportWidth;
  if (height != nullptr)
    *height = kDefaultViewportHeight;
  return NSERROR_OK;
}

nserror CoreWindowDragStatus(struct core_window* cw,
                             core_window_drag_status ds) {
  if (cw != nullptr && cw->host != nullptr)
    cw->host->SetDragStatus(ds);
  return NSERROR_OK;
}

}  // namespace

namespace netsurf {
namespace perception {

using ::perception::ui::DrawContext;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::Point;
using ::perception::ui::components::Container;
using ::perception::ui::components::Focusable;
using ::perception::ui::components::ScrollBar;

struct core_window_table perception_core_window_table = {
    .invalidate = CoreWindowInvalidate,
    .set_extent = CoreWindowSetExtent,
    .set_scroll = CoreWindowSetScroll,
    .get_scroll = CoreWindowGetScroll,
    .get_dimensions = CoreWindowGetDimensions,
    .drag_status = CoreWindowDragStatus,
};

std::shared_ptr<CoreWindowHost> CoreWindowHost::Create(DrawCallback on_draw,
                                                       MouseCallback on_mouse,
                                                       KeyCallback on_key) {
  auto host = std::shared_ptr<CoreWindowHost>(new CoreWindowHost(
      std::move(on_draw), std::move(on_mouse), std::move(on_key)));
  host->BuildUi();
  return host;
}

CoreWindowHost::CoreWindowHost(DrawCallback on_draw, MouseCallback on_mouse,
                               KeyCallback on_key)
    : on_draw_(std::move(on_draw)),
      on_mouse_(std::move(on_mouse)),
      on_key_(std::move(on_key)),
      is_alive_(std::make_shared<bool>(true)) {
  core_window_wrapper_.host = this;
}

CoreWindowHost::~CoreWindowHost() {
  if (is_alive_)
    *is_alive_ = false;
  core_window_wrapper_.host = nullptr;
}

struct core_window* CoreWindowHost::GetCoreWindow() {
  return &core_window_wrapper_;
}

std::shared_ptr<Node> CoreWindowHost::GetRootNode() const { return root_node_; }

std::shared_ptr<Node> CoreWindowHost::GetCanvasNode() const {
  return canvas_node_;
}

int CoreWindowHost::GetContentWidth() const { return content_width_; }

int CoreWindowHost::GetContentHeight() const { return content_height_; }

void CoreWindowHost::InvalidateArea(const struct rect* r) {
  (void)r;
  if (canvas_node_)
    canvas_node_->Invalidate();
}

void CoreWindowHost::SetExtent(int width, int height) {
  if (width >= 0)
    content_width_ = width;
  if (height >= 0)
    content_height_ = height;
  ScheduleScrollBarUpdate();
  if (canvas_node_)
    canvas_node_->Invalidate();
}

void CoreWindowHost::SetScroll(int x, int y) {
  int viewport_w = kDefaultViewportWidth;
  int viewport_h = kDefaultViewportHeight;
  GetDimensions(&viewport_w, &viewport_h);

  int max_x = std::max(0, content_width_ - viewport_w);
  int max_y = std::max(0, content_height_ - viewport_h);
  scroll_x_ = std::clamp(x, 0, max_x);
  scroll_y_ = std::clamp(y, 0, max_y);

  ScheduleScrollBarUpdate();
  if (canvas_node_)
    canvas_node_->Invalidate();
}

void CoreWindowHost::GetScroll(int* x, int* y) const {
  if (x != nullptr)
    *x = scroll_x_;
  if (y != nullptr)
    *y = scroll_y_;
}

void CoreWindowHost::GetDimensions(int* width, int* height) const {
  int w = kDefaultViewportWidth;
  int h = kDefaultViewportHeight;
  if (canvas_node_) {
    auto size = canvas_node_->GetSize();
    if (size.width > 0.0f)
      w = static_cast<int>(size.width);
    if (size.height > 0.0f)
      h = static_cast<int>(size.height);
  }
  if (width != nullptr)
    *width = w;
  if (height != nullptr)
    *height = h;
}

void CoreWindowHost::SetDragStatus(core_window_drag_status status) {
  drag_status_ = status;
}

browser_mouse_state CoreWindowHost::GetModifierFlags() const {
  int flags = 0;
  if (shift_pressed_)
    flags |= BROWSER_MOUSE_MOD_1;
  if (ctrl_pressed_)
    flags |= BROWSER_MOUSE_MOD_2;
  return static_cast<browser_mouse_state>(flags);
}

void CoreWindowHost::ScheduleScrollBarUpdate() {
  if (scrollbar_update_scheduled_)
    return;
  scrollbar_update_scheduled_ = true;
  auto is_alive = is_alive_;
  CoreWindowHost* self = this;
  ::perception::DeferAfterEvents([self, is_alive]() {
    if (!*is_alive)
      return;
    self->scrollbar_update_scheduled_ = false;
    self->UpdateScrollBars();
  });
}

void CoreWindowHost::UpdateScrollBars() {
  if (!canvas_node_)
    return;

  float viewport_w = canvas_node_->GetSize().width;
  float viewport_h = canvas_node_->GetSize().height;
  if (viewport_w < 1.0f)
    viewport_w = 1.0f;
  if (viewport_h < 1.0f)
    viewport_h = 1.0f;

  bool show_h = content_width_ > static_cast<int>(viewport_w);
  bool show_v = content_height_ > static_cast<int>(viewport_h);

  if (h_scroll_bar_node_) {
    YGDisplay desired = show_h ? YGDisplayFlex : YGDisplayNone;
    if (h_scroll_bar_node_->GetLayout().GetDisplay() != desired)
      h_scroll_bar_node_->GetLayout().SetDisplay(desired);
  }
  if (v_scroll_bar_node_) {
    YGDisplay desired = show_v ? YGDisplayFlex : YGDisplayNone;
    if (v_scroll_bar_node_->GetLayout().GetDisplay() != desired)
      v_scroll_bar_node_->GetLayout().SetDisplay(desired);
  }

  int max_scroll_x = std::max(0, content_width_ - static_cast<int>(viewport_w));
  int max_scroll_y =
      std::max(0, content_height_ - static_cast<int>(viewport_h));
  if (scroll_x_ > max_scroll_x) {
    scroll_x_ = max_scroll_x;
    canvas_node_->Invalidate();
  }
  if (scroll_y_ > max_scroll_y) {
    scroll_y_ = max_scroll_y;
    canvas_node_->Invalidate();
  }

  int effective_w = std::max(content_width_, static_cast<int>(viewport_w));
  int effective_h = std::max(content_height_, static_cast<int>(viewport_h));

  if (h_scroll_bar_) {
    h_scroll_bar_->SetAlwaysShowScrollBar(show_h);
    h_scroll_bar_->SetValue(0.0f, static_cast<float>(effective_w),
                            static_cast<float>(scroll_x_), viewport_w);
  }
  if (v_scroll_bar_) {
    v_scroll_bar_->SetAlwaysShowScrollBar(show_v);
    v_scroll_bar_->SetValue(0.0f, static_cast<float>(effective_h),
                            static_cast<float>(scroll_y_), viewport_h);
  }
}

void CoreWindowHost::HandleKeyDown(
    const ::perception::window::KeyboardKeyEvent& event) {
  using ::perception::ui::IsControlKey;
  using ::perception::ui::IsShiftKey;
  using ::perception::ui::KeyCode;
  using ::perception::ui::ScancodeToAscii;

  if (IsShiftKey(event.key)) {
    shift_pressed_ = true;
    return;
  }
  if (IsControlKey(event.key)) {
    ctrl_pressed_ = true;
    return;
  }

  if (!on_key_)
    return;

  if (ctrl_pressed_) {
    char ascii = ScancodeToAscii(event.key, false);
    if (ascii == 'a' || ascii == 'A') {
      on_key_(NS_KEY_SELECT_ALL);
      return;
    }
    if (ascii == 'c' || ascii == 'C') {
      on_key_(NS_KEY_COPY_SELECTION);
      return;
    }
    if (ascii == 'v' || ascii == 'V') {
      on_key_(NS_KEY_PASTE);
      return;
    }
    if (ascii == 'x' || ascii == 'X') {
      on_key_(NS_KEY_CUT_SELECTION);
      return;
    }
    if (ascii == 'z' || ascii == 'Z') {
      on_key_(shift_pressed_ ? NS_KEY_REDO : NS_KEY_UNDO);
      return;
    }
    if (ascii == 'y' || ascii == 'Y') {
      on_key_(NS_KEY_REDO);
      return;
    }
  }

  KeyCode key = static_cast<KeyCode>(event.key);
  switch (key) {
    case KeyCode::Backspace:
      on_key_(NS_KEY_DELETE_LEFT);
      return;
    case KeyCode::Delete:
      on_key_(NS_KEY_DELETE_RIGHT);
      return;
    case KeyCode::Enter:
      on_key_(NS_KEY_CR);
      return;
    case KeyCode::Tab:
      on_key_(shift_pressed_ ? NS_KEY_SHIFT_TAB : NS_KEY_TAB);
      return;
    case KeyCode::LeftArrow:
      if (ctrl_pressed_)
        on_key_(NS_KEY_LINE_START);
      else if (shift_pressed_)
        on_key_(NS_KEY_WORD_LEFT);
      else
        on_key_(NS_KEY_LEFT);
      return;
    case KeyCode::RightArrow:
      if (ctrl_pressed_)
        on_key_(NS_KEY_LINE_END);
      else if (shift_pressed_)
        on_key_(NS_KEY_WORD_RIGHT);
      else
        on_key_(NS_KEY_RIGHT);
      return;
    case KeyCode::UpArrow:
      on_key_(NS_KEY_UP);
      return;
    case KeyCode::DownArrow:
      on_key_(NS_KEY_DOWN);
      return;
    case KeyCode::Home:
      on_key_(ctrl_pressed_ ? NS_KEY_TEXT_START : NS_KEY_LINE_START);
      return;
    case KeyCode::End:
      on_key_(ctrl_pressed_ ? NS_KEY_TEXT_END : NS_KEY_LINE_END);
      return;
    case KeyCode::PageUp:
      on_key_(NS_KEY_PAGE_UP);
      return;
    case KeyCode::PageDown:
      on_key_(NS_KEY_PAGE_DOWN);
      return;
    case KeyCode::Escape:
      on_key_(NS_KEY_ESCAPE);
      return;
    default:
      break;
  }

  char ascii = ScancodeToAscii(event.key, shift_pressed_);
  if (ascii != '\0')
    on_key_(static_cast<uint32_t>(static_cast<unsigned char>(ascii)));
}

void CoreWindowHost::HandleKeyUp(
    const ::perception::window::KeyboardKeyEvent& event) {
  using ::perception::ui::IsControlKey;
  using ::perception::ui::IsShiftKey;

  if (IsShiftKey(event.key))
    shift_pressed_ = false;
  else if (IsControlKey(event.key))
    ctrl_pressed_ = false;
}

void CoreWindowHost::BuildUi() {
  canvas_node_ = Node::Empty([](Layout& layout) {
    layout.SetFlexGrow(1.0f);
    layout.SetAlignSelf(YGAlignStretch);
  });

  h_scroll_bar_node_ = ScrollBar::HorizontalScrollBar(
      &h_scroll_bar_, [](Layout& layout) { layout.SetAlignSelf(YGAlignStretch); });
  v_scroll_bar_node_ = ScrollBar::VerticalScrollBar(
      &v_scroll_bar_, [](Layout& layout) { layout.SetAlignSelf(YGAlignStretch); });

  root_node_ = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetAlignSelf(YGAlignStretch);
        layout.SetGap(0.0f);
      },
      Container::VerticalContainer(
          [](Layout& layout) {
            layout.SetFlexGrow(1.0f);
            layout.SetFlexShrink(1.0f);
            layout.SetAlignSelf(YGAlignStretch);
            layout.SetGap(0.0f);
          },
          canvas_node_, h_scroll_bar_node_),
      v_scroll_bar_node_);

  std::weak_ptr<CoreWindowHost> weak_self = shared_from_this();

  h_scroll_bar_->OnScroll([weak_self](float value) {
    auto self = weak_self.lock();
    if (!self)
      return;
    int new_x = static_cast<int>(std::lround(value));
    if (new_x != self->scroll_x_) {
      self->scroll_x_ = new_x;
      if (self->canvas_node_)
        self->canvas_node_->Invalidate();
    }
  });

  v_scroll_bar_->OnScroll([weak_self](float value) {
    auto self = weak_self.lock();
    if (!self)
      return;
    int new_y = static_cast<int>(std::lround(value));
    if (new_y != self->scroll_y_) {
      self->scroll_y_ = new_y;
      if (self->canvas_node_)
        self->canvas_node_->Invalidate();
    }
  });

  auto focusable = canvas_node_->GetOrAdd<Focusable>();
  focusable->OnUnfocus([weak_self]() {
    if (auto self = weak_self.lock()) {
      self->shift_pressed_ = false;
      self->ctrl_pressed_ = false;
    }
  });
  focusable->OnKeyDown(
      [weak_self](const ::perception::window::KeyboardKeyEvent& event) {
        if (auto self = weak_self.lock()) {
          NETSURF_LOCK;
          self->HandleKeyDown(event);
        }
      });
  focusable->OnKeyUp(
      [weak_self](const ::perception::window::KeyboardKeyEvent& event) {
        if (auto self = weak_self.lock()) {
          NETSURF_LOCK;
          self->HandleKeyUp(event);
        }
      });

  canvas_node_->OnDraw([weak_self](const DrawContext& context) {
    auto self = weak_self.lock();
    if (!self || !context.skia_canvas)
      return;

    NETSURF_LOCK;
    int width = static_cast<int>(context.area.size.width);
    int height = static_cast<int>(context.area.size.height);
    if (width <= 0)
      width = 1;
    if (height <= 0)
      height = 1;

    if (self->last_viewport_width_ != width ||
        self->last_viewport_height_ != height) {
      self->last_viewport_width_ = width;
      self->last_viewport_height_ = height;
      self->ScheduleScrollBarUpdate();
    }

    int save_count = context.skia_canvas->save();
    SkRect viewport_rect =
        SkRect::MakeXYWH(context.area.origin.x, context.area.origin.y,
                         context.area.size.width, context.area.size.height);
    context.skia_canvas->clipRect(viewport_rect);

    SkPaint bg_paint;
    bg_paint.setColor(kCoreWindowBackgroundColor);
    context.skia_canvas->drawRect(viewport_rect, bg_paint);

    context.skia_canvas->translate(
        context.area.origin.x - static_cast<float>(self->scroll_x_),
        context.area.origin.y - static_cast<float>(self->scroll_y_));

    SetActiveCanvas(context.skia_canvas);

    struct redraw_context ctx = {
        .interactive = true,
        .background_images = true,
        .plot = &skia_plotters,
    };

    struct rect clip = {
        .x0 = self->scroll_x_,
        .y0 = self->scroll_y_,
        .x1 = self->scroll_x_ + width,
        .y1 = self->scroll_y_ + height,
    };

    if (self->on_draw_)
      self->on_draw_(clip, ctx);

    SetActiveCanvas(nullptr);
    context.skia_canvas->restoreToCount(save_count);
  });

  canvas_node_->OnMouseHover([weak_self](const Point& p) {
    auto self = weak_self.lock();
    if (!self || !self->on_mouse_)
      return;

    NETSURF_LOCK;
    int content_x = static_cast<int>(p.x) + self->scroll_x_;
    int content_y = static_cast<int>(p.y) + self->scroll_y_;
    browser_mouse_state mods = self->GetModifierFlags();

    if (self->is_mouse_down_) {
      if (!self->drag_started_) {
        if (std::abs(content_x - self->press_x_) > kDragThresholdPixels ||
            std::abs(content_y - self->press_y_) > kDragThresholdPixels) {
          browser_mouse_state drag_btn =
              (self->mouse_button_down_ ==
               ::perception::window::MouseButton::Right)
                  ? BROWSER_MOUSE_DRAG_2
                  : BROWSER_MOUSE_DRAG_1;
          self->on_mouse_(static_cast<browser_mouse_state>(drag_btn | mods),
                          self->press_x_, self->press_y_);
          self->drag_started_ = true;
        }
      }
      if (self->drag_started_) {
        browser_mouse_state hold_btn =
            (self->mouse_button_down_ ==
             ::perception::window::MouseButton::Right)
                ? BROWSER_MOUSE_HOLDING_2
                : BROWSER_MOUSE_HOLDING_1;
        self->on_mouse_(static_cast<browser_mouse_state>(BROWSER_MOUSE_DRAG_ON |
                                                         hold_btn | mods),
                        content_x, content_y);
      }
      return;
    }

    self->on_mouse_(static_cast<browser_mouse_state>(BROWSER_MOUSE_HOVER | mods),
                    content_x, content_y);
  });

  canvas_node_->OnMouseLeave([weak_self]() {
    auto self = weak_self.lock();
    if (!self || !self->on_mouse_)
      return;
    NETSURF_LOCK;
    self->on_mouse_(BROWSER_MOUSE_LEAVE, 0, 0);
  });

  canvas_node_->OnMouseButtonDown(
      [weak_self, focusable](const Point& p,
                             ::perception::window::MouseButton button) {
        auto self = weak_self.lock();
        if (!self)
          return;
        if (focusable)
          focusable->Focus();
        if (!self->on_mouse_)
          return;

        NETSURF_LOCK;
        int content_x = static_cast<int>(p.x) + self->scroll_x_;
        int content_y = static_cast<int>(p.y) + self->scroll_y_;

        self->is_mouse_down_ = true;
        self->drag_started_ = false;
        self->mouse_button_down_ = button;
        self->press_x_ = content_x;
        self->press_y_ = content_y;

        browser_mouse_state press_state = static_cast<browser_mouse_state>(0);
        if (button == ::perception::window::MouseButton::Left)
          press_state = BROWSER_MOUSE_PRESS_1;
        else if (button == ::perception::window::MouseButton::Right ||
                 button == ::perception::window::MouseButton::Middle)
          press_state = BROWSER_MOUSE_PRESS_2;

        if (press_state != 0) {
          self->on_mouse_(static_cast<browser_mouse_state>(
                              press_state | self->GetModifierFlags()),
                          content_x, content_y);
        }
      });

  canvas_node_->OnMouseButtonUp(
      [weak_self](const Point& p, ::perception::window::MouseButton button) {
        auto self = weak_self.lock();
        if (!self || !self->on_mouse_)
          return;

        NETSURF_LOCK;
        int content_x = static_cast<int>(p.x) + self->scroll_x_;
        int content_y = static_cast<int>(p.y) + self->scroll_y_;
        browser_mouse_state mods = self->GetModifierFlags();

        if (self->drag_started_) {
          self->is_mouse_down_ = false;
          self->drag_started_ = false;
          self->on_mouse_(
              static_cast<browser_mouse_state>(BROWSER_MOUSE_HOVER | mods),
              content_x, content_y);
          return;
        }

        self->is_mouse_down_ = false;
        browser_mouse_state click_state = static_cast<browser_mouse_state>(0);
        if (button == ::perception::window::MouseButton::Left)
          click_state = BROWSER_MOUSE_CLICK_1;
        else if (button == ::perception::window::MouseButton::Right ||
                 button == ::perception::window::MouseButton::Middle)
          click_state = BROWSER_MOUSE_CLICK_2;

        if (click_state != 0) {
          auto now = std::chrono::steady_clock::now();
          auto elapsed_ms =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  now - self->last_click_time_)
                  .count();
          if (elapsed_ms <= kDoubleClickTimeMs &&
              std::abs(content_x - self->last_click_x_) <=
                  kDoubleClickDistancePixels &&
              std::abs(content_y - self->last_click_y_) <=
                  kDoubleClickDistancePixels) {
            click_state = static_cast<browser_mouse_state>(
                click_state | BROWSER_MOUSE_DOUBLE_CLICK);
          }
          self->last_click_time_ = now;
          self->last_click_x_ = content_x;
          self->last_click_y_ = content_y;

          self->on_mouse_(static_cast<browser_mouse_state>(click_state | mods),
                          content_x, content_y);
        }
      });

  UpdateScrollBars();
}

}  // namespace perception
}  // namespace netsurf
