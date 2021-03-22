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

#pragma once

#include "permebuf/Libraries/perception/devices/storage_device.permebuf.h"

class IdeDevice;

class IdeStorageDevice : public ::permebuf::perception::devices::StorageDevice::Server {
public:
	typedef ::permebuf::perception::devices::StorageDevice SD;

	IdeStorageDevice(IdeDevice* device);
	virtual ~IdeStorageDevice() {}

	StatusOr<Permebuf<SD::GetDeviceDetailsResponse>>
		HandleGetDeviceDetails(::perception::ProcessId sender,
		const SD::GetDeviceDetailsRequest& request) override;

	StatusOr<SD::ReadResponse> HandleRead(
		::perception::ProcessId sender,
		const SD::ReadRequest& request) override;

private:
	IdeDevice* device_;

};
