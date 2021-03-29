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

#include <vector>

#include "ide_storage_device.h"
#include "types.h"

struct IdeChannelRegisters {
	uint16 io_base; /* I/O base */
	uint16 control_base; /* control base */
	uint16 bus_master_id; /* bus master ide */
	uint8 no_interrupt; /* no interrupt */
};

struct IdeController;

struct IdeDevice {
	bool primary_channel;
	bool master_drive;
	uint16 type;
	uint16 signature;
	uint16 capabilities;
	uint32 command_sets; /* supported command sets */
	uint32 size; /* size in sectors */
	uint64 size_in_bytes;
	bool is_writable;
	std::string name;
	// unsigned char model[41]; /* model string */
	IdeController* controller;
	std::unique_ptr<IdeStorageDevice> storage_device;
};

struct IdeController {
	struct IdeChannelRegisters channels[2];
	std::vector<std::unique_ptr<IdeDevice>> devices;
};