#include "permebuf.h"

#include "perception/memory.h"

#include <string>

namespace {

// Size of a Permebuf page (aligns with the system page size.)
constexpr int kPageSize = 4096;

std::string kEmptyString = "";

}  // namespace

PermebufString::PermebufString(PermebufBase* buffer, size_t offset) : buffer_(buffer), address_(offset) {}

std::string_view PermebufString::operator->() const {
	return **this;
}

std::string_view PermebufString::operator*() const {
	if (address_ == 0)
		return kEmptyString;
	size_t bytes;
	size_t length = buffer_->ReadVariableLengthNumber(address_, bytes);
	if (length == 0)
		return kEmptyString;
	return std::string_view((const char *)buffer_->GetRawPointer(address_ + bytes, length), length);
}

bool PermebufString::IsEmpty() const {
	return address_ == 0;
}

size_t PermebufString::Length() const {
	size_t bytes;
	return buffer_->ReadVariableLengthNumber(address_, bytes);
}

size_t PermebufString::Address() const {
	return address_;
}

void* PermebufString::RawString() const {
	if (address_ == 0)
		return (void *)kEmptyString.c_str();

	size_t bytes;
	size_t length = buffer_->ReadVariableLengthNumber(address_, bytes);
	if (length == 0)
		return (void *)kEmptyString.c_str();
	void* raw_string = buffer_->GetRawPointer(address_ + bytes, length);
	if (raw_string == nullptr)
		return (void *)kEmptyString.c_str();
	return raw_string;
}

PermebufBytes::PermebufBytes(PermebufBase* buffer, size_t offset) : buffer_(buffer), address_(offset) {}

void* PermebufBytes::operator->() const {
	return **this;
}

void* PermebufBytes::operator*() const {
	if (address_ == 0)
		return nullptr;

	size_t bytes;
	size_t length = buffer_->ReadVariableLengthNumber(address_, bytes);
	if (length == 0)
		return nullptr;
	return buffer_->GetRawPointer(address_ + bytes, length);
}

void* PermebufBytes::RawBytes() const {
	return **this;
}

size_t PermebufBytes::Size() const {
	size_t bytes;
	return buffer_->ReadVariableLengthNumber(address_, bytes);
}

size_t PermebufBytes::Address() const {
	return address_;
}

PermebufArray::PermebufArray(PermebufBase* buffer, size_t offset) {
	if (offset == 0) {
		length_ = 0;
		first_item_address_ = 0;
	} else {
		size_t bytes;
		length_ = buffer_->ReadVariableLengthNumber(offset, bytes);
		first_item_address_ = offset + length_;
	}
}

bool PermebufArray::IsValid() const {
	return length_ != 0;

}

int PermebufArray::Length() const {
	return length_;
}

PermebufArrayOfBooleans::PermebufArrayOfBooleans(PermebufBase* buffer, size_t offset)
	: PermebufArray(buffer, offset) {}

bool PermebufArrayOfBooleans::Get(int index) const {
	if (index >= length_) {
		return false;
	}

	uint8_t byte = buffer_->Read1Byte(first_item_address_ + index / 8);
	return byte & (1 << (index % 8));
}

void PermebufArrayOfBooleans::Set(int index, bool value) {
	if (index >= length_) {
		return;
	}

	size_t address_of_byte = first_item_address_ + index / 8;
	uint8_t byte = buffer_->Read1Byte(address_of_byte);
	byte |= 1 << (index % 8);
	buffer_->Write1Byte(address_of_byte, byte);
}

PermebufArrayOfStrings::PermebufArrayOfStrings(PermebufBase* buffer, size_t offset)
	: PermebufArray(buffer, offset) {}

PermebufString PermebufArrayOfStrings::Get(int index) const {
	if (index >= length_) {
		return PermebufString(buffer_, 0);
	}

	size_t string_offset = first_item_address_ +
		(index << static_cast<size_t>(buffer_->GetAddressSize()));

	return PermebufString(buffer_, buffer_->ReadPointer(string_offset));
}

bool PermebufArrayOfStrings::Has(int index) const {
	if (index >= length_) {
		return false;
	}

	size_t string_offset = first_item_address_ +
		(index << static_cast<size_t>(buffer_->GetAddressSize()));

	return buffer_->ReadPointer(string_offset) != 0;
}

