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

#include "window.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

#include "misc.h"
#include "plotters.h"
#include "settings.h"
#include "tabs.h"

extern "C" {
#include <libwapcaplet/libwapcaplet.h>

#include "content/fetch.h"
#include "content/fetchers.h"
#include "desktop/browser_history.h"
#include "netsurf/bitmap.h"
#include "netsurf/browser_window.h"
#include "netsurf/clipboard.h"
#include "netsurf/content_type.h"
#include "netsurf/cookie_db.h"
#include "netsurf/fetch.h"
#include "netsurf/layout.h"
#include "netsurf/misc.h"
#include "netsurf/mouse.h"
#include "netsurf/netsurf.h"
#include "netsurf/plot_style.h"
#include "netsurf/plotters.h"
#include "netsurf/window.h"
#include "utils/errors.h"
#include "utils/file.h"
#include "utils/filepath.h"
#include "utils/log.h"
#include "utils/messages.h"
#include "utils/nsoption.h"
#include "utils/nsurl.h"
content_status content_get_status(struct hlcache_handle* h);
const uint8_t* content_get_source_data(struct hlcache_handle* h, size_t* size);
}

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkRect.h"
#include "include/core/SkString.h"
#include "perception/debug.h"
#include "perception/fibers.h"
#include "perception/processes.h"
#include "perception/random.h"
#include "perception/scheduler.h"
#include "perception/time.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/checkbox.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/scroll_bar.h"
#include "perception/ui/components/tab_bar.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/font.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/theme.h"

using ::perception::HandOverControl;
using ::perception::TerminateProcess;
using ::perception::ui::DrawContext;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::Point;
using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::Checkbox;
using ::perception::ui::components::Container;
using ::perception::ui::components::InputBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::ScrollBar;
using ::perception::ui::components::TabBar;
using ::perception::ui::components::UiWindow;

