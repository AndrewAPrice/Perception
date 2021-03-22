// Copyright 2020 Google LLC
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

#include "perception/framebuffer.h"

#include "perception/messages.h"
#include "perception/memory.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/shared_memory.h"
#include "permebuf/Libraries/perception/devices/graphics_driver.permebuf.h"

#include <iostream>
#include <map>
#include <set>

// #define DEBUG

using ::perception::Fiber;
using ::perception::GetMultibootFramebufferDetails;
using ::perception::kPageSize;
using ::perception::MapPhysicalMemory;
using ::perception::MessageId;
using ::perception::NotifyUponProcessTermination;
using ::perception::ProcessId;
using ::perception::SharedMemory;
using ::perception::StopNotifyingUponProcessTermination;
using ::permebuf::perception::devices::GraphicsCommand;
using ::permebuf::perception::devices::GraphicsDriver;

// Beyer ditchering pattern.
constexpr uint8 kDitheringTable[] = {
	0, 48, 12, 60, 3, 51, 15, 63,
	32, 16, 44, 28, 35, 19, 47, 31,
	8, 56, 4, 52, 11, 59, 7, 55,
	40, 24, 36, 20, 43, 27, 39, 23,
	2, 50, 14, 62, 1, 49, 13, 61,
	34, 18, 46, 30, 33, 17, 46, 29,
	10, 58, 6, 54, 9, 57, 5, 53,
	42, 26, 38, 22, 41, 25, 37, 21
	};

constexpr int kDitheringTableWidth = 8;

struct Texture {
	// The owner of the texture.
	ProcessId owner;

	// The width of the texture, in pixels.
	uint32 width;

	// The height of the texture, in pixels.
	uint32 height;

	// The shared buffer.
	std::unique_ptr<SharedMemory> shared_memory;
};

struct ProcessInformation {
	// The listener for handling with the process disappears, so
	// we can release all textures that it owns.
	MessageId on_process_disappear_listener;

	// Textures owned by this process.
	std::set<uint64> textures;
};

struct RenderState {
	// The texture to render to.
	Texture* source_texture = nullptr;

	// The texture to render from.
	Texture* destination_texture = nullptr;
};

class FramebufferGraphicsDriver : GraphicsDriver::Server {
public:
	FramebufferGraphicsDriver(
		size_t physical_address_of_framebuffer,
		uint32 width,
		uint32 height,
		uint32 pitch,
		uint8 bpp) :
		screen_width_(width),
		screen_height_(height),
		screen_pitch_(pitch),
		screen_bits_per_pixel_(bpp),
		framebuffer_(MapPhysicalMemory(physical_address_of_framebuffer,
			(width * pitch + kPageSize - 1) / kPageSize)),
		next_texture_id_(1),
		process_allowed_to_write_to_the_screen_(0) {
		// Create the initial texture, which is the screen buffer.
		Texture texture;
		texture.owner = 0; // 0 = The kernel.
		texture.width = screen_width_;
		texture.height = screen_height_;

		textures_[0] = std::move(texture);
	}

	void HandleRunCommands(
		ProcessId sender,
		Permebuf<GraphicsDriver::RunCommandsMessage> commands) override {
		RenderState render_state;

#ifdef DEBUG
		std::cout << "Start commands" << std::endl;
#endif
		// Run each of the commands.
		for (GraphicsCommand command : commands->GetCommands()) {
			RunCommand(sender, command, render_state);
		}
#ifdef DEBUG
		std::cout << "End commands" << std::endl;
#endif
	}

	StatusOr<GraphicsDriver::EmptyResponse> HandleRunCommandsAndWait(
		ProcessId sender,
		Permebuf<GraphicsDriver::RunCommandsMessage> commands) override {
		HandleRunCommands(sender, std::move(commands));
		return GraphicsDriver::EmptyResponse();
	}

