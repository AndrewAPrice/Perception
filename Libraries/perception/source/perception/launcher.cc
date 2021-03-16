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

#include "perception/launcher.h"

#include "permebuf/Libraries/perception/launcher.permebuf.h"

using ::permebuf::perception::Launcher;

namespace perception {

std::optional<ProcessId> LaunchApplication(std::string_view name) {
	Permebuf<Launcher::LaunchApplicationRequest> request;
	request->SetName(name);
	auto status_or_response = Launcher::Get().CallLaunchApplication(
		std::move(request));
	if (status_or_response) {
		return static_cast<ProcessId>(status_or_response->GetProcessId());
	} else {
		return std::nullopt;
	}
}

void ShowLauncher() {
	Launcher::Get().SendShowLauncher(
		Launcher::ShowLauncherMessage());
}

}