namespace netsurf {
namespace perception {

std::recursive_mutex& GetNetSurfMutex() {
  static std::recursive_mutex mutex;
  return mutex;
}

Window::Window() : is_alive_(std::make_shared<bool>(true)) {}

Window::~Window() {
  if (is_alive_) {
    *is_alive_ = false;
  }
}

namespace {

SkColor ConvertColor(colour c) {
  uint8_t r = (c) & 0xff;
  uint8_t g = (c >> 8) & 0xff;
  uint8_t b = (c >> 16) & 0xff;
  uint8_t a = 255;
  if (c == NS_TRANSPARENT) {
    a = 0;
  } else if ((c & 0xff000000) != 0) {
    a = (c >> 24) & 0xff;
  }
  return SkColorSetARGB(a, r, g, b);
}

::perception::window::Cursor MapPointerShape(gui_pointer_shape shape) {
  switch (shape) {
    case GUI_POINTER_POINT:
      return ::perception::window::Cursor::Poke;
    case GUI_POINTER_CARET:
      return ::perception::window::Cursor::Caret;
    case GUI_POINTER_MOVE:
      return ::perception::window::Cursor::Drag;
    case GUI_POINTER_UP:
    case GUI_POINTER_DOWN:
      return ::perception::window::Cursor::ResizeVertical;
    case GUI_POINTER_LEFT:
    case GUI_POINTER_RIGHT:
      return ::perception::window::Cursor::ResizeHorizontal;
    case GUI_POINTER_RU:
    case GUI_POINTER_LD:
      return ::perception::window::Cursor::ResizeDiagonalTopRightBottomLeft;
    case GUI_POINTER_LU:
    case GUI_POINTER_RD:
      return ::perception::window::Cursor::ResizeDiagonalTopLeftBottomRight;
    default:
      return ::perception::window::Cursor::Pointer;
  }
}

static struct gui_window* gui_window_create(struct browser_window* bw,
                                            struct gui_window* existing,
                                            gui_window_create_flags flags);

static void gui_window_destroy(struct gui_window* gw);

nserror FbWindowInvalidateArea(struct gui_window* g, const struct rect* rect) {
  if (g && g->GetContentNode()) {
    g->GetContentNode()->Invalidate();
  }
  return NSERROR_OK;
}

bool GuiWindowGetScroll(struct gui_window* g, int* sx, int* sy) {
  NETSURF_LOCK;
  *sx = g ? (int)g->GetScroll().x : 0;
  *sy = g ? (int)g->GetScroll().y : 0;
  return true;
}

nserror GuiWindowSetScroll(struct gui_window* gw, const struct rect* rect) {
  NETSURF_LOCK;
  if (gw) {
    gw->GetScroll().x = (float)rect->x0;
    gw->GetScroll().y = (float)rect->y0;
    gw->GetPendingScroll() = gw->GetScroll();
    if (gw->GetContentNode()) gw->GetContentNode()->Invalidate();
    UpdateScrollBars(gw);
  }
  return NSERROR_OK;
}

nserror GuiWindowGetDimensions(struct gui_window* gw, int* width, int* height) {
  NETSURF_LOCK;
  if (gw && gw->GetContentNode()) {
    *width = (int)gw->GetContentNode()->GetSize().width;
    *height = (int)gw->GetContentNode()->GetSize().height;
  } else {
    *width = 800;
    *height = 600;
  }
  return NSERROR_OK;
}

nserror GuiWindowEvent(struct gui_window* gw, enum gui_window_event event) {
  NETSURF_LOCK;
  if (event == GW_EVENT_UPDATE_EXTENT || event == GW_EVENT_NEW_CONTENT) {
    if (gw && !gw->GetExtentDeferred()) {
      gw->GetExtentDeferred() = true;
      auto is_alive = gw->GetIsAlive();
      ::perception::DeferAfterEvents([gw, is_alive]() {
        if (!*is_alive) return;
        NETSURF_LOCK;
        UpdateScrollBars(gw);
        if (gw->GetContentNode()) {
          gw->GetContentNode()->Invalidate();
        }
        gw->GetExtentDeferred() = false;
      });
    }
  }
  return NSERROR_OK;
}

nserror GuiWindowSetUrl(struct gui_window* g, nsurl* url) {
  if (g) {
    const char* url_str = url ? nsurl_access(url) : "NULL";
    auto* url_input = GetGlobalUrlInput();
    if (url_input && !url_input->HasFocus()) {
      url_input->SetText(url_str);
    }
  }
  return NSERROR_OK;
}

void GuiWindowSetStatus(struct gui_window* g, const char* text) {
  auto* status_label = GetGlobalStatusLabel();
  if (g && status_label) status_label->SetText(text);
}

void GuiWindowSetPointer(struct gui_window* g, gui_pointer_shape shape) {
  if (g && g->GetContentNode())
    g->GetContentNode()->SetCursor(MapPointerShape(shape));
}

void GuiWindowPlaceCaret(struct gui_window* g, int x, int y, int height,
                         const struct rect* clip) {}

void GuiWindowSetTitle(struct gui_window* gw, const char* title) {
  if (gw) {
    gw->GetTitle() = title ? title : "";
    if (!gw->GetTitleDeferred()) {
      gw->GetTitleDeferred() = true;
      auto is_alive = gw->GetIsAlive();
      ::perception::DeferAfterEvents([gw, is_alive]() {
        if (!*is_alive) return;
        NETSURF_LOCK;
        UpdateTabBar();
        if (GetGlobalUiWindow()) {
          GetGlobalUiWindow()->Invalidate();
        }
        gw->GetTitleDeferred() = false;
      });
    }
  }
}

void FlushPendingMouseHover(struct gui_window* gw) {
  if (gw && gw->GetHasPendingHover()) {
    browser_window_mouse_click(
        gw->GetBrowserWindow(), gw->GetPendingHoverState(),
        (int)gw->GetPendingHover().x, (int)gw->GetPendingHover().y);
    gw->GetHasPendingHover() = false;
  }
}

static struct gui_window* gui_window_create(struct browser_window* bw,
                                            struct gui_window* existing,
                                            gui_window_create_flags flags) {
  struct gui_window* gw = new gui_window();
  gw->GetBrowserWindow() = bw;
  gw->GetScroll() = {.x = 0.0f, .y = 0.0f};
  gw->GetTitle() = "New Tab";

  // Create Content Viewport Node for this tab.
  gw->GetContentNode() = Node::Empty([](Layout& layout) {
    layout.SetFlexGrow(1.0f);
    layout.SetAlignSelf(YGAlignStretch);
  });

  auto h_scroll_bar_node = ScrollBar::HorizontalScrollBar(
      &gw->GetHorizontalScrollBar(),
      [](Layout& layout) { layout.SetAlignSelf(YGAlignStretch); });
  auto v_scroll_bar_node = ScrollBar::VerticalScrollBar(
      &gw->GetVerticalScrollBar(),
      [](Layout& layout) { layout.SetAlignSelf(YGAlignStretch); });

  gw->GetTabRootNode() = Container::HorizontalContainer(
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
          gw->GetContentNode(), h_scroll_bar_node),
      v_scroll_bar_node);