	StatusOr<GraphicsDriver::CreateTextureResponse> HandleCreateTexture(
		ProcessId sender,
		const GraphicsDriver::CreateTextureRequest& request) override {

		// Create the texture.
		uint32 texture_id = next_texture_id_++;
		Texture texture;
		texture.owner = sender;
		texture.width = request.GetWidth();
		texture.height = request.GetHeight();
		texture.shared_memory = SharedMemory::FromSize(texture.width * texture.height * 4);

		// Record what textures this process owns.		
		auto process_information_itr = process_information_.find(sender);
		if (process_information_itr == process_information_.end()) {
			// This process doesn't own any textures.
			ProcessInformation process_information;
			// We want to listen for when the process disappears so we
			// can release all textures that process owns.
			process_information.on_process_disappear_listener =
				NotifyUponProcessTermination(sender, [this, sender]() {
					ReleaseAllResourcesBelongingToProcess(sender);
				});
			process_information.textures.insert(texture_id);
			process_information_[sender] = process_information;
		} else {
			process_information_itr->second.textures.insert(texture_id);
		}

		// Send it back to the client.
		GraphicsDriver::CreateTextureResponse response;
		response.SetTexture(texture_id);
		response.SetPixelBuffer(*texture.shared_memory);

		textures_[texture_id] = std::move(texture);

		return response;
	}

	void HandleDestroyTexture(
		ProcessId sender,
		const GraphicsDriver::DestroyTextureMessage& request) override {
		// Try to find the texture.
		auto texture_itr = textures_.find(request.GetTexture());
		if (texture_itr == textures_.end())
			// We couldn't find the texture.
			return;

		if (texture_itr->second.owner != sender)
			// Only the owner can destroy a texture.
			return;

		textures_.erase(texture_itr);

		auto process_information_itr = process_information_.find(sender);
		if (process_information_itr == process_information_.end())
			// We can't find this process. This shouldn't happen.
			return;

		process_information_itr->second.textures.erase(request.GetTexture());
		if (process_information_itr->second.textures.empty()) {
			// This process owns no more textures. We no longer care about
			// listening for it it disappears.
			StopNotifyingUponProcessTermination(
				process_information_itr->second.on_process_disappear_listener);
			process_information_.erase(process_information_itr);
		}
	}

	StatusOr<GraphicsDriver::GetTextureInformationResponse> HandleGetTextureInformation(
		ProcessId,
		const GraphicsDriver::GetTextureInformationRequest& request) override {
		GraphicsDriver::GetTextureInformationResponse response;
		// Try to find the texture.
		auto texture_itr = textures_.find(request.GetTexture());
		if (texture_itr != textures_.end()) {
			// We found the texture. Respond with details about it.
			response.SetOwner(texture_itr->second.owner);
			response.SetWidth(texture_itr->second.width);
			response.SetHeight(texture_itr->second.height);
		}
		return response;
	}

	void HandleSetProcessAllowedToDrawToScreen(
		ProcessId,
		const GraphicsDriver::SetProcessAllowedToDrawToScreenMessage& request) override {
		// TODO: Implement some kind of security.
		process_allowed_to_write_to_the_screen_ = request.GetProcess();
	}

	StatusOr<GraphicsDriver::GetScreenSizeResponse> HandleGetScreenSize(
		ProcessId,
		const GraphicsDriver::GetScreenSizeRequest& request) override {
		GraphicsDriver::GetScreenSizeResponse response;
		response.SetWidth(screen_width_);
		response.SetHeight(screen_height_);
		return response;
	}

private:
	// The width of the screen, in pixels.
	uint32 screen_width_;

	// The height of the screen, in pixels.
	uint32 screen_height_;

	// Number of bytes between rows of pixels on the screen.
	uint32 screen_pitch_;

	// The number of bits per pixel on the screen.
	uint8 screen_bits_per_pixel_;

	// Pointer to the screen's framebuffer.
	void* framebuffer_;

	// Textures indexed by their IDs.
	std::map<uint64, Texture> textures_;

	// Information about processes that we care about.
	std::map<ProcessId, ProcessInformation> process_information_;

	// The ID of the next texture.
	uint64 next_texture_id_;

	// The process that is allowed to write to the screen.
	ProcessId process_allowed_to_write_to_the_screen_;

