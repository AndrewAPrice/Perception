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

#include "perception/clipboard.h"
#include "perception/serialization/value.h"

class ClipboardServer : public ::perception::Clipboard::Server {
 public:
  ClipboardServer();
  virtual ~ClipboardServer();

  virtual ::perception::Status SetClipboard(
      const ::perception::serialization::Value& input) override;
  virtual StatusOr<::perception::serialization::Value> GetClipboard() override;

 private:
  ::perception::serialization::Value clipboard_value_;
};
