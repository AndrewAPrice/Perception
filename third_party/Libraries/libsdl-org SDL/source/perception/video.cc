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

#include "video.h"

extern "C" {
#include <SDL_mouse.h>
#include <SDL_syswm.h>
#include <SDL_version.h>
#include <SDL_video.h>

#include "events/SDL_events_c.h"
#include "video/SDL_pixels_c.h"
#include "video/SDL_sysvideo.h"
}

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "perception/clipboard.h"
#include "perception/scheduler.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/layout.h"
#include "perception/window/cursor.h"
#include "perception/window/rectangle.h"
#include "perception/window/window.h"
#include "perception/window/window_manager.h"
#include "window.h"

namespace {

struct PerceptionWindowData {
  std::shared_ptr<PerceptionSDLWindow> ui_window;
  int width = 0;
  int height = 0;
};

perception::window::Cursor current_cursor = perception::window::Cursor::Pointer;

SDL_Cursor* CreateCursor(SDL_Surface* surface, int hot_x, int hot_y) {
  SDL_Cursor* cursor = (SDL_Cursor*)SDL_calloc(1, sizeof(SDL_Cursor));
  if (!cursor) return nullptr;
  cursor->driverdata = reinterpret_cast<void*>(
      static_cast<uintptr_t>(perception::window::Cursor::Pointer));
  return cursor;
}

SDL_Cursor* CreateSystemCursor(SDL_SystemCursor id) {
  SDL_Cursor* cursor = (SDL_Cursor*)SDL_calloc(1, sizeof(SDL_Cursor));
  if (!cursor) return nullptr;
  perception::window::Cursor p_cursor = perception::window::Cursor::Pointer;
  switch (id) {
    case SDL_SYSTEM_CURSOR_IBEAM:
      p_cursor = perception::window::Cursor::Caret;
      break;
    case SDL_SYSTEM_CURSOR_SIZENWSE:
      p_cursor = perception::window::Cursor::ResizeDiagonalTopLeftBottomRight;
      break;
    case SDL_SYSTEM_CURSOR_SIZENESW:
      p_cursor = perception::window::Cursor::ResizeDiagonalTopRightBottomLeft;
      break;
    case SDL_SYSTEM_CURSOR_SIZEWE:
      p_cursor = perception::window::Cursor::ResizeHorizontal;
      break;
    case SDL_SYSTEM_CURSOR_SIZENS:
      p_cursor = perception::window::Cursor::ResizeVertical;
      break;
    case SDL_SYSTEM_CURSOR_SIZEALL:
      p_cursor = perception::window::Cursor::Drag;
      break;
    case SDL_SYSTEM_CURSOR_HAND:
      p_cursor = perception::window::Cursor::Poke;
      break;
    default:
      p_cursor = perception::window::Cursor::Pointer;
      break;
  }
  cursor->driverdata =
      reinterpret_cast<void*>(static_cast<uintptr_t>(p_cursor));
  return cursor;
}

int ShowCursor(SDL_Cursor* cursor) {
  if (!cursor) {
    current_cursor = perception::window::Cursor::Hidden;
  } else {
    current_cursor = static_cast<perception::window::Cursor>(
        reinterpret_cast<uintptr_t>(cursor->driverdata));
  }
  SDL_VideoDevice* video_device = SDL_GetVideoDevice();
  if (video_device && video_device->windows) {
    for (SDL_Window* window = video_device->windows; window;
         window = window->next) {
      auto data = (PerceptionWindowData*)window->driverdata;
      if (data && data->ui_window && data->ui_window->base_window_) {
        data->ui_window->base_window_->SetCursor(current_cursor);
      }
    }
  }
  return 0;
}

void FreeCursor(SDL_Cursor* cursor) {
  if (cursor) SDL_free(cursor);
}

int VideoInit(_THIS) {
  SDL_DisplayMode mode;
  SDL_zero(mode);
  mode.format = SDL_PIXELFORMAT_BGRA8888;
  mode.w = 1024;
  mode.h = 768;
  mode.refresh_rate = 60;
  SDL_AddBasicVideoDisplay(&mode);

  SDL_Mouse* mouse = SDL_GetMouse();
  if (mouse) {
    mouse->CreateCursor = CreateCursor;
    mouse->CreateSystemCursor = CreateSystemCursor;
    mouse->ShowCursor = ShowCursor;
    mouse->FreeCursor = FreeCursor;
  }
  return 0;
}

void VideoQuit(_THIS) {}

int CreateWindow(_THIS, SDL_Window* window) {
  auto data = new PerceptionWindowData();
  data->width = window->w;
  data->height = window->h;

  std::string title = window->title ? window->title : "Perception SDL App";
  auto ui_win = std::make_shared<PerceptionSDLWindow>(window);

  perception::window::Window::CreationOptions options;
  options.title = title;
  options.prefered_width = window->w;
  options.prefered_height = window->h;
  options.prefer_fullscreen = (window->flags & SDL_WINDOW_FULLSCREEN) != 0;
  options.is_resizable = (window->flags & SDL_WINDOW_RESIZABLE) != 0 || true;
  options.add_title_bar = true;
  options.is_double_buffered = !options.prefer_fullscreen;

  auto base_window = perception::window::Window::CreateWindow(options);
  if (!base_window) {
    delete data;
    return -1;
  }
  base_window->SetDelegate(ui_win);
  ui_win->SetBaseWindow(base_window);
  base_window->SetCursor(current_cursor);
  base_window->Focus();

  data->ui_window = ui_win;
  window->driverdata = data;
  return 0;
}

void DestroyWindow(_THIS, SDL_Window* window) {
  auto data = (PerceptionWindowData*)window->driverdata;
  if (data) {
    if (data->ui_window && data->ui_window->base_window_) {
      data->ui_window->base_window_.reset();
      data->ui_window->WindowClosed();
    }
    delete data;
    window->driverdata = nullptr;
  }
}

int CreateWindowFramebuffer(_THIS, SDL_Window* window, Uint32* format,
                            void** pixels, int* pitch) {
  auto data = (PerceptionWindowData*)window->driverdata;
  if (!data || !data->ui_window) return -1;

  int w, h;
  SDL_GetWindowSizeInPixels(window, &w, &h);
  const Uint32 surface_format = SDL_PIXELFORMAT_BGRA8888;

  std::scoped_lock lock(data->ui_window->mutex_);
  if (data->ui_window->surface_) SDL_FreeSurface(data->ui_window->surface_);
  data->ui_window->surface_ =
      SDL_CreateRGBSurfaceWithFormat(0, w, h, 0, surface_format);
  if (!data->ui_window->surface_) return -1;

  *format = surface_format;
  *pixels = data->ui_window->surface_->pixels;
  *pitch = data->ui_window->surface_->pitch;

  return 0;
}

int UpdateWindowFramebuffer(_THIS, SDL_Window* window, const SDL_Rect* rects,
                            int numrects) {
  auto data = (PerceptionWindowData*)window->driverdata;
  if (data && data->ui_window && data->ui_window->base_window_) {
    std::optional<perception::window::Rectangle> dirty_rect;
    if (rects && numrects > 0) {
      int min_x = rects[0].x;
      int min_y = rects[0].y;
      int max_x = rects[0].x + rects[0].w;
      int max_y = rects[0].y + rects[0].h;
      for (int i = 1; i < numrects; ++i) {
        min_x = std::min(min_x, rects[i].x);
        min_y = std::min(min_y, rects[i].y);
        max_x = std::max(max_x, rects[i].x + rects[i].w);
        max_y = std::max(max_y, rects[i].y + rects[i].h);
      }
      dirty_rect = perception::window::Rectangle(min_x, min_y, max_x, max_y);
    }
    data->ui_window->base_window_->Present(dirty_rect);
  }
  return 0;
}

void DestroyWindowFramebuffer(_THIS, SDL_Window* window) {
  auto data = (PerceptionWindowData*)window->driverdata;
  if (data && data->ui_window) {
    std::scoped_lock lock(data->ui_window->mutex_);
    if (data->ui_window->surface_) {
      SDL_FreeSurface(data->ui_window->surface_);
      data->ui_window->surface_ = nullptr;
    }
  }
}

void ShowWindow(_THIS, SDL_Window* window) {
  auto data = (PerceptionWindowData*)window->driverdata;
  if (data && data->ui_window && data->ui_window->base_window_)
    data->ui_window->base_window_->Focus();
}

void HideWindow(_THIS, SDL_Window* window) {
  auto data = (PerceptionWindowData*)window->driverdata;
  if (data && data->ui_window && data->ui_window->base_window_) {
    data->ui_window->base_window_.reset();
    data->ui_window->WindowClosed();
  }
}

void SetWindowTitle(_THIS, SDL_Window* window) {
  auto data = (PerceptionWindowData*)window->driverdata;
  if (data && data->ui_window && data->ui_window->base_window_ &&
      window->title) {
    data->ui_window->base_window_->SetTitle(window->title);
  }
}

void SetWindowSize(_THIS, SDL_Window* window) {
  auto data = (PerceptionWindowData*)window->driverdata;
  if (data && data->ui_window && data->ui_window->base_window_)
    data->ui_window->base_window_->SetSize(window->w, window->h);
}

void PumpEvents(_THIS) { perception::FinishAnyPendingWork(); }

int SetClipboardText(_THIS, const char* text) {
  if (!text) return -1;
  ::perception::SetClipboard(std::string_view(text));
  return 0;
}

char* GetClipboardText(_THIS) {
  auto val_or = ::perception::GetClipboard();
  std::string str = "";
  if (val_or) {
    str = val_or->ToString();
  }
  return SDL_strdup(str.c_str());
}

SDL_bool HasClipboardText(_THIS) {
  auto val_or = ::perception::GetClipboard();
  if (val_or && !val_or->ToString().empty()) {
    return SDL_TRUE;
  }
  return SDL_FALSE;
}

int GetDisplayBounds(_THIS, SDL_VideoDisplay* display, SDL_Rect* rect) {
  if (!rect) return -1;
  auto response_or =
      ::perception::GetService<::perception::window::WindowManager>()
          .GetDisplayBounds();
  if (response_or && !response_or->bounds.empty()) {
    const auto& b = response_or->bounds[0];
    rect->x = b.min_x;
    rect->y = b.min_y;
    rect->w = b.max_x - b.min_x;
    rect->h = b.max_y - b.min_y;
    return 0;
  }
  rect->x = 0;
  rect->y = 0;
  rect->w = display ? display->desktop_mode.w : 1024;
  rect->h = display ? display->desktop_mode.h : 768;
  return 0;
}

int GetDisplayUsableBounds(_THIS, SDL_VideoDisplay* display, SDL_Rect* rect) {
  return GetDisplayBounds(_this, display, rect);
}

void GetWindowSizeInPixels(_THIS, SDL_Window* window, int* w, int* h) {
  auto data = (PerceptionWindowData*)window->driverdata;
  if (data && data->ui_window && data->ui_window->base_window_) {
    if (w) *w = data->ui_window->base_window_->GetWidth();
    if (h) *h = data->ui_window->base_window_->GetHeight();
  } else {
    if (w) *w = window->w;
    if (h) *h = window->h;
  }
}

void SetWindowFullscreen(_THIS, SDL_Window* window, SDL_VideoDisplay* display,
                         SDL_bool fullscreen) {
  auto data = (PerceptionWindowData*)window->driverdata;
  if (data && data->ui_window && data->ui_window->base_window_) {
    data->ui_window->base_window_->SetFullScreen(fullscreen == SDL_TRUE);
  }
}

void SetWindowMouseGrab(_THIS, SDL_Window* window, SDL_bool grabbed) {
  auto data = (PerceptionWindowData*)window->driverdata;
  if (data && data->ui_window && data->ui_window->base_window_) {
    data->ui_window->base_window_->SetCaptureMouse(grabbed == SDL_TRUE);
  }
}

void SetWindowKeyboardGrab(_THIS, SDL_Window* window, SDL_bool grabbed) {
  auto data = (PerceptionWindowData*)window->driverdata;
  if (data && data->ui_window && data->ui_window->base_window_) {
    data->ui_window->base_window_->SetCaptureKeyboard(grabbed == SDL_TRUE);
  }
}

int SetWindowInputFocus(_THIS, SDL_Window* window) {
  auto data = (PerceptionWindowData*)window->driverdata;
  if (data && data->ui_window && data->ui_window->base_window_) {
    data->ui_window->base_window_->Focus();
    return 0;
  }
  return -1;
}

void RaiseWindow(_THIS, SDL_Window* window) {
  auto data = (PerceptionWindowData*)window->driverdata;
  if (data && data->ui_window && data->ui_window->base_window_) {
    data->ui_window->base_window_->Focus();
  }
}

SDL_bool GetWindowWMInfo(_THIS, SDL_Window* window,
                         struct SDL_SysWMinfo* info) {
  if (info && info->version.major >= SDL_MAJOR_VERSION) {
    info->subsystem = SDL_SYSWM_UNKNOWN;
    return SDL_TRUE;
  }
  return SDL_FALSE;
}

int ShowMessageBoxImpl(const SDL_MessageBoxData* messageboxdata,
                       int* buttonid) {
  if (!messageboxdata) return -1;
  std::string title = messageboxdata->title ? messageboxdata->title : "Message";
  std::string message = messageboxdata->message ? messageboxdata->message : "";

  struct State {
    bool done = false;
    int selected_id = -1;
  };
  auto state = std::make_shared<State>();

  std::vector<std::shared_ptr<perception::ui::Node>> button_nodes;
  for (int i = 0; i < messageboxdata->numbuttons; ++i) {
    int btn_id = messageboxdata->buttons[i].buttonid;
    std::string btn_text = messageboxdata->buttons[i].text
                               ? messageboxdata->buttons[i].text
                               : "OK";
    button_nodes.push_back(perception::ui::components::Button::TextButton(
        btn_text,
        [state, btn_id]() {
          state->selected_id = btn_id;
          state->done = true;
        },
        [](perception::ui::Layout& layout) { layout.SetFlexGrow(1.0f); }));
  }

  auto dialog = perception::ui::components::UiWindow::DialogWithTitleBar(
      title,
      [state](perception::ui::components::UiWindow& win) {
        win.OnClose([state]() { state->done = true; });
      },
      [](perception::ui::Layout& layout) {
        layout.SetPadding(YGEdgeAll, 16.0f);
        layout.SetGap(16.0f);
        layout.SetWidth(320.0f);
      },
      perception::ui::components::Label::BasicLabel(message),
      perception::ui::components::Container::HorizontalContainer(
          [](perception::ui::Layout& layout) { layout.SetGap(8.0f); },
          button_nodes));

  while (!state->done) perception::WaitForMessagesThenReturn();
  if (buttonid) *buttonid = state->selected_id;
  return 0;
}

int ShowMessageBoxDevice(_THIS, const SDL_MessageBoxData* messageboxdata,
                         int* buttonid) {
  return ShowMessageBoxImpl(messageboxdata, buttonid);
}

void SetWindowMinimumSize(_THIS, SDL_Window* window) {
  auto data = (PerceptionWindowData*)window->driverdata;
  if (data && data->ui_window && data->ui_window->base_window_) {
    if (window->min_w <= 0 && window->min_h <= 0) {
      data->ui_window->base_window_->SetMinimumSize(std::nullopt);
    } else {
      data->ui_window->base_window_->SetMinimumSize(
          perception::window::Size(static_cast<float>(window->min_w),
                                   static_cast<float>(window->min_h)));
    }
  }
}

void SetWindowMaximumSize(_THIS, SDL_Window* window) {
  auto data = (PerceptionWindowData*)window->driverdata;
  if (data && data->ui_window && data->ui_window->base_window_) {
    if (window->max_w <= 0 && window->max_h <= 0) {
      data->ui_window->base_window_->SetMaximumSize(std::nullopt);
    } else {
      data->ui_window->base_window_->SetMaximumSize(
          perception::window::Size(static_cast<float>(window->max_w),
                                   static_cast<float>(window->max_h)));
    }
  }
}

void MaximizeWindow(_THIS, SDL_Window* window) {
  auto data = (PerceptionWindowData*)window->driverdata;
  if (data && data->ui_window && data->ui_window->base_window_) {
    data->ui_window->base_window_->SetFullScreen(true);
  }
}

void RestoreWindow(_THIS, SDL_Window* window) {
  auto data = (PerceptionWindowData*)window->driverdata;
  if (data && data->ui_window && data->ui_window->base_window_) {
    data->ui_window->base_window_->SetFullScreen(false);
  }
}

void MinimizeWindow(_THIS, SDL_Window* window) {}

void SetWindowBordered(_THIS, SDL_Window* window, SDL_bool bordered) {}

void DeleteDevice(SDL_VideoDevice* device) { SDL_free(device); }

}  // namespace

