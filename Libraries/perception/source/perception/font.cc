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

#include "perception/font.h"

#include <iostream>
#include <map>

#define STB_FONTCHAR__TYPEDEF
struct stb_fontchar
{
    // coordinates if using integer positioning
    unsigned short s0,t0,s1,t1;
    signed short x0,y0,x1,y1;
    int advance_int;
};

// Include each font here:
#include "../../../third_party/DejaVuSans.inl"

namespace perception {
namespace {

constexpr int kFontHeight = 8;

std::map<FontFace, std::unique_ptr<Font>> fonts;
Font* ui_font = nullptr;

}

Font::Font(uint8* font_bitmap, stb_fontchar* font_chars) :
	font_bitmap_(font_bitmap), font_chars_(font_chars) {}

Font::~Font() {
	delete font_bitmap_;
	delete font_chars_;
}

int Font::GetHeight() {
	return kFontHeight;
}

int Font::MeasureString(std::string_view string) {
	int length = 0;
	for (int i = 0; i < string.size(); i++) {
		if (string[i] >= STB_FONT_DejaVuSans_FIRST_CHAR) {
			length += font_chars_[string[i] - STB_FONT_DejaVuSans_FIRST_CHAR].
				advance_int;
		}
	}
	return length;
}

void Font::DrawString(int x, int y, std::string_view string,
	uint32 color, uint32* buffer, int buffer_width,
	int buffer_height) {
	uint8 *color_components = (uint8 *)&color;
	for (int i = 0; i < string.size(); i++) {
		if(string[i] < STB_FONT_DejaVuSans_FIRST_CHAR)
			continue;

		stb_fontchar *font_char = &font_chars_[string[i] - STB_FONT_DejaVuSans_FIRST_CHAR];

		int in_x = font_char->s0;
		int in_y = font_char->t0;
		int end_in_x = font_char->s1;
		int end_in_y = font_char->t1;

		int out_x = x + font_char->x0;
		int out_y = y + font_char->y0;

		int in_indx = in_y * STB_FONT_DejaVuSans_BITMAP_WIDTH + in_x;
		int out_indx = out_y * buffer_width + out_x;

		for(int y = in_y; y < end_in_y; y++) {
			int next_in_indx = in_indx + STB_FONT_DejaVuSans_BITMAP_WIDTH;
			int next_out_indx = out_indx + buffer_width;

			for(int x = in_x; x < end_in_x; x++) {
				if (x >= 0 && x < buffer_width &&
					y >= 0 && y < buffer_height) {
					uint8 c = font_bitmap_[in_indx];
					if(c) {
						if (c == 255) {
							buffer[out_indx] = color;
						} else {
							size_t alpha = c;
							size_t inv_alpha = 255 - c;

							uint8 *sc_buf = (uint8 *)(&buffer[out_indx]);
							sc_buf[0] = 0xFF;
							sc_buf[1] = (uint8)((alpha * (int)color_components[1] + inv_alpha * (int)sc_buf[1]) >> 8);
							sc_buf[2] = (uint8)((alpha * (int)color_components[2] + inv_alpha * (int)sc_buf[2]) >> 8);
							sc_buf[3] = (uint8)((alpha * (int)color_components[3] + inv_alpha * (int)sc_buf[3]) >> 8);
						}
					}
				}
				in_indx++;
				out_indx++;
			}

			in_indx = next_in_indx;
			out_indx = next_out_indx;
		}

		// Move to the next position.
		x += font_char->advance_int;
	}
}

Font* Font::LoadFont(FontFace font_face) {
	auto font_itr = fonts.find(font_face);
	if (font_itr != fonts.end()) {
		return font_itr->second.get();
	}

	uint8* font_bitmap;
	stb_fontchar* font_chars;
	std::unique_ptr<Font> font;

	switch (font_face) {
		case FontFace::DejaVuSans:
			font_bitmap = new uint8[
				STB_FONT_DejaVuSans_BITMAP_HEIGHT *
				STB_FONT_DejaVuSans_BITMAP_WIDTH];
			font_chars = new stb_fontchar[
				STB_FONT_DejaVuSans_NUM_CHARS];
			stb_font_DejaVuSans(font_chars,
				(uint8(*)[STB_FONT_DejaVuSans_BITMAP_WIDTH])
				font_bitmap, kFontHeight);
			font = std::make_unique<Font>(font_bitmap, font_chars);
			break;
		default:
			return nullptr;
	}

	Font* to_return = font.get();
	fonts[font_face] = std::move(font);
	return to_return;
}


Font* GetUiFont() {
	if (ui_font == nullptr) {
		ui_font = Font::LoadFont(FontFace::DejaVuSans);
	}

	return ui_font;
}

}
