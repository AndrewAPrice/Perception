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

#include "settings.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

extern "C" {
#include "utils/nsoption.h"
}

#include "perception/registry.h"

namespace netsurf {
namespace perception {

void LoadSettingsFromRegistry() {
  auto val = ::perception::GetRegistryValue("homepage");
  if (val.Ok() && (*val).StringValue()) {
    nsoption_set_charp(homepage_url,
                       strdup(std::string(*(*val).StringValue()).c_str()));
  }

  val = ::perception::GetRegistryValue("enable_javascript");
  if (val.Ok() && (*val).BoolValue())
    nsoption_set_bool(enable_javascript, *(*val).BoolValue());

  val = ::perception::GetRegistryValue("load_images");
  if (val.Ok() && (*val).BoolValue()) {
    bool enabled = *(*val).BoolValue();
    nsoption_set_bool(foreground_images, enabled);
    nsoption_set_bool(background_images, enabled);
  }

  val = ::perception::GetRegistryValue("block_advertisements");
  if (val.Ok() && (*val).BoolValue())
    nsoption_set_bool(block_advertisements, *(*val).BoolValue());

  val = ::perception::GetRegistryValue("do_not_track");
  if (val.Ok() && (*val).BoolValue())
    nsoption_set_bool(do_not_track, *(*val).BoolValue());
}

}  // namespace perception
}  // namespace netsurf
