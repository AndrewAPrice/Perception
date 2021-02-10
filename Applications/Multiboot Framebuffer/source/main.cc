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
#include "perception/shared_memory.h"
#include "permebuf/Libraries/perception/devices/graphics_driver.permebuf.h"

#include <iostream>
#include <map>
#include <set>

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
		screen_bytes_per_pixel_(bpp / 8),
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
		// Run each of the commands.
		for (GraphicsCommand command : commands->GetCommands())
			RunCommand(sender, command);
	}

	void HandleCreateTexture(
		ProcessId sender,
		const GraphicsDriver::CreateTextureRequest& request,
		PermebufMiniMessageReplier<GraphicsDriver::CreateTextureResponse> responder) override {

		// Create the texture.
		uint32 texture_id = next_texture_id_++;
		Texture texture;
		texture.owner = sender;
		texture.width = request.GetWidth();
		texture.height = request.GetHeight();
		texture.shared_memory = std::make_unique<SharedMemory>(
			SharedMemory::FromSize(texture.width * texture.height * 4));

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
		responder.Reply(response);

		textures_[texture_id] = std::move(texture);
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

	void HandleGetTextureInformation(
		ProcessId,
		const GraphicsDriver::GetTextureInformationRequest& request,
		PermebufMiniMessageReplier<GraphicsDriver::GetTextureInformationResponse> responder) override {
		GraphicsDriver::GetTextureInformationResponse response;
		// Try to find the texture.
		auto texture_itr = textures_.find(request.GetTexture());
		if (texture_itr != textures_.end()) {
			// We found the texture. Respond with details about it.
			response.SetOwner(texture_itr->second.owner);
			response.SetWidth(texture_itr->second.width);
			response.SetHeight(texture_itr->second.height);
		}
		responder.Reply(response);
	}

	void HandleSetProcessAllowedToDrawToScreen(
		ProcessId,
		const GraphicsDriver::SetProcessAllowedToDrawToScreenMessage& request) override {
		// TODO: Implement some kind of security.
		process_allowed_to_write_to_the_screen_ = request.GetProcess();
	}
private:
	// The width of the screen, in pixels.
	uint32 screen_width_;

	// The height of the screen, in pixels.
	uint32 screen_height_;

	// Number of bytes between rows of pixels on the screen.
	uint32 screen_pitch_;

	// The number of bytes per pixel on the screen.
	uint8 screen_bytes_per_pixel_;

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
	void RunCommand(ProcessId sender, const GraphicsCommand& graphics_command) {
		switch (graphics_command.GetOption()) {
			case GraphicsCommand::Options::CopyEntireTexture: {
				GraphicsCommand::CopyEntireTexture command =
					*graphics_command.GetCopyEntireTexture();
				BitBlt(sender,
					command.GetSourceTexture(),
					command.GetDestinationTexture(),
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
					*graphics_command.GetCopyEntireTextureWithAlphaBlending();
				BitBlt(sender,
					command.GetSourceTexture(),
					command.GetDestinationTexture(),
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
					*graphics_command.GetCopyTextureToPosition();
				BitBlt(sender,
					command.GetSourceTexture(),
					command.GetDestinationTexture(),
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
					*graphics_command.GetCopyTextureToPositionWithAlphaBlending();
				BitBlt(sender,
					command.GetSourceTexture(),
					command.GetDestinationTexture(),
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
					*graphics_command.GetCopyPartOfATexture();
				BitBlt(sender,
					command.GetSourceTexture(),
					command.GetDestinationTexture(),
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
					*graphics_command.GetCopyPartOfATexture();
				BitBlt(sender,
					command.GetSourceTexture(),
					command.GetDestinationTexture(),
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

	// Bit blit two textures. The BitBlt functions are inlined. The hope is that
	// RunCommand() will be HUGE but all the compiler will optimize away dead code
	// so we'd have a super fast version of each permutation (alpha blend,
	//    no alpha blending, etc.)
	inline void BitBlt(ProcessId sender,
		uint64 source_texture,
		uint64 destination_texture,
		uint32 left_source,
		uint32 top_source,
		uint32 left_destination,
		uint32 top_destination,
		uint32 width_to_copy,
		uint32 height_to_copy,
		bool alpha_blend) {
		// We can't copy from the frame buffer.
		if (source_texture == 0)
			return;

		// Find the source texture.
		auto source_texture_itr = textures_.find(source_texture);
		if (source_texture_itr == textures_.end())
			return;

		if (destination_texture == 0) {
			// We're writing to the screen's frame buffer.

			// Only one process is allowed to write to the screen's
			// framebuffer.
			if (sender != process_allowed_to_write_to_the_screen_)
				// We're not that process.
				return;

			// Call the BitBlt function based on pixel depth
			// of the framebuffer. The ordering is the most likely
			// (in my opinion) pixel depths first.
			if (screen_bytes_per_pixel_ == 3) {
				BitBltToTexture(
					(char*)**source_texture_itr->second.shared_memory,
					source_texture_itr->second.width,
					source_texture_itr->second.height,
					(char*)framebuffer_,
					screen_width_,
					screen_height_,
					screen_pitch_,
					/*destination_bpp=*/3,
					left_source,
					top_source,
					left_destination,
					top_destination,
					width_to_copy,
					height_to_copy,
					alpha_blend);
			} else if (screen_bytes_per_pixel_ == 4) {
				BitBltToTexture(
					(char*)**source_texture_itr->second.shared_memory,
					source_texture_itr->second.width,
					source_texture_itr->second.height,
					(char*)framebuffer_,
					screen_width_,
					screen_height_,
					screen_pitch_,
					/*destination_bpp=*/4,
					left_source,
					top_source,
					left_destination,
					top_destination,
					width_to_copy,
					height_to_copy,
					alpha_blend);
			} else if (screen_bytes_per_pixel_ == 2) {
				BitBltToTexture(
					(char*)**source_texture_itr->second.shared_memory,
					source_texture_itr->second.width,
					source_texture_itr->second.height,
					(char*)framebuffer_,
					screen_width_,
					screen_height_,
					screen_pitch_,
					/*destination_bpp=*/2,
					left_source,
					top_source,
					left_destination,
					top_destination,
					width_to_copy,
					height_to_copy,
					alpha_blend);
			} else {
				// Unsupported bits per pixel for the screen.
			}

		} else {
			// We're writing to another texture.
			auto destination_texture_itr = textures_.find(destination_texture);
			if (destination_texture_itr == textures_.end())
				return;

			BitBltToTexture(
				(char*)**source_texture_itr->second.shared_memory,
				source_texture_itr->second.width,
				source_texture_itr->second.height,
				(char*)**destination_texture_itr->second.shared_memory,
				destination_texture_itr->second.width,
				destination_texture_itr->second.height,
				/*destination_pitch=*/
				destination_texture_itr->second.width * 4,
				/*destination_bpp=*/4,
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
		char* source,
		uint32 source_width,
		uint32 source_height,
		char* destination,
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
			left_destination >= source_width) {
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
			width_to_copy = destination_width - source_width;

		if (width_to_copy == 0 || height_to_copy == 0) {
			// Nothing to copy.
			return;
		}

		char* source_copy_start =
			&source[(top_source * source_width + left_source) * 4];

		char* destination_copy_start =
			&destination[top_destination * destination_pitch +
				left_destination * destination_bpp];

		// Copy this row.
		for (;height_to_copy > 0; height_to_copy--) {
			source = source_copy_start;
			destination = destination_copy_start;

			for (uint32 i = width_to_copy; i > 0; i--) {
				switch (destination_bpp) {
					case 2:
						if (!alpha_blend || source[0] != 0) {
							// Trim colors down to 5:6:5-bits.
							char r = source[1] >> (8-5);
							char g = source[2] >> (8-6);
							char b = source[3] >> (8-5);

							*(uint16*)destination =
								(r << (5 + 6)) |
								(g << 5) |
								b;
						}
						destination += 2;
						break;
					case 3:
						if (!alpha_blend || source[0] != 0) {
							destination[0] = source[1];
							destination[1] = source[2];
							destination[2] = source[3];
						}
						destination += 3;
						break;
					case 4:
						if (!alpha_blend || source[0] != 0) {
							*(uint32*)destination = *(uint32*)source;	
						}
						destination += 4;
						break;
				}

				source += 4;

			}

			// More the start pointers to the next row.
			source_copy_start += source_width * 4;
			destination_copy_start + destination_pitch;
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

	std::cout << "The bootloader has set up a " << width << "x" <<
		height << " (" << (int)bpp << "-bit) framebuffer." << std::endl;

	if (bpp != 16 && bpp != 24 && bpp != 32) {
		std::cout << "The framebuffer is not 16, 24, or 32 bits per pixel." <<
			std::endl;
		return 0;
	}

	FramebufferGraphicsDriver graphics_driver(
		physical_address, width, height, pitch, bpp);
	perception::TransferToEventLoop();
	return 0;
}
