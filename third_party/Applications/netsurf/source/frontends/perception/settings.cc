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
#include "gui.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory>
#include <string>

extern "C" {
#include "netsurf/misc.h"
#include "utils/filepath.h"
#include "utils/nsoption.h"
}

#include "perception/registry.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/checkbox.h"
#include "perception/ui/node.h"
#include "perception/ui/layout.h"

using ::perception::ui::Node;
using ::perception::ui::Layout;
using ::perception::ui::Point;
using ::perception::ui::components::UiWindow;
using ::perception::ui::components::InputBox;
using ::perception::ui::components::Button;
using ::perception::ui::components::Container;
using ::perception::ui::components::Label;
using ::perception::ui::components::Checkbox;

namespace netsurf {
namespace perception {

void LoadSettingsFromRegistry() {
	// Homepage
	auto val = ::perception::GetRegistryValue(
		::perception::RegistryCorpus::APPLICATIONS, "netsurf", "homepage");
	if (val.Ok() && (*val).StringValue()) {
		nsoption_set_charp(homepage_url, strdup(std::string(*(*val).StringValue()).c_str()));
	}

	// JS
	val = ::perception::GetRegistryValue(
		::perception::RegistryCorpus::APPLICATIONS, "netsurf", "enable_javascript");
	if (val.Ok() && (*val).BoolValue()) {
		nsoption_set_bool(enable_javascript, *(*val).BoolValue());
	}

	// Load images
	val = ::perception::GetRegistryValue(
		::perception::RegistryCorpus::APPLICATIONS, "netsurf", "load_images");
	if (val.Ok() && (*val).BoolValue()) {
		bool enabled = *(*val).BoolValue();
		nsoption_set_bool(foreground_images, enabled);
		nsoption_set_bool(background_images, enabled);
	}

	// Block Ads
	val = ::perception::GetRegistryValue(
		::perception::RegistryCorpus::APPLICATIONS, "netsurf", "block_advertisements");
	if (val.Ok() && (*val).BoolValue()) {
		nsoption_set_bool(block_advertisements, *(*val).BoolValue());
	}

	// DNT
	val = ::perception::GetRegistryValue(
		::perception::RegistryCorpus::APPLICATIONS, "netsurf", "do_not_track");
	if (val.Ok() && (*val).BoolValue()) {
		nsoption_set_bool(do_not_track, *(*val).BoolValue());
	}
}

}  // namespace perception
}  // namespace netsurf
