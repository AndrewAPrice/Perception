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

#include "include/codec/SkPngDecoder.h"

namespace SkPngDecoder {
bool IsPng(const void *data, size_t len) { return false; }

std::unique_ptr<SkCodec> Decode(std::unique_ptr<SkStream> stream,
                                SkCodec::Result *result,
                                SkCodecs::DecodeContext ctx) {
  if (result)
    *result = SkCodec::Result::kUnimplemented;
  return nullptr;
}

std::unique_ptr<SkCodec> Decode(sk_sp<const SkData> data,
                                SkCodec::Result *result,
                                SkCodecs::DecodeContext ctx) {
  if (result)
    *result = SkCodec::Result::kUnimplemented;
  return nullptr;
}
} // namespace SkPngDecoder