extern "C" {

SDL_VideoDevice* PERCEPTION_CreateDevice(void) {
  SDL_VideoDevice* device =
      (SDL_VideoDevice*)SDL_calloc(1, sizeof(SDL_VideoDevice));
  if (!device) return nullptr;

  device->VideoInit = VideoInit;
  device->VideoQuit = VideoQuit;
  device->CreateSDLWindow = CreateWindow;
  device->ShowWindow = ShowWindow;
  device->HideWindow = HideWindow;
  device->RaiseWindow = RaiseWindow;
  device->SetWindowTitle = SetWindowTitle;
  device->SetWindowSize = SetWindowSize;
  device->SetWindowMinimumSize = SetWindowMinimumSize;
  device->SetWindowMaximumSize = SetWindowMaximumSize;
  device->MaximizeWindow = MaximizeWindow;
  device->MinimizeWindow = MinimizeWindow;
  device->RestoreWindow = RestoreWindow;
  device->SetWindowBordered = SetWindowBordered;
  device->GetWindowSizeInPixels = GetWindowSizeInPixels;
  device->SetWindowFullscreen = SetWindowFullscreen;
  device->SetWindowMouseGrab = SetWindowMouseGrab;
  device->SetWindowKeyboardGrab = SetWindowKeyboardGrab;
  device->SetWindowInputFocus = SetWindowInputFocus;
  device->DestroyWindow = DestroyWindow;
  device->CreateWindowFramebuffer = CreateWindowFramebuffer;
  device->UpdateWindowFramebuffer = UpdateWindowFramebuffer;
  device->DestroyWindowFramebuffer = DestroyWindowFramebuffer;
  device->PumpEvents = PumpEvents;
  device->SetClipboardText = SetClipboardText;
  device->GetClipboardText = GetClipboardText;
  device->HasClipboardText = HasClipboardText;
  device->GetDisplayBounds = GetDisplayBounds;
  device->GetDisplayUsableBounds = GetDisplayUsableBounds;
  device->ShowMessageBox = ShowMessageBoxDevice;
  device->GetWindowWMInfo = GetWindowWMInfo;

  device->free = DeleteDevice;
  return device;
}

VideoBootStrap PERCEPTION_bootstrap = {
    "perception", "Perception OS Video Driver", PERCEPTION_CreateDevice,
    ShowMessageBoxImpl};

}  // extern "C"
