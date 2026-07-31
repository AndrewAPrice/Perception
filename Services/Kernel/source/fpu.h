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

#pragma once

#include "types.h"

// Type alias for an opaque 64-byte aligned FPU/SIMD register save area pointer.
typedef void FpuRegisters;

// Initializes CPU FPU and SIMD state saving (XSAVE/FXSAVE) and the save area
// pool.
void InitializeFpu();

// Allocates a 64-byte aligned FPU save area from the object pool.
FpuRegisters* AllocateFpuSaveArea();

// Releases an FPU save area back to the object pool.
void ReleaseFpuSaveArea(FpuRegisters* buffer);

// Saves the current FPU/SIMD state into the 64-byte aligned thread buffer.
void SaveFpuState(FpuRegisters* buffer);

// Restores FPU/SIMD state from the 64-byte aligned thread buffer.
void RestoreFpuState(FpuRegisters* buffer);