void PermebufArrayOfStrings::Set(int index, PermebufString value) {
	if (index >= length_) {
		return;
	}

	size_t string_offset = first_item_address_ +
		(index << static_cast<size_t>(buffer_->GetAddressSize()));

	buffer_->WritePointer(string_offset, value.Address());
}

void PermebufArrayOfStrings::Set(int index, std::string_view value) {
	if (index >= length_) {
		return;
	}

	size_t string_offset = first_item_address_ +
		(index << static_cast<size_t>(buffer_->GetAddressSize()));

	buffer_->WritePointer(string_offset, buffer_->AllocateString(value).Address());
}

void PermebufArrayOfStrings::Clear(int index) {
	if (index >= length_) {
		return;
	}

	size_t string_offset = first_item_address_ +
		(index << static_cast<size_t>(buffer_->GetAddressSize()));

	buffer_->WritePointer(string_offset, 0);
}

PermebufArrayOfBytes::PermebufArrayOfBytes(PermebufBase* buffer, size_t offset)
	: PermebufArray(buffer, offset) {}

PermebufBytes PermebufArrayOfBytes::Get(int index) const {
	if (index >= length_) {
		return PermebufBytes(buffer_, 0);
	}

	size_t string_offset = first_item_address_ +
		(index << static_cast<size_t>(buffer_->GetAddressSize()));

	return PermebufBytes(buffer_, buffer_->ReadPointer(string_offset));

}

bool PermebufArrayOfBytes::Has(int index) const {
	if (index >= length_) {
		return false;
	}

	size_t string_offset = first_item_address_ +
		(index << static_cast<size_t>(buffer_->GetAddressSize()));

	return buffer_->ReadPointer(string_offset) != 0;
}

void PermebufArrayOfBytes::Set(int index, PermebufBytes value) {
	if (index >= length_) {
		return;
	}

	size_t string_offset = first_item_address_ +
		(index << static_cast<size_t>(buffer_->GetAddressSize()));

	buffer_->WritePointer(string_offset, value.Address());
}

void PermebufArrayOfBytes::Set(int index, void* value, size_t length) {
	if (index >= length_) {
		return;
	}

	size_t string_offset = first_item_address_ +
		(index << static_cast<size_t>(buffer_->GetAddressSize()));

	buffer_->WritePointer(string_offset, buffer_->AllocateBytes(value, length).Address());
}

void PermebufArrayOfBytes::Clear(int index) {
	if (index >= length_) {
		return;
	}

	size_t string_offset = first_item_address_ +
		(index << static_cast<size_t>(buffer_->GetAddressSize()));

	buffer_->WritePointer(string_offset, 0);
}

PermebufListOfBooleans::PermebufListOfBooleans(PermebufBase* buffer, size_t offset)
	: PermebufList<PermebufListOfBooleans>(buffer, offset) {}

bool PermebufListOfBooleans::Get() const {
	if (!IsValid()) {
		return false;
	}
	return static_cast<bool>(buffer_->Read1Byte(GetItemAddress()));
}

void PermebufListOfBooleans::Set(bool value) {
	if (!IsValid()) {
		return;
	}
	buffer_->Write1Byte(GetItemAddress(), static_cast<uint8_t>(value));
}

size_t PermebufListOfBooleans::GetSizeInBytes(PermebufBase* buffer) {
	return buffer->GetAddressSizeInBytes() + 1;
}


PermebufListOfStrings::PermebufListOfStrings(PermebufBase* buffer, size_t offset)
	: PermebufList<PermebufListOfStrings>(buffer, offset) {}

bool PermebufListOfStrings::Has() const {
	if (!IsValid()) {
		return false;
	}
	return buffer_->ReadPointer(GetItemAddress()) != 0;
}

PermebufString PermebufListOfStrings::Get() const {
	if (!IsValid()) {
		return PermebufString(buffer_, 0);
	}
	return PermebufString(buffer_, buffer_->ReadPointer(GetItemAddress()));
}

void PermebufListOfStrings::Set(PermebufString value) {
	if (!IsValid()) {
		return;
	}
	buffer_->WritePointer(GetItemAddress(), value.Address());
}

void PermebufListOfStrings::Set(std::string_view value) {
	if (!IsValid()) {
		return;
	}
	buffer_->WritePointer(GetItemAddress(), buffer_->AllocateString(value).Address());
}

