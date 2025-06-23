// Copyright 2025 Google LLC
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
#include "perception/ui/font_manager.h"

#include "perception/serialization/serializer.h"

namespace perception {
namespace ui {

void FontStyle::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Weight", weight);
  serializer.Integer("Width", width);
  serializer.Integer("Slant", slant);
}

void FontData::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Type", type);
  serializer.String("Path", path);
  serializer.Serializable("Buffer", buffer);
}

void MatchFontRequest::Serialize(serialization::Serializer& serializer) {
  serializer.String("Family name", family_name);
  serializer.Serializable("Style", style);
}

void MatchFontResponse::Serialize(serialization::Serializer& serializer) {
  serializer.String("Family name", family_name);
  serializer.Serializable("Data", data);
  serializer.Serializable("Style", style);
  serializer.Integer("Face index", face_index);
}

void FontFamily::Serialize(serialization::Serializer& serializer) {
  serializer.String("Name", name);
}

void FontFamilies::Serialize(serialization::Serializer& serializer) {
  serializer.ArrayOfSerializables("Families", families);
}

void FontStyles::Serialize(serialization::Serializer& serializer) {
  serializer.ArrayOfSerializables("Font styles", styles);
}

}  // namespace ui
}  // namespace perception