	// Handles a graphics command
	void RunCommand(ProcessId sender, const GraphicsCommand& graphics_command,
		RenderState& render_state) {
		switch (graphics_command.GetOption()) {
			case GraphicsCommand::Options::SetDestinationTexture:
#ifdef DEBUG
				std::cout << "Set destination texture to " << 
					graphics_command.GetSetDestinationTexture().GetTexture() << std::endl;
#endif
				SetDestinationTexture(
					sender,
					graphics_command.GetSetDestinationTexture().GetTexture(),
					render_state);
				break;
			case GraphicsCommand::Options::SetSourceTexture:
#ifdef DEBUG
				std::cout << "Set source texture to " << 
					graphics_command.GetSetSourceTexture().GetTexture() << std::endl;
#endif
				SetSourceTexture(
					graphics_command.GetSetSourceTexture().GetTexture(),
					render_state);
				break;
			case GraphicsCommand::Options::FillRectangle: {
				GraphicsCommand::FillRectangle command =
					graphics_command.GetFillRectangle();

				FillRectangle(
					command.GetLeft(),
					command.GetTop(),
					command.GetRight(),
					command.GetBottom(),
					command.GetColor(),
					render_state
				);
				break;
			}
			case GraphicsCommand::Options::CopyEntireTexture: {
				GraphicsCommand::CopyEntireTexture command =
					graphics_command.GetCopyEntireTexture();
				BitBlt(sender,
					render_state,
					/*left_source=*/0,
					/*top_source=*/0,
					/*left_destination=*/0,
					/*top_destination=*/0,
					/*width=*/UINT_MAX,
					/*height=*/UINT_MAX,
					/*alpha_blend=*/false);
				break;
			}
			case GraphicsCommand::Options::CopyEntireTextureWithAlphaBlending: {
				GraphicsCommand::CopyEntireTexture command =
					graphics_command.GetCopyEntireTextureWithAlphaBlending();
				BitBlt(sender,
					render_state,
					/*left_source=*/0,
					/*top_source=*/0,
					/*left_destination=*/0,
					/*top_destination=*/0,
					/*width=*/UINT_MAX,
					/*height=*/UINT_MAX,
					/*alpha_blend=*/true);
				break;
			}
			case GraphicsCommand::Options::CopyTextureToPosition: {
				GraphicsCommand::CopyTextureToPosition command =
					graphics_command.GetCopyTextureToPosition();
				BitBlt(sender,
					render_state,
					/*left_source=*/0,
					/*top_source=*/0,
					command.GetLeftDestination(),
					command.GetTopDestination(),
					/*width=*/UINT_MAX,
					/*height=*/UINT_MAX,
					/*alpha_blend=*/false);
				break;
			}
			case GraphicsCommand::Options::CopyTextureToPositionWithAlphaBlending: {
				GraphicsCommand::CopyTextureToPosition command =
					graphics_command.GetCopyTextureToPositionWithAlphaBlending();
				BitBlt(sender,
					render_state,
					/*left_source=*/0,
					/*top_source=*/0,
					command.GetLeftDestination(),
					command.GetTopDestination(),
					/*width=*/UINT_MAX,
					/*height=*/UINT_MAX,
					/*alpha_blend=*/true);
				break;
			}
			case GraphicsCommand::Options::CopyPartOfATexture: {
				GraphicsCommand::CopyPartOfATexture command =
					graphics_command.GetCopyPartOfATexture();

				BitBlt(sender,
					render_state,
					command.GetLeftSource(),
					command.GetTopSource(),
					command.GetLeftDestination(),
					command.GetTopDestination(),
					command.GetWidth(),
					command.GetHeight(),
					/*alpha_blend=*/false);
				break;
			}
			case GraphicsCommand::Options::CopyPartOfATextureWithAlphaBlending:{
				GraphicsCommand::CopyPartOfATexture command =
					graphics_command.GetCopyPartOfATexture();
				BitBlt(sender,
					render_state,
					command.GetLeftSource(),
					command.GetTopSource(),
					command.GetLeftDestination(),
					command.GetTopDestination(),
					command.GetWidth(),
					command.GetHeight(),
					/*alpha_blend=*/true);
				break;
			}
		}
	}

	void SetDestinationTexture(ProcessId sender, uint64 texture_id,
		RenderState& render_state) {
		auto texture_itr = textures_.find(texture_id);
		if (texture_itr == textures_.end()) {
			render_state.destination_texture = nullptr;
		}
		else {
			// Check if we have permission to write to this texture.
			if (texture_itr->second.owner == 0) {
				// Only one process is allowed to write to the screen's
				// framebuffer.
				if (sender != process_allowed_to_write_to_the_screen_) {
					std::cout << "Not allowed to draw to the screen." << std::endl;
					// We're not that process.
					render_state.destination_texture = nullptr;
					return;
				}
			} else if (texture_itr->second.owner != sender) {
				// We're not the owner of this texture.
				render_state.destination_texture = nullptr;
				return;
			}
			render_state.destination_texture = &texture_itr->second;
		}
	}