void PermebufListOfStrings::Clear() {
	if (!IsValid()) {
		return;
	}
	buffer_->WritePointer(GetItemAddress(), 0);
}

PermebufListOfBytes::PermebufListOfBytes(PermebufBase* buffer, size_t offset)
	: PermebufList<PermebufListOfBytes>(buffer, offset) {}

bool PermebufListOfBytes::Has() const {
	if (!IsValid()) {
		return false;
	}
	return buffer_->ReadPointer(GetItemAddress()) != 0;
}

PermebufBytes PermebufListOfBytes::Get() const {
	if (!IsValid()) {
		return PermebufBytes(buffer_, 0);
	}
	return PermebufBytes(buffer_, buffer_->ReadPointer(GetItemAddress()));
}

void PermebufListOfBytes::Set(PermebufBytes value) {
	if (!IsValid()) {
		return;
	}
	buffer_->WritePointer(GetItemAddress(), value.Address());
}

void PermebufListOfBytes::Set(void* value, size_t length) {
	if (!IsValid()) {
		return;
	}
	buffer_->WritePointer(GetItemAddress(), buffer_->AllocateBytes(value, length).Address());
}

void PermebufListOfBytes::Clear() {
	if (!IsValid()) {
		return;
	}
	buffer_->WritePointer(GetItemAddress(), 0);
}

uint8_t PermebufBase::Read1Byte(size_t address) const {
	if (address + 1 > size_) {
		return 0;
	}

	return *reinterpret_cast<uint8_t*>(reinterpret_cast<size_t>(start_of_memory_) + address);
}

uint16_t PermebufBase::Read2Bytes(size_t address) const {
	if (address + 2 > size_) {
		return 0;
	}

	return *reinterpret_cast<uint16_t*>(reinterpret_cast<size_t>(start_of_memory_) + address);
}

uint32_t PermebufBase::Read4Bytes(size_t address) const {
	if (address + 4 > size_) {
		return 0;
	}

	return *reinterpret_cast<uint32_t*>(reinterpret_cast<size_t>(start_of_memory_) + address);
}

uint64_t PermebufBase::Read8Bytes(size_t address) const {
	if (address + 8 > size_) {
		return 0;
	}

	return *reinterpret_cast<uint64_t*>(reinterpret_cast<size_t>(start_of_memory_) + address);
}

size_t PermebufBase::ReadPointer(size_t address) const {
	switch (address_size_) {
		case PermebufAddressSize::BITS_8:
			return static_cast<size_t>(Read1Byte(address));
		case PermebufAddressSize::BITS_16:
			return static_cast<size_t>(Read2Bytes(address));
		case PermebufAddressSize::BITS_32:
			return static_cast<size_t>(Read4Bytes(address));
		case PermebufAddressSize::BITS_64:
			return static_cast<size_t>(Read8Bytes(address));
		default:
			return 0;
	}
}

size_t PermebufBase::ReadVariableLengthNumber(size_t address) const {
	size_t bytes;
	return ReadVariableLengthNumber(address, bytes);
}