  gw->GetHorizontalScrollBar()->OnScroll([gw](float value) {
    if (value == gw->GetPendingScroll().x) return;
    gw->GetPendingScroll().x = value;
    gw->GetHasPendingScroll() = true;

    if (!gw->GetScrollDeferred()) {
      gw->GetScrollDeferred() = true;
      auto is_alive = gw->GetIsAlive();
      ::perception::DeferAfterEvents([gw, is_alive]() {
        if (!*is_alive) return;
        if (gw->GetHasPendingScroll()) {
          gw->GetScroll() = gw->GetPendingScroll();
          gw->GetContentNode()->Invalidate();
          gw->GetHasPendingScroll() = false;
        }
        gw->GetScrollDeferred() = false;
      });
    }
  });

  gw->GetVerticalScrollBar()->OnScroll([gw](float value) {
    if (value == gw->GetPendingScroll().y) return;
    gw->GetPendingScroll().y = value;
    gw->GetHasPendingScroll() = true;

    if (!gw->GetScrollDeferred()) {
      gw->GetScrollDeferred() = true;
      auto is_alive = gw->GetIsAlive();
      ::perception::DeferAfterEvents([gw, is_alive]() {
        if (!*is_alive) return;
        if (gw->GetHasPendingScroll()) {
          gw->GetScroll() = gw->GetPendingScroll();
          gw->GetContentNode()->Invalidate();
          gw->GetHasPendingScroll() = false;
        }
        gw->GetScrollDeferred() = false;
      });
    }
  });

  // Wire up Draw callback using Skia Canvas.
  gw->GetContentNode()->OnDraw([gw](const DrawContext& context) {
    NETSURF_LOCK;
    if (!context.skia_canvas) return;

    if (!browser_window_has_content(gw->GetBrowserWindow())) {
      SkPaint paint;
      paint.setColor(SkColorSetARGB(255, 255, 255, 255));
      context.skia_canvas->drawRect(
          SkRect::MakeXYWH(context.area.origin.x, context.area.origin.y,
                           context.area.size.width, context.area.size.height),
          paint);
      return;
    }

    struct hlcache_handle* content =
        browser_window_get_content(gw->GetBrowserWindow());
    if (!content) return;

    content_status status = content_get_status(content);
    if (status != CONTENT_STATUS_READY && status != CONTENT_STATUS_DONE) {
      SkPaint paint;
      paint.setColor(SkColorSetARGB(255, 255, 255, 255));
      context.skia_canvas->drawRect(
          SkRect::MakeXYWH(context.area.origin.x, context.area.origin.y,
                           context.area.size.width, context.area.size.height),
          paint);
      return;
    }

    int w = (int)context.area.size.width;
    int h = (int)context.area.size.height;

    if (w < 100) w = 100;
    if (h < 100) h = 100;

    SetActiveCanvas(context.skia_canvas);

    struct redraw_context ctx = {
        .interactive = true, .background_images = true, .plot = &skia_plotters};

    struct rect rect = {.x0 = (int)gw->GetScroll().x,
                        .y0 = (int)gw->GetScroll().y,
                        .x1 = (int)gw->GetScroll().x + w,
                        .y1 = (int)gw->GetScroll().y + h};

    context.skia_canvas->save();
    context.skia_canvas->translate(context.area.origin.x - gw->GetScroll().x,
                                   context.area.origin.y - gw->GetScroll().y);
    context.skia_canvas->save();

    browser_window_redraw(gw->GetBrowserWindow(), 0, 0, &rect, &ctx);

    context.skia_canvas->restore();
    context.skia_canvas->restore();
    SetActiveCanvas(nullptr);
  });

