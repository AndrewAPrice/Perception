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

#include <string_view>

#include "types.h"

struct stb_fontchar;

namespace perception {

enum class FontFace {
	DejaVuSans = 0
};

class Font {
public:
	Font(uint8* font_bitmap, stb_fontchar* font_chars);
	~Font();

	int GetHeight();

	int MeasureString(std::string_view string);

	void DrawString(int x, int y, std::string_view string,
		uint32 color, uint32* buffer, int buffer_width,
		int buffer_height);

	static Font* LoadFont(FontFace font_face);

private:

	uint8* font_bitmap_;
	stb_fontchar* font_chars_;
};

}