	void SetSourceTexture(uint64 texture_id, RenderState& render_state) {
		// We can't copy from the frame buffer.
		if (texture_id == 0) {
			render_state.source_texture = nullptr;
			return;
		}

		auto texture_itr = textures_.find(texture_id);
		if (texture_itr == textures_.end())
			render_state.source_texture = nullptr;
		else
			render_state.source_texture = &texture_itr->second;
	}


	// Bit blit two textures. The BitBlt functions are inlined. The hope is that
	// RunCommand() will be HUGE but all the compiler will optimize away dead code
	// so we'd have a super fast version of each permutation (alpha blend,
	//    no alpha blending, etc.)
	inline void BitBlt(ProcessId sender,
		const RenderState& render_state,
		uint32 left_source,
		uint32 top_source,
		uint32 left_destination,
		uint32 top_destination,
		uint32 width_to_copy,
		uint32 height_to_copy,
		bool alpha_blend) {
#ifdef DEBUG
		std::cout << "Copy texture " <<
			command.GetLeftDestination() << "," <<
			command.GetTopDestination() << " -> " <<
			(command.GetWidth() + command.GetLeftDestination()) << "," <<
			(command.GetHeight() + command.GetTopDestination()) << " @ " <<
			command.GetLeftSource() << "," <<
			command.GetTopSource() << std::endl;
#endif
		if (render_state.source_texture == nullptr ||
			render_state.destination_texture == nullptr) {
			// Nowhere to copy to/from.
			return;
		}

		if (render_state.destination_texture->owner == 0) {
			// We're writing to the screen's frame buffer.

			if (alpha_blend) {
				// It's probably best not to support alpha blending with the
				// framebuffer, because a) reading from the frame buffer could
				// be slow, and b) if we downsample to a lower bit depth, we'd
				// loose precision and it'll be a low quality blend. So it's
				// better if we just don't allow alpha blending with the
				// framebuffer.
				return;
			}
		
			// Call the BitBlt function based on pixel depth of the
			// framebuffer. The ordering is the most likely in my opinion)
			// pixel depths first. We branch on screen_bits_per_pixel_ then
			// call BitBltToTexture with the same value because we want the
			// compiler to inline BitBltToTexture with the pixel depth constant
			// folded.
			if (screen_bits_per_pixel_ == 24) {
				BitBltToTexture(
					(uint8*)**render_state.source_texture->shared_memory,
					render_state.source_texture->width,
					render_state.source_texture->height,
					(uint8*)framebuffer_,
					screen_width_,
					screen_height_,
					screen_pitch_,
					/*destination_bpp=*/24,
					left_source,
					top_source,
					left_destination,
					top_destination,
					width_to_copy,
					height_to_copy,
					/*alpha_blend=*/false);
			} else if (screen_bits_per_pixel_ == 32) {
				BitBltToTexture(
					(uint8*)**render_state.source_texture->shared_memory,
					render_state.source_texture->width,
					render_state.source_texture->height,
					(uint8*)framebuffer_,
					screen_width_,
					screen_height_,
					screen_pitch_,
					/*destination_bpp=*/32,
					left_source,
					top_source,
					left_destination,
					top_destination,
					width_to_copy,
					height_to_copy,
					/*alpha_blend=*/false);
			} else if (screen_bits_per_pixel_ == 16) {
				BitBltToTexture(
					(uint8*)**render_state.source_texture->shared_memory,
					render_state.source_texture->width,
					render_state.source_texture->height,
					(uint8*)framebuffer_,
					screen_width_,
					screen_height_,
					screen_pitch_,
					/*destination_bpp=*/16,
					left_source,
					top_source,
					left_destination,
					top_destination,
					width_to_copy,
					height_to_copy,
					/*alpha_blend=*/false);
			}  if (screen_bits_per_pixel_ == 15) {
				BitBltToTexture(
					(uint8*)**render_state.source_texture->shared_memory,
					render_state.source_texture->width,
					render_state.source_texture->height,
					(uint8*)framebuffer_,
					screen_width_,
					screen_height_,
					screen_pitch_,
					/*destination_bpp=*/15,
					left_source,
					top_source,
					left_destination,
					top_destination,
					width_to_copy,
					height_to_copy,
					/*alpha_blend=*/false);
			} else {
				// Unsupported bits per pixel for the screen.
			}
		} else {
			// We're writing to another texture.
			BitBltToTexture(
				(uint8*)**render_state.source_texture->shared_memory,
				render_state.source_texture->width,
				render_state.source_texture->height,
				(uint8*)**render_state.destination_texture->shared_memory,
				render_state.destination_texture->width,
				render_state.destination_texture->height,
				/*destination_pitch=*/
				render_state.destination_texture->width * 4,
				/*destination_bpp=*/32,
				left_source,
				top_source,
				left_destination,
				top_destination,
				width_to_copy,
				height_to_copy,
				alpha_blend);
		}

	}