  // Wire up Mouse interaction.
  gw->GetContentNode()->OnMouseHover([gw](const Point& p) {
    gw->GetPendingHover() = {.x = p.x + gw->GetScroll().x,
                             .y = p.y + gw->GetScroll().y};
    gw->GetPendingHoverState() = BROWSER_MOUSE_HOVER;
    gw->GetHasPendingHover() = true;

    if (!gw->GetHoverDeferred()) {
      gw->GetHoverDeferred() = true;
      auto is_alive = gw->GetIsAlive();
      ::perception::DeferAfterEvents([gw, is_alive]() {
        if (!*is_alive) return;
        if (gw->GetHasPendingHover()) {
          NETSURF_LOCK;
          browser_window_mouse_click(
              gw->GetBrowserWindow(), gw->GetPendingHoverState(),
              (int)gw->GetPendingHover().x, (int)gw->GetPendingHover().y);
          gw->GetHasPendingHover() = false;
        }
        gw->GetHoverDeferred() = false;
      });
    }
  });

  gw->GetContentNode()->OnMouseButtonDown(
      [gw](const Point& p, ::perception::window::MouseButton button) {
        NETSURF_LOCK;
        FlushPendingMouseHover(gw);
        browser_mouse_state mouse_state = (browser_mouse_state)0;
        if (button == ::perception::window::MouseButton::Left) {
          mouse_state = BROWSER_MOUSE_PRESS_1;
        } else if (button == ::perception::window::MouseButton::Right) {
          mouse_state = BROWSER_MOUSE_PRESS_2;
        } else if (button == ::perception::window::MouseButton::Middle) {
          mouse_state = BROWSER_MOUSE_PRESS_3;
        }
        browser_window_mouse_click(gw->GetBrowserWindow(), mouse_state,
                                   (int)p.x + (int)gw->GetScroll().x,
                                   (int)p.y + (int)gw->GetScroll().y);
      });

  gw->GetContentNode()->OnMouseButtonUp(
      [gw](const Point& p, ::perception::window::MouseButton button) {
        NETSURF_LOCK;
        FlushPendingMouseHover(gw);
        browser_mouse_state mouse_state = (browser_mouse_state)0;
        if (button == ::perception::window::MouseButton::Left) {
          mouse_state = BROWSER_MOUSE_CLICK_1;
        } else if (button == ::perception::window::MouseButton::Right) {
          mouse_state = BROWSER_MOUSE_CLICK_2;
        } else if (button == ::perception::window::MouseButton::Middle) {
          mouse_state = BROWSER_MOUSE_CLICK_3;
        }
        browser_window_mouse_click(gw->GetBrowserWindow(), mouse_state,
                                   (int)p.x + (int)gw->GetScroll().x,
                                   (int)p.y + (int)gw->GetScroll().y);
      });

  AddTab(gw);

