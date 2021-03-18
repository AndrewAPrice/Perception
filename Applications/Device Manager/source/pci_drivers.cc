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

#include "pci_drivers.h"

#include "driver_loader.h"

bool LoadPciDriver(uint8 base_class, uint8 sub_class, uint8 prog_if,
	uint16 vendor_id, uint16 device_id, uint8 bus, uint8 slot, uint8 function) {
	switch (base_class) {
		case 0x01: // Mass storage controller
			switch(sub_class) {
				case 0x01:
					AddDriverToLoad("IDE Controller");
					return true;
				default: return false;
			}
		default: return false;
	}
}