size_t PermebufBase::ReadVariableLengthNumber(size_t address, size_t& bytes) const {
	size_t first_byte = static_cast<size_t>(Read1Byte(address));

	if (first_byte == 0b11111111) {
		// 64-bit number. Read 8 more bytes.
		bytes = 9;
		return static_cast<size_t>(Read8Bytes(address + 1));
	} else if (first_byte == 0b01111111) {
		// 56-bit number. Read 7 more bytes.
		bytes = 8;
		return static_cast<size_t>(Read8Bytes(address)) >> 8;
	} else if ((first_byte & 0b00111111) == 0b00111111) {
		// 49-bit number. Read 6 more bytes.
		bytes = 7;
		size_t bits_0 = first_byte >> 7;
		size_t bits_1_to_16 = static_cast<size_t>(Read2Bytes(address + 1)) << 1;
		size_t bits_17_to_48 = static_cast<size_t>(Read4Bytes(address + 3)) << 17;
		return bits_0 + bits_1_to_16 + bits_17_to_48;
	} else if ((first_byte & 0b00011111) == 0b00011111) {
		// 42-bit number. Read 5 more bytes.
		bytes = 6;
		size_t bits_0_to_1 = first_byte >> 6;
		size_t bits_2_to_9 = static_cast<size_t>(Read1Byte(address + 1)) << 2;
		size_t bits_10_to_41 = static_cast<size_t>(Read4Bytes(address + 2)) << 10;
		return bits_0_to_1 + bits_2_to_9 + bits_10_to_41;
	} else if ((first_byte & 0b00001111) == 0b00001111) {
		// 35-bit number. Read 4 more bytes.
		bytes = 5;
		size_t bits_0_to_2 = first_byte >> 5;
		size_t bits_3_to_34 = static_cast<size_t>(Read4Bytes(address + 1)) << 3;
		return bits_0_to_2 + bits_3_to_34;
	} else if ((first_byte & 0b00000111) == 0b00000111) {
		// 28-bit number. Read 3 more bytes.
		bytes = 4;
		return static_cast<size_t>(Read4Bytes(address)) >> 4;
	} else if ((first_byte & 0b00000011) == 0b00000011) {
		// 21-bit number. Read 2 more bytes.
		bytes = 3;
		size_t bits_0_to_4 = first_byte >> 3;
		size_t bits_5_to_20 = static_cast<size_t>(Read2Bytes(address + 1)) << 5;
		return bits_0_to_4 + bits_5_to_20;
	} else if ((first_byte & 0b00000001) == 0b00000001) {
		// 14-bit number. Read 1 more byte.
		bytes = 2;
		return static_cast<size_t>(Read2Bytes(address)) >> 2;
	} else {
		// 7-bit number.
		bytes = 1;
		return first_byte >> 1;
	}
}

void PermebufBase::Write1Byte(size_t address, uint8_t value) {
	if (!GrowTo(address + 1)) {
		return;
	}

	*reinterpret_cast<uint8_t*>(reinterpret_cast<size_t>(start_of_memory_) + address) = value;
}

void PermebufBase::Write2Bytes(size_t address, uint16_t value) {
	if (!GrowTo(address + 2)) {
		return;
	}

	*reinterpret_cast<uint16_t*>(reinterpret_cast<size_t>(start_of_memory_) + address) = value;
}

void PermebufBase::Write4Bytes(size_t address, uint32_t value) {
	if (!GrowTo(address + 4)) {
		return;
	}

	*reinterpret_cast<uint32_t*>(reinterpret_cast<size_t>(start_of_memory_) + address) = value;
}

void PermebufBase::Write8Bytes(size_t address, uint64_t value) {
	if (!GrowTo(address + 8)) {
		return;
	}

	*reinterpret_cast<uint64_t*>(reinterpret_cast<size_t>(start_of_memory_) + address) = value;
}

void PermebufBase::WritePointer(size_t address, size_t value) {
	switch (address_size_) {
		case PermebufAddressSize::BITS_8:
			Write1Byte(address, static_cast<uint8_t>(value));
			break;
		case PermebufAddressSize::BITS_16:
			Write2Bytes(address, static_cast<uint16_t>(value));
			break;
		case PermebufAddressSize::BITS_32:
			Write4Bytes(address, static_cast<uint32_t>(value));
			break;
		case PermebufAddressSize::BITS_64:
			Write8Bytes(address, static_cast<uint64_t>(value));
			break;
		default:
			break;
	}
}

