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

#include "misc.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
#include "utils/errors.h"
#include "content/urldb.h"
#include "netsurf/cookie_db.h"
#include "netsurf/misc.h"
#include "utils/log.h"
#include "utils/nsoption.h"
#include "utils/nsurl.h"
}

#include "managers.h"
#include "perception/fibers.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/time.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/message_dialog.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "tabs.h"
#include "window.h"

namespace {

// Default startup URL opened when no URL argument is provided.
constexpr char kDefaultInitialUrl[] = "about:welcome";

// Width of the HTTP authentication login dialog in pixels.
constexpr float kLoginDialogWidth = 380.0f;

// Height of the HTTP authentication login dialog in pixels.
constexpr float kLoginDialogHeight = 220.0f;

// Width of field labels in the HTTP authentication dialog.
constexpr float kLoginLabelWidth = 80.0f;

}  // namespace

namespace netsurf {
namespace perception {
namespace {

using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::components::Button;
using ::perception::ui::components::Container;
using ::perception::ui::components::InputBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::ShowMessageDialog;
using ::perception::ui::components::UiWindow;

std::string internal_feurl = kDefaultInitialUrl;

struct ScheduledTimer {
  void (*callback)(void* p);
  void* p;
  std::atomic<bool> cancelled{false};
};

std::mutex active_timers_mutex;
std::vector<std::shared_ptr<ScheduledTimer>> active_timers;

nserror ScheduleTimer(int tival, void (*callback)(void* p), void* p) {
  std::shared_ptr<ScheduledTimer> timer;
  {
    std::scoped_lock lock(active_timers_mutex);
    for (auto& t : active_timers) {
      if (t->callback == callback && t->p == p)
        t->cancelled = true;
    }

    active_timers.erase(
        std::remove_if(active_timers.begin(), active_timers.end(),
                       [](const std::shared_ptr<ScheduledTimer>& t) {
                         return t->cancelled.load();
                       }),
        active_timers.end());

    if (tival < 0)
      return NSERROR_OK;

    timer = std::make_shared<ScheduledTimer>();
    timer->callback = callback;
    timer->p = p;
    timer->cancelled = false;
    active_timers.push_back(timer);
  }

  if (tival == 0) {
    ::perception::Defer([timer]() {
      {
        NETSURF_LOCK;
        if (!timer->cancelled)
          timer->callback(timer->p);
      }
      timer->cancelled = true;
    });
  } else {
    ::perception::Defer([tival, timer]() {
      ::perception::SleepForDuration(std::chrono::milliseconds(tival));
      {
        NETSURF_LOCK;
        if (!timer->cancelled)
          timer->callback(timer->p);
      }
      timer->cancelled = true;
    });
  }
  return NSERROR_OK;
}

nserror LaunchUrl(struct nsurl* url) {
  std::string url_str =
      (url != nullptr && nsurl_access(url) != nullptr) ? nsurl_access(url) : "";
  ShowMessageDialog("External URL",
                    "Cannot open external URL scheme: " + url_str,
                    GetGlobalUiWindow());
  return NSERROR_OK;
}

nserror LoginPrompt(struct nsurl* url, const char* realm,
                    const char* username, const char* password,
                    nserror (*cb)(struct nsurl* url, const char* realm,
                                  const char* username,
                                  const char* password, void* pw),
                    void* cbpw) {
  if (url == nullptr || cb == nullptr)
    return NSERROR_INVALID;

  std::shared_ptr<struct nsurl> retained_url(
      nsurl_ref(url), [](struct nsurl* u) {
        if (u != nullptr)
          nsurl_unref(u);
      });

  std::string realm_str = realm != nullptr ? realm : "Restricted Area";
  std::string url_str =
      nsurl_access(url) != nullptr ? nsurl_access(url) : "";
  std::string initial_user = username != nullptr ? username : "";
  std::string initial_pass = password != nullptr ? password : "";

  std::shared_ptr<InputBox> user_box;
  auto user_node = InputBox::BasicInputBox(
      initial_user, &user_box,
      [](Layout& layout) { layout.SetFlexGrow(1.0f); });

  std::shared_ptr<InputBox> pass_box;
  auto pass_node = InputBox::BasicInputBox(
      initial_pass, &pass_box,
      [](Layout& layout) { layout.SetFlexGrow(1.0f); });

  auto responded = std::make_shared<bool>(false);
  auto window_holder = std::make_shared<std::shared_ptr<Node>>();
  auto close_dialog = [window_holder]() {
    auto node = *window_holder;
    if (!node)
      return;
    if (auto uw = node->Get<UiWindow>())
      uw->Close();
    ::perception::Defer([window_holder]() { window_holder->reset(); });
  };

  auto sign_in_button = Button::TextButton(
      "Sign In",
      [retained_url, realm_str, user_box, pass_box, cb, cbpw, responded,
       close_dialog]() {
        if (*responded)
          return;
        *responded = true;
        std::string user = user_box ? user_box->GetText() : "";
        std::string pass = pass_box ? pass_box->GetText() : "";
        std::string auth = user + ":" + pass;
        {
          NETSURF_LOCK;
          urldb_set_auth_details(retained_url.get(), realm_str.c_str(),
                                 auth.c_str());
          cb(retained_url.get(), realm_str.c_str(), user.c_str(), pass.c_str(),
             cbpw);
        }
        close_dialog();
      },
      [](Button& button) {
        button.SetButtonStyle(Button::ButtonStyle::PRIMARY);
      });

  auto cancel_button = Button::TextButton(
      "Cancel", [responded, close_dialog]() {
        if (*responded)
          return;
        *responded = true;
        close_dialog();
      });

  auto dialog = UiWindow::DialogWithTitleBar(
      "Authentication Required", UiWindow::Parent(GetGlobalUiWindow()),
      [responded, window_holder](UiWindow& window) {
        window.OnClose([responded, window_holder]() {
          *responded = true;
          ::perception::Defer([window_holder]() { window_holder->reset(); });
        });
      },
      [](Layout& layout) {
        layout.SetWidth(kLoginDialogWidth);
        layout.SetHeight(kLoginDialogHeight);
      },
      Label::SingleLineTruncated("Sign in to " + url_str + " (" + realm_str +
                                 ")"),
      Container::HorizontalContainer(
          [](Layout& layout) {
            layout.SetWidthPercent(100.0f);
            layout.SetAlignItems(YGAlignCenter);
          },
          Label::BasicLabel(
              "Username:",
              [](Layout& layout) { layout.SetWidth(kLoginLabelWidth); }),
          user_node),
      Container::HorizontalContainer(
          [](Layout& layout) {
            layout.SetWidthPercent(100.0f);
            layout.SetAlignItems(YGAlignCenter);
          },
          Label::BasicLabel(
              "Password:",
              [](Layout& layout) { layout.SetWidth(kLoginLabelWidth); }),
          pass_node),
      Node::Empty([](Layout& layout) { layout.SetFlexGrow(1.0f); }),
      Container::HorizontalContainer(
          [](Layout& layout) {
            layout.SetWidthPercent(100.0f);
            layout.SetJustifyContent(YGJustifyFlexEnd);
            layout.SetAlignItems(YGAlignCenter);
          },
          cancel_button, sign_in_button));

  *window_holder = dialog;
  return NSERROR_OK;
}

nserror PresentCookies(const char* search_term) {
  ShowCookieManagerWindow(search_term);
  return NSERROR_OK;
}

}  // namespace

const char* GetInitialUrl() { return internal_feurl.c_str(); }
void SetInitialUrl(const char* url) {
  if (url)
    internal_feurl = url;
}

void gui_quit(void) {
  NSLOG(netsurf, INFO, "gui_quit");
  FinalizeManagers();
  if (nsoptions && nsoption_charp(cookie_jar))
    urldb_save_cookies(nsoption_charp(cookie_jar));
  ::perception::TerminateProcess();
}

struct gui_misc_table perception_misc_table = {
    .schedule = ScheduleTimer,
    .quit = gui_quit,
    .launch_url = LaunchUrl,
    .login = LoginPrompt,
    .present_cookies = PresentCookies,
};

}  // namespace perception
}  // namespace netsurf