	inline void BitBltToTexture(
		uint8* source,
		uint32 source_width,
		uint32 source_height,
		uint8* destination,
		uint32 destination_width,
		uint32 destination_height,
		uint32 destination_pitch,
		uint32 destination_bpp,
		uint32 left_source,
		uint32 top_source,
		uint32 left_destination,
		uint32 top_destination,
		uint32 width_to_copy,
		uint32 height_to_copy,
		bool alpha_blend) {
		if (top_source >= source_height ||
			left_source >= source_width ||
			top_destination >= destination_height ||
			left_destination >= destination_width) {
			// Everything to copy is off screen.
			return;
		}

		// Shrink the copy region if any of it is out of bounds.
		if (top_source + height_to_copy > source_height)
			height_to_copy = source_height - top_source;
		if (top_destination + height_to_copy > destination_height)
			height_to_copy = destination_height - top_destination;
		if (left_source + width_to_copy > source_width)
			width_to_copy = source_width - left_source;
		if (left_destination + width_to_copy > destination_width)
			width_to_copy = destination_width - left_destination;

		if (width_to_copy == 0 || height_to_copy == 0) {
			// Nothing to copy.
			return;
		}

		width_to_copy = std::min(width_to_copy, source_width);
		height_to_copy = std::min(height_to_copy, source_height);

		uint8* source_copy_start =
			&source[(top_source * source_width + left_source) * 4];

		int bytes_per_pixel;
		switch (destination_bpp) {
			case 15:
			case 16:
				bytes_per_pixel = 2;
				break;
			case 24:
				bytes_per_pixel = 3;
				break;
			case 32:
				bytes_per_pixel = 4;
				break;
			break;
		}

		uint8* destination_copy_start =
			&destination[top_destination * destination_pitch +
				left_destination * bytes_per_pixel];

		int y = top_destination;
		// Copy this row.
		for (;height_to_copy > 0; height_to_copy--, y++) {
			source = source_copy_start;
			destination = destination_copy_start;
			int x = left_destination;

			for (uint32 i = width_to_copy; i > 0; i--, x++) {
				switch (destination_bpp) {
					case 32:
						if (!alpha_blend || source[0] == 0xFF) {
							*(uint32*)destination = *(uint32*)source;	
						} else if (source[0] > 0) {
							int alpha = source[0];
							int inv_alpha = 255 - source[0];

							destination[1] = (uint8)((alpha * (int)source[1] + inv_alpha * (int)destination[1]) >> 8);
							destination[2] = (uint8)((alpha * (int)source[2] + inv_alpha * (int)destination[2]) >> 8);
							destination[3] = (uint8)((alpha * (int)source[3] + inv_alpha * (int)destination[3]) >> 8);
						}
						destination += 4;
						break;
					case 24:
						destination[0] = source[1];
						destination[1] = source[2];
						destination[2] = source[3];
						destination += 3;
						break;
					case 16: {
						uint16 dither_val = (uint16)kDitheringTable[
							x % kDitheringTableWidth +
							(y % kDitheringTableWidth) * kDitheringTableWidth];
						// Trim colors down to 5:6:5-bits.
						// Beyer color table is 6-bit (0 to 63).
						// 5-bit color has 32 values (increments of 8).
						// 6-bit color has 64 values (increments of 4).
						// We divide the dither value to be in the range of
						// the color into the next increment.
						uint16 red = std::min(((uint16)source[1] + dither_val / 8) >> (8-5), 31);
						uint16 green = std::min(((uint16)source[2] + dither_val / 4) >> (8-6), 63);
						uint16 blue = std::min(((uint16)source[3] + dither_val / 8) >> (8-5), 31);
						*(uint16*)destination =
							(blue << 11) | (green << 5) | red;
						destination += 2;
						break;
					}
					case 15: {
						uint16 dither_val = (uint16)kDitheringTable[
							x % kDitheringTableWidth +
							(y % kDitheringTableWidth) * kDitheringTableWidth];
						// Trim colors down to 5:5:5-bits.
						uint16 red = std::min(((uint16)source[1] + dither_val / 8) >> (8-5), 31);
						uint16 green = std::min(((uint16)source[2] + dither_val / 8) >> (8-5), 31);
						uint16 blue = std::min(((uint16)source[3] + dither_val / 8) >> (8-5), 31);

						*(uint16*)destination =
							(blue << 10) | (green << 5) | red;
						destination += 2;
						break;
					}
				}
				source += 4;
			}

			// More the start pointers to the next row.
			source_copy_start += source_width * 4;
			destination_copy_start += destination_pitch;
		}
	}