void PermebufBase::WriteVariableLengthNumber(size_t address, size_t value) {
	if (!(value & 0b1111'1111'1111'1111'1111'1111'1111'1111'1111'1111'1111'1111'1111'1111'1000'0000)) {
		// 7 bit number
		if (!GrowTo(address + 1)) {
			return;
		}
		Write1Byte(address, static_cast<uint8_t>(value << 1));
	} else if (!(value & 0b1111'1111'1111'1111'1111'1111'1111'1111'1111'1111'1111'1111'1100'0000'0000'0000)) {
		// 14 bit number
		if (!GrowTo(address + 2)) {
			return;
		}
		Write2Bytes(address, 0b0000'0001 + static_cast<uint16_t>(value << 2));
	} else if (!(value & 0b1111'1111'1111'1111'1111'1111'1111'1111'1111'1111'1110'0000'0000'0000'0000'0000)) {
		// 21 bit number
		if (!GrowTo(address + 3)) {
			return;
		}
		Write1Byte(address, 0b0000'0011 + static_cast<uint8_t>(value << 3));
		Write2Bytes(address + 1, static_cast<uint16_t>(value >> 5));
	} else if (!(value & 0b1111'1111'1111'1111'1111'1111'1111'1111'1111'0000'0000'0000'0000'0000'0000'0000)) {
		// 28 bit number
		if (!GrowTo(address + 4)) {
			return;
		}
		Write4Bytes(address, 0b0000'0111 + static_cast<uint32_t>(value << 4));
	} else if (!(value & 0b1111'1111'1111'1111'1111'1111'1111'1000'0000'0000'0000'0000'0000'0000'0000'0000)) {
		// 35 bit number
		if (!GrowTo(address + 5)) {
			return;
		}
		Write1Byte(address, 0b0000'1111 + static_cast<uint8_t>(value << 5));
		Write4Bytes(address + 1, static_cast<uint32_t>(value >> 3));
	} else if (!(value & 0b1111'1111'1111'1111'1111'1100'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000)) {
		// 42 bit number
		if (!GrowTo(address + 6)) {
			return;
		}
		Write1Byte(address, 0b0001'1111 + static_cast<uint8_t>(value << 6));
		Write1Byte(address + 1, static_cast<uint8_t>(value >> 2));
		Write4Bytes(address + 2, static_cast<uint32_t>(value >> 10));
	} else if (!(value & 0b1111'1111'1111'1110'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000)) {
		// 49 bit number
		if (!GrowTo(address + 7)) {
			return;
		}
		Write1Byte(address, 0b0011'1111 + static_cast<uint8_t>(value << 7));
		Write2Bytes(address + 1, static_cast<uint16_t>(value >> 1));
		Write4Bytes(address + 3, static_cast<uint32_t>(value >> 17));
	} else if (!(value & 0b1111'1111'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000)) {
		// 56 bit number
		if (!GrowTo(address + 8)) {
			return;
		}

		Write8Bytes(address, 0b0111'1111 + static_cast<uint64_t>(value << 8));
	} else {
		// 64 bit number
		if (!GrowTo(address + 9)) {
			return;
		}
		Write1Byte(address, 0b1111'1111);
		Write8Bytes(address + 1, static_cast<uint64_t>(value));
	}
}


void* PermebufBase::GetRawPointer(size_t address, size_t data_length) {
	if (address + data_length > size_) {
		return nullptr;
	}

	return reinterpret_cast<void *>(
		reinterpret_cast<size_t>(start_of_memory_) + address);
}

size_t PermebufBase::Size() const {
	return size_;
}

PermebufString PermebufBase::AllocateString(std::string_view value) {
	if (value.empty()) {
		// Blank strings shouldn't take up memory.
		return PermebufString(this, 0);
	}

	// Calculate the size of the string.
	size_t string_length = value.length();
	size_t string_length_bytes = GetBytesNeededForVariableLengthNumber(string_length);
	size_t total_length = string_length_bytes + string_length;

	// Allocate the string at the end of the Permebuf.
	size_t string_address = size_;
	if (!GrowTo(size_ + total_length)) {
		// Couldn't allocate space for the string.
		return PermebufString(this, 0);
	}

	WriteVariableLengthNumber(string_address, string_length);
	memcpy(reinterpret_cast<void *>(
		reinterpret_cast<size_t>(start_of_memory_) + string_address + string_length_bytes),
		value.data(),
		string_length);

	return PermebufString(this, string_address);
}

PermebufBytes PermebufBase::AllocateBytes(void* data, size_t length) {
	if (length == 0) {
		// Empty data shouldn't take up memory.
		return PermebufBytes(this, 0);
	}

	size_t length_bytes = GetBytesNeededForVariableLengthNumber(length);
	size_t total_length = length_bytes + length;

	// Allocate the bytes at the end of the Permebuf.
	size_t bytes_address = size_;
	if (!GrowTo(size_ + total_length)) {
		// Couldn't allocate space for the bytes.
		return PermebufBytes(this, 0);
	}

	WriteVariableLengthNumber(bytes_address, length);
	memcpy(reinterpret_cast<void *>(
		reinterpret_cast<size_t>(start_of_memory_) + bytes_address + length_bytes),
		data,
		length);

	return PermebufBytes(this, bytes_address);
}

PermebufAddressSize PermebufBase::GetAddressSize() const {
	return address_size_;
}

PermebufBase::PermebufBase(PermebufAddressSize address_size) :
	address_size_(address_size) {
	start_of_memory_ = ::perception::AllocateMemoryPages(1);
	size_ = 1;

	// Set the metadata byte.
	*static_cast<uint8_t*>(start_of_memory_) = static_cast<uint8_t>(address_size);
}

PermebufBase::PermebufBase(void* start_of_memory, size_t size) :
	start_of_memory_(start_of_memory),
	size_(size) {
	// Read the address size from the metadata byte.
	uint8_t metadata_byte = *static_cast<uint8_t*>(start_of_memory_);
	address_size_ = static_cast<PermebufAddressSize>(metadata_byte & 0b11);
}

PermebufBase::~PermebufBase() {
	if (start_of_memory_ != nullptr) {
		::perception::ReleaseMemoryPages(start_of_memory_, size_);
	}
}

size_t PermebufBase::AllocateMessage(size_t size) {
	// The new message will be allocated at the current end of the buffer.
	size_t current_ptr = size_;

	// Calculate the bytes needed to store the size itself.
	size_t size_bytes = GetBytesNeededForVariableLengthNumber(size);
	size_t total_length = size_bytes + size;

	// Attempt to grow the buffer by the size of the message.
	if (GrowTo(size_ + total_length)) {
		WriteVariableLengthNumber(current_ptr, size);
		return current_ptr;
	} else {
		// Couldn't grow the PermebufBase to this size.
		return 0;
	}
}

bool PermebufBase::GrowTo(size_t size) {
	if (size < size_) {
		// Already big enough!
		return true;
	}

	// Can we grow to this size?
	switch (address_size_) {
		case PermebufAddressSize::BITS_8:
			if (size > 0xFF) {
				return false;
			}
			break;
		case PermebufAddressSize::BITS_16:
			if (size > 0xFFFF) {
				return false;
			}
			break;
		case PermebufAddressSize::BITS_32:
			if (size > 0xFFFFFFFF) {
				return false;
			}
			break;
		case PermebufAddressSize::BITS_64:
			// size_t is 64-bit. All valid size_t values are within range.
			break;
		default:
			return false;
	}

	// Allocate us more pages.
	size_t desired_numer_of_pages = (size + kPageSize - 1) / kPageSize;
	size_t current_numer_of_pages = GetNumberOfAllocatedMemoryPages();

	// We already have the allocated pages.
	if (desired_numer_of_pages <= current_numer_of_pages) {
		size_ = size;
		return true;
	}

	// We need to allocate more pages.
	if (::perception::MaybeResizePages(&start_of_memory_, current_numer_of_pages,
			desired_numer_of_pages)) {
		// We allocated more memory.
		size_ = size;
		return true;
	} else {
		// We weren't able to allocate more memory.
		return false;
	}
}

size_t PermebufBase::GetNumberOfAllocatedMemoryPages() const {
	return (size_ + kPageSize - 1) / kPageSize;;
}

size_t PermebufBase::GetBytesNeededForVariableLengthNumber(size_t value) const {
	if (!(value & 0b1111'1111'1111'1111'1111'1111'1111'1111'1111'1111'1111'1111'1111'1111'1000'0000)) {
		// 7 bit number
		return 1;
	} else if (!(value & 0b1111'1111'1111'1111'1111'1111'1111'1111'1111'1111'1111'1111'1100'0000'0000'0000)) {
		// 14 bit number
		return 2;
	} else if (!(value & 0b1111'1111'1111'1111'1111'1111'1111'1111'1111'1111'1110'0000'0000'0000'0000'0000)) {
		// 21 bit number
		return 3;
	} else if (!(value & 0b1111'1111'1111'1111'1111'1111'1111'1111'1111'0000'0000'0000'0000'0000'0000'0000)) {
		// 28 bit number
		return 4;
	} else if (!(value & 0b1111'1111'1111'1111'1111'1111'1111'1000'0000'0000'0000'0000'0000'0000'0000'0000)) {
		// 35 bit number
		return 5;
	} else if (!(value & 0b1111'1111'1111'1111'1111'1100'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000)) {
		// 42 bit number
		return 6;
	} else if (!(value & 0b1111'1111'1111'1110'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000)) {
		// 49 bit number
		return 7;
	} else if (!(value & 0b1111'1111'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000)) {
		// 56 bit number
		return 8;
	} else {
		// 64 bit number
		return 9;
	}
}

