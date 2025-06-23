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
#pragma once

#include <string>
#include <vector>

#include "perception/serialization/serializable.h"
#include "perception/service_macros.h"
#include "perception/shared_memory.h"

namespace perception {
namespace serialization {
class Serializer;
}

namespace ui {
class FontStyle : public serialization::Serializable {
  public:
  enum class Weight {
    THIN = 1,
    EXTRALIGHT = 40,
    LIGHT = 50,
    SEMILIGHT = 55,
    BOOK = 75,
    REGULAR = 80,
    MEDIUM = 100,
    SEMIBOLD = 180,
    BOLD = 200,
    EXTRABOLD = 205,
    BLACK = 210,
    EXTRABLACK = 215
  };
  Weight weight;

  enum class Width {
    ULTRACONDENSED = 50,
    EXTRACONDENSED = 63,
    CONDENSED = 75,
    SEMICONDENSED = 87,
    NORMAL = 100,
    SEMIEXPANDED = 113,
    EXPANDED = 125,
    EXTRAEXPANDED = 150,
    ULTRAEXPANDED = 200
  };
  Width width;

  enum class Slant { UPRIGHT = 1, ITALIC = 2, OBLIQUE = 3 };
  Slant slant;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class FontData : public serialization::Serializable {
 public:
  enum class Type { FILE, BUFFER };
  Type type;

  // If type is FILE.
  std::string path;

  // If type is BUFFER.
  std::shared_ptr<SharedMemory> buffer;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class MatchFontRequest : public serialization::Serializable {
 public:
  std::string family_name;
  FontStyle style;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class MatchFontResponse : public serialization::Serializable {
 public:
  std::string family_name;
  FontData data;
  FontStyle style;
  int32 face_index;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class FontFamily : public serialization::Serializable {
  public:
  std::string name;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class FontFamilies : public serialization::Serializable {
  std::vector<FontFamilies> families;

  virtual void Serialize(serialization::Serializer& serializer) override;

};

class FontStyles : public serialization::Serializable {
 public:
  std::vector<FontStyle> styles;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

#define METHOD_LIST(X)                                \
  X(1, MatchFont,MatchFontResponse, MatchFontRequest)       \
  X(2, GetFontFamilies, FontFamilies, void)        \
  X(3, GetFontFamilyStyles, FontStyles, FontFamily)

DEFINE_PERCEPTION_SERVICE(FontManager, "perception.ui.FontManager",
                          METHOD_LIST)

#undef METHOD_LIST

}  // namespace ui
}  // namespace perception