  if (!GetGlobalUiWindow()) {
    // Construct UI elements for the very first time.
    auto go_back = []() {
      NETSURF_LOCK;
      if (GetActiveTab()) {
        browser_window_history_back(GetActiveTab()->GetBrowserWindow(), false);
      }
    };

    auto go_forward = []() {
      NETSURF_LOCK;
      if (GetActiveTab()) {
        browser_window_history_forward(GetActiveTab()->GetBrowserWindow(),
                                       false);
      }
    };

    auto reload_page = []() {
      NETSURF_LOCK;
      if (GetActiveTab()) {
        browser_window_reload(GetActiveTab()->GetBrowserWindow(), false);
      }
    };

    auto vc = Container::VerticalContainer([](Layout& layout) {
      layout.SetFlexGrow(1.0f);
      layout.SetAlignSelf(YGAlignStretch);
    });
    SetViewportContainer(vc);
    vc->AddChild(gw->GetTabRootNode());

    std::shared_ptr<TabBar> tab_bar_ptr;
    InputBox* url_input_ptr = nullptr;
    Label* status_label_ptr = nullptr;

    auto win = UiWindow::ResizableWindowWithTabBar(
        &tab_bar_ptr,
        [](UiWindow& win) {
          win.OnClose([]() { gui_quit(); });
          win.OnResize([]() {
            NETSURF_LOCK;
            for (auto gw : GetOpenTabs()) {
              if (gw && gw->GetBrowserWindow()) {
                browser_window_schedule_reformat(gw->GetBrowserWindow());
                if (gw->GetContentNode()) {
                  gw->GetContentNode()->Invalidate();
                }
              }
            }
          });
        },
        [](Layout& layout) {
          layout.SetWidth(640.0f);
          layout.SetHeight(480.0f);
          layout.SetPadding(YGEdgeHorizontal, 0.0f);
          layout.SetPadding(YGEdgeBottom, 0.0f);
        },
        Container::VerticalContainer(
            [](Layout& layout) {
              layout.SetFlexGrow(1.0f);
              layout.SetAlignSelf(YGAlignStretch);
              layout.SetGap(0.0f);
            },
            Container::HorizontalContainer(
                [](Layout& layout) {
                  layout.SetAlignSelf(YGAlignStretch);
                  layout.SetHeight(40.0f);
                  layout.SetGap(8.0f);
                  layout.SetPadding(YGEdgeAll, 4.0f);
                  layout.SetMargin(YGEdgeHorizontal, 8.0f);
                },
                Button::TextButton("Back", go_back),
                Button::TextButton("Forward", go_forward),
                Button::TextButton("Reload", reload_page),
                InputBox::BasicInputBox(
                    "", [](Layout& layout) { layout.SetFlexGrow(1.0f); },
                    [](InputBox& input_box) {
                      input_box.OnEnterPressed([](std::string_view text) {
                        ::perception::DebugPrinterSingleton
                            << "NetSurf Native: OnEnterPressed for URL: \""
                            << std::string(text).c_str() << "\"\n";
                        NETSURF_LOCK;
                        if (GetActiveTab()) {
                          std::string url_str(text);
                          if (url_str.find("://") == std::string::npos) {
                            url_str = "http://" + url_str;
                          }
                          nsurl* url;
                          nserror ret = nsurl_create(url_str.c_str(), &url);
                          ::perception::DebugPrinterSingleton
                              << "NetSurf Native: nsurl_create returned: "
                              << (int64)ret << "\n";
                          if (ret == NSERROR_OK) {
                            ::perception::DebugPrinterSingleton
                                << "NetSurf Native: Navigating active tab to "
                                   "URL!\n";
                            browser_window_navigate(
                                GetActiveTab()->GetBrowserWindow(), url, NULL,
                                BW_NAVIGATE_NONE, NULL, NULL, NULL);
                            nsurl_unref(url);
                          }
                        }
                      });
                    },
                    &url_input_ptr)),
            Block::SolidColor(::perception::ui::kContainerBorderColor,
                              [](Layout& layout) {
                                layout.SetAlignSelf(YGAlignStretch);
                                layout.SetHeight(1.0f);
                              }),
            vc,
            Block::SolidColor(::perception::ui::kContainerBorderColor,
                              [](Layout& layout) {
                                layout.SetAlignSelf(YGAlignStretch);
                                layout.SetHeight(1.0f);
                              }),
            Label::BasicLabel(
                "",
                [](Layout& layout) {
                  layout.SetAlignSelf(YGAlignStretch);
                  layout.SetHeight(20.0f);
                  layout.SetMargin(YGEdgeHorizontal, 8.0f);
                },
                [](Label& lbl) {
                  lbl.SetTextAlignment(
                      ::perception::ui::TextAlignment::MiddleLeft);
                },
                &status_label_ptr)));

    SetGlobalUiWindow(win);
    SetGlobalTabBar(tab_bar_ptr);
    SetGlobalUrlInput(url_input_ptr);
    SetGlobalStatusLabel(status_label_ptr);

    if (!win->GetChildren().empty()) {
      win->GetChildren().front()->Apply(
          [](Layout& layout) { layout.SetMargin(YGEdgeHorizontal, 0.0f); });
    }

    tab_bar_ptr->SetPrefixNode(Label::BasicLabel(
        "NetSurf", [](Label& lbl) { lbl.SetColor(0xFFFFFFFF); },
        [](Layout& layout) {
          // The margin was removed from the window so that the browser contents
          // can be full bleed.
          layout.SetMargin(YGEdgeLeft,
                           ::perception::ui::kUiWindowPadding * 2.0f);
          layout.SetMargin(YGEdgeRight, ::perception::ui::kUiWindowPadding);
        }));

    tab_bar_ptr->SetSuffixNode(Node::Empty(
        [](Layout& layout) {
          layout.SetMinWidth(20.0f);
          layout.SetAlignSelf(YGAlignStretch);
          layout.SetFlexDirection(YGFlexDirectionRow);
          layout.SetJustifyContent(YGJustifyCenter);
          layout.SetAlignItems(YGAlignStretch);
        },
        [](Node& node) {
          node.SetCursor(::perception::window::Cursor::Poke);
          node.OnMouseButtonUp([](const Point&,
                                  ::perception::window::MouseButton button) {
            if (button != ::perception::window::MouseButton::Left) return;
            ::perception::DebugPrinterSingleton
                << "NetSurf Native: Clicked '+' button to add a new tab.\n";
            ::perception::DeferAfterEvents([]() {
              NETSURF_LOCK;
              if (GetActiveTab()) {
                struct nsurl* url = nullptr;
                nserror ret = nsurl_create(
                    "file:///Applications/netsurf/res/en/welcome.html", &url);
                ::perception::DebugPrinterSingleton
                    << "  nsurl_create returned " << (int64)ret << "\n";
                if (ret == NSERROR_OK) {
                  struct browser_window* new_bw = nullptr;
                  nserror create_ret = browser_window_create(
                      (browser_window_create_flags)(BW_CREATE_TAB |
                                                    BW_CREATE_FOREGROUND |
                                                    BW_CREATE_HISTORY),
                      url, nullptr, GetActiveTab()->GetBrowserWindow(),
                      &new_bw);
                  ::perception::DebugPrinterSingleton
                      << "  browser_window_create returned "
                      << (int64)create_ret << "\n";
                  nsurl_unref(url);
                }
              }
            });
          });
        },
        Label::BasicLabel(
            "+",
            [](Label& lbl) {
              lbl.SetColor(0xFFFFFFFF);
              lbl.SetTextAlignment(
                  ::perception::ui::TextAlignment::MiddleCenter);
            },
            [](Layout& layout) {
              layout.SetMargin(YGEdgeRight, ::perception::ui::kUiWindowPadding);
            })));
    tab_bar_ptr->OnTabSelected([](int index) { SwitchTab(index); });
    tab_bar_ptr->OnTabClosed([](int index) { CloseTab(index); });

    SetActiveTabIndex(0);
  } else {
    // Dynamic new tab path.
    SetActiveTabIndex(GetTabCount() - 1);
    GetViewportContainer()->RemoveChildren();
    GetViewportContainer()->AddChild(gw->GetTabRootNode());
  }

  UpdateTabBar();
  GetGlobalUiWindow()->Invalidate();

  return gw;
}

static void gui_window_destroy(struct gui_window* gw) {
  NETSURF_LOCK;
  HandleTabDestroyed(gw);
  delete gw;
}

}  // namespace

struct gui_window_table perception_window_table = {
    .create = gui_window_create,
    .destroy = gui_window_destroy,
    .invalidate = FbWindowInvalidateArea,
    .get_scroll = GuiWindowGetScroll,
    .set_scroll = GuiWindowSetScroll,
    .get_dimensions = GuiWindowGetDimensions,
    .event = GuiWindowEvent,

    .set_title = GuiWindowSetTitle,
    .set_url = GuiWindowSetUrl,
    .set_status = GuiWindowSetStatus,
    .set_pointer = GuiWindowSetPointer,
    .place_caret = GuiWindowPlaceCaret,
};

}  // namespace perception
}  // namespace netsurf
