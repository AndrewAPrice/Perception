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
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
#include "utils/errors.h"
#include "netsurf/cookie_db.h"
#include "netsurf/misc.h"
#include "utils/log.h"
#include "utils/nsoption.h"
}

#include "perception/fibers.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/time.h"
#include "window.h"

namespace netsurf {
namespace perception {
namespace {

std::string internal_feurl = "https://www.google.com";

struct ScheduledTimer {
  void (*callback)(void* p);
  void* p;
  bool cancelled;
};

std::vector<std::shared_ptr<ScheduledTimer>> active_timers;

nserror ScheduleTimer(int tival, void (*callback)(void* p), void* p) {
  // Unschedule/cancel any existing timer matching callback and p.
  for (auto& timer : active_timers) {
    if (timer->callback == callback && timer->p == p) {
      timer->cancelled = true;
    }
  }

  // Clean up cancelled/inactive timers.
  active_timers.erase(
      std::remove_if(active_timers.begin(), active_timers.end(),
                     [](const std::shared_ptr<ScheduledTimer>& t) {
                       return t->cancelled;
                     }),
      active_timers.end());

  // If tival is negative, it is an unschedule request, so just return!
  if (tival < 0) return NSERROR_OK;

  auto timer = std::make_shared<ScheduledTimer>();
  timer->callback = callback;
  timer->p = p;
  timer->cancelled = false;
  active_timers.push_back(timer);

  if (tival == 0) {
    ::perception::Defer([timer]() {
      if (!timer->cancelled) {
        NETSURF_LOCK;
        timer->callback(timer->p);
      }
    });
  } else {
    ::perception::Defer([tival, timer]() {
      ::perception::SleepForDuration(std::chrono::milliseconds(tival));
      if (!timer->cancelled) {
        NETSURF_LOCK;
        timer->callback(timer->p);
      }
    });
  }
  return NSERROR_OK;
}

}  // namespace

const char* GetInitialUrl() { return internal_feurl.c_str(); }
void SetInitialUrl(const char* url) {
  if (url) internal_feurl = url;
}

void gui_quit(void) {
  NSLOG(netsurf, INFO, "gui_quit");
  if (nsoptions && nsoption_charp(cookie_jar)) {
    urldb_save_cookies(nsoption_charp(cookie_jar));
  }
  ::perception::TerminateProcess();
}

struct gui_misc_table perception_misc_table = {
    .schedule = ScheduleTimer,
    .quit = gui_quit,
};

}  // namespace perception
}  // namespace netsurf