	void FillRectangle(
		uint32 left,
		uint32 top,
		uint32 right,
		uint32 bottom,
		uint32 color,
		RenderState& render_state) {
#ifdef DEBUG
		std::cout << "Fill rectangle " <<
			command.GetLeft() << "," <<
			command.GetTop() << " -> " <<
			command.GetRight() << "," <<
			command.GetBottom() << " with " <<
			std::hex << command.GetColor() << std::dec << std::endl;
#endif
		uint8* color_channels = (uint8*)&color;
		if (color_channels[0] == 0) {
			// Completely transparent, nothing to draw.
			return;
		}

		if (render_state.destination_texture == nullptr) {
			// No destination texture.
			return;
		}

		if (render_state.destination_texture->owner == 0) {
			// Filling to the frame buffer.
			// Call the FillRectangle function based on pixel depth of the
			// framebuffer. The ordering is the most likely in my opinion)
			// pixel depths first. We branch on screen_bits_per_pixel_ then
			// call FillRectangle with the same value because we want the
			// compiler to inline FillRectangle with the pixel depth constant
			// folded.
			if (screen_bits_per_pixel_ == 24) {
				FillRectangle(
					left, right, top, bottom,
					(uint8*)framebuffer_,
					screen_width_,
					screen_height_,
					screen_pitch_,
					/*destination_bpp=*/24,
					color,
					/*alpha_blend=*/false);
			} else if (screen_bits_per_pixel_ == 32) {
				FillRectangle(
					left, right, top, bottom,
					(uint8*)framebuffer_,
					screen_width_,
					screen_height_,
					screen_pitch_,
					/*destination_bpp=*/32,
					color,
					/*alpha_blend=*/false);
			} else if (screen_bits_per_pixel_ == 16) {
				FillRectangle(
					left, right, top, bottom,
					(uint8*)framebuffer_,
					screen_width_,
					screen_height_,
					screen_pitch_,
					/*destination_bpp=*/16,
					color,
					/*alpha_blend=*/false);
			}  if (screen_bits_per_pixel_ == 15) {
				FillRectangle(
					left, right, top, bottom,
					(uint8*)framebuffer_,
					screen_width_,
					screen_height_,
					screen_pitch_,
					/*destination_bpp=*/15,
					color,
					/*alpha_blend=*/false);
			} else {
				// Unsupported bits per pixel for the screen.
			}
		} else {
			// Filling another texture.
			FillRectangle(
				left, right, top, bottom,
				(uint8*)**render_state.destination_texture->shared_memory,
				render_state.destination_texture->width,
				render_state.destination_texture->height,
				/*destination_pitch=*/
				render_state.destination_texture->width * 4,
				/*destination_bpp=*/32,
				color,
				/*alpha_blend=*/true);
		}
	}

