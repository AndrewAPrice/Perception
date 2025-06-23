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

#include "perception/loader.h"

#include "perception/serialization/serializer.h"

namespace perception {

void LoadApplicationRequest::Serialize(
    serialization::Serializer& serializer) {
  serializer.String("Name", name);
}

void LoadApplicationResponse::Serialize(
    serialization::Serializer& serializer) {
  serializer.Integer("Process", process);
}

}  // namespace perception