	inline void FillRectangle(
		uint32 left, uint32 right, uint32 top, uint32 bottom,
		uint8* destination,
		uint32 destination_width,
		uint32 destination_height,
		uint32 destination_pitch,
		uint32 destination_bpp,
		uint32 color,
		bool alpha_blend) {
		uint8* color_channels = (uint8*)&color;

		left = std::max((uint32)0, left);
		top = std::max((uint32)0, top);
		right = std::min(right, destination_width);
		bottom = std::min(bottom, destination_height);

		if (color_channels[0] == 0xFF || !alpha_blend) {
			// Completely solid color.
			int bytes_per_pixel;
			switch (destination_bpp) {
				case 15:
				case 16:
					bytes_per_pixel = 2;
					break;
				case 24:
					bytes_per_pixel = 3;
					break;
				case 32:
					bytes_per_pixel = 4;
					break;
			}

			uint8* destination_copy_start =
				&destination[top * destination_pitch +
					left * bytes_per_pixel];
			// Copy this row.
			for (int y = top; y < bottom; y++) {
				destination = destination_copy_start;

				for (int x = left; x < right; x++) {
					switch (destination_bpp) {
						case 32:
							*(uint32*)destination = color;
							destination += 4;
							break;
						case 24:
							destination[0] = color_channels[1];
							destination[1] = color_channels[2];
							destination[2] = color_channels[3];
							destination += 3;
							break;
						case 16: {
							uint16 dither_val = (uint16)kDitheringTable[
								x % kDitheringTableWidth +
								(y % kDitheringTableWidth) * kDitheringTableWidth];
							// Trim colors down to 5:6:5-bits.
							// Beyer color table is 6-bit (0 to 63).
							// 5-bit color has 32 values (increments of 8).
							// 6-bit color has 64 values (increments of 4).
							// We divide the dither value to be in the range of
							// the color into the next increment.
							uint16 red = std::min(((uint16)color_channels[1] + dither_val / 8) >> (8-5), 31);
							uint16 green = std::min(((uint16)color_channels[2] + dither_val / 4) >> (8-6), 63);
							uint16 blue = std::min(((uint16)color_channels[3] + dither_val / 8) >> (8-5), 31);
							*(uint16*)destination =
								(blue << 11) | (green << 5) | red;
							destination += 2;
							break;
						}
						case 15: {
							uint16 dither_val = (uint16)kDitheringTable[
								x % kDitheringTableWidth +
								(y % kDitheringTableWidth) * kDitheringTableWidth];
							// Trim colors down to 5:5:5-bits.
							uint16 red = std::min(((uint16)color_channels[1] + dither_val / 8) >> (8-5), 31);
							uint16 green = std::min(((uint16)color_channels[2] + dither_val / 8) >> (8-5), 31);
							uint16 blue = std::min(((uint16)color_channels[3] + dither_val / 8) >> (8-5), 31);

							*(uint16*)destination =
								(blue << 10) | (green << 5) | red;
							destination += 2;
							break;
						}
					}
				}

				// More the start pointers to the next row.
				destination_copy_start += destination_pitch;
			}
		} else {
			// Alpha blend.
			int alpha = color_channels[0] ;
			int inv_alpha = 255 - color_channels[0];

			int indx = (destination_width * top + left) * 4;
			int _x, _y;
			for(_y = top; _y < bottom; _y++) {
				int next_indx = indx + destination_width * 4;
				for(_x = left; _x < right; _x++) {
					destination[indx + 1] = (uint8)((alpha * (int)color_channels[1] + inv_alpha * (int)destination[indx + 1]) >> 8);
					destination[indx + 2] = (uint8)((alpha * (int)color_channels[2] + inv_alpha * (int)destination[indx + 2]) >> 8);
					destination[indx + 3] = (uint8)((alpha * (int)color_channels[3] + inv_alpha * (int)destination[indx + 3]) >> 8);
					indx += 4;
				}
				indx = next_indx;
			}
		}
	}

	// Releases all of the resources that a process owns.
	void ReleaseAllResourcesBelongingToProcess(ProcessId process) {
		auto process_information_itr = process_information_.find(process);
		if (process_information_itr == process_information_.end())
			return; // Cant find this process.

		for (uint64 texture : process_information_itr->second.textures) {
			// Release every texture owned by this process.
			auto texture_itr = textures_.find(texture);
			if (texture_itr == textures_.end())
				// Can't find this texture. This shouldn't happen.
				continue;

			textures_.erase(texture_itr);
		}
		process_information_.erase(process_information_itr);
	}
};

int main() {
	size_t physical_address;
	uint32 width, height, pitch;
	uint8 bpp;
	GetMultibootFramebufferDetails(physical_address,
		width, height, pitch, bpp);

	if (width == 0) {
		std::cout << "The bootloader did not set up a framebuffer." <<
			std::endl;
		return 0;
	}

	// std::cout << "The bootloader has set up a " << width << "x" <<
	//	height << " (" << (int)bpp << "-bit) framebuffer." << std::endl;

	if (bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32) {
		std::cout << "The framebuffer is not 15, 16, 24, or 32 bits per pixel." <<
			std::endl;
		return 0;
	}

	FramebufferGraphicsDriver graphics_driver(
		physical_address, width, height, pitch, bpp);
	perception::HandOverControl();
	return 0;
}
