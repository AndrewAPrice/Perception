#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string_view>

// Determines the address size. Larger addresses allow the overall Permebuf to grow larger,
// however, data structures take up more memory.
enum class PermebufAddressSize {
	// 8-bit addresses allow Permebufs up to 256 bytes.
	BITS_8 = 0,
	// 16-bit addresses allow Permebufs up to 64 KB.
	BITS_16 = 1,
	// 32-bit adddresses allow Permebufs up to 4 GB.
	BITS_32 = 2,
	// 64-bit addresses allow Permebufs up to 16 EB.
	BITS_64 = 3
};

class PermebufBase;

// TODO: Implement
class PermebufString {
public:
	PermebufString(PermebufBase* buffer, size_t offset);

	std::string_view operator->() const;
	std::string_view operator*() const;
	bool IsEmpty() const;
	size_t Length() const;
	size_t Address() const;
	void* RawString() const;

private:
	PermebufBase* buffer_;
	size_t address_;
};

// TODO: Implement
class PermebufBytes {
public:
	PermebufBytes(PermebufBase* buffer, size_t offset);

	void* operator->() const;
	void* operator*() const;
	void* RawBytes() const;
	size_t Size() const;
	size_t Address() const;

private:
	PermebufBase* buffer_;
	size_t address_;
};

class PermebufArray {
public:
	PermebufArray(PermebufBase* buffer, size_t offset);
	bool IsValid() const;
	int Length() const;

protected:
	PermebufBase* buffer_;
	size_t length_;
	size_t first_item_address_;
};

class PermebufArrayOfBooleans : public PermebufArray {
public:
	PermebufArrayOfBooleans(PermebufBase* buffer, size_t offset);
	bool Get(int index) const;
	void Set(int index, bool value);

private:
	PermebufBase* buffer_;
	size_t length_;
	size_t first_item_address_;
};

template <class T>
class PermebufArrayOfEnums : public PermebufArray {
public:
	PermebufArrayOfEnums(PermebufBase* buffer, size_t offset);
	T Get(int index) const;
	void Set(int index, T value);

private:
	PermebufBase* buffer_;
	size_t length_;
	size_t first_item_address_;
};

template <class T>
class PermebufArrayOf : public PermebufArray {
public:
	PermebufArrayOf(PermebufBase* buffer, size_t offset);
	typename T::Ref Get(int index) const;
	bool Has(int index) const;
	void Set(int index, typename T::Ref value);
	void Clear(int index);

private:
	PermebufBase* buffer_;
	size_t length_;
	size_t first_item_address_;
};

template <class T>
class PermebufArrayOfOneofs : public PermebufArray {
public:
	PermebufArrayOfOneofs(PermebufBase* buffer, size_t offset);
	typename T::Ref Get(int index) const;
	bool Has(int index) const;
	void Set(int index, typename T::Ref value);
	void Clear(int index);

private:
	PermebufBase* buffer_;
	size_t length_;
	size_t first_item_address_;
};

class PermebufArrayOfStrings : public PermebufArray {
public:
	PermebufArrayOfStrings(PermebufBase* buffer, size_t offset);
	PermebufString Get(int index) const;
	bool Has(int index) const;
	void Set(int index, PermebufString value);
	void Set(int index, std::string_view value);
	void Clear(int index);

private:
	PermebufBase* buffer_;
	size_t length_;
	size_t first_item_address_;
};

class PermebufArrayOfBytes : public PermebufArray {
public:
	PermebufArrayOfBytes(PermebufBase* buffer, size_t offset);
	PermebufBytes Get(int index) const;
	bool Has(int index) const;
	void Set(int index, PermebufBytes value);
	void Set(int index, void* value, size_t length);
	void Clear(int index);

private:
	PermebufBase* buffer_;
	size_t length_;
	size_t first_item_address_;
};

template <class T>
class PermebufArrayOfNumbers : public PermebufArray {
public:
	PermebufArrayOfNumbers(PermebufBase* buffer, size_t offset);
	T Get(int index) const;
	void Set(int index, T value);

private:
	PermebufBase* buffer_;
	size_t length_;
	size_t first_item_address_;
};

template <class T>
class PermebufList {
public:
	PermebufList(PermebufBase* buffer, size_t offset);
	bool IsValid() const;
	bool HasNext() const;
	int Count() const;
	T Next() const;
	T Get(int index) const;
	T RemoveNext() const;
	T RemoveAllAfter() const;
protected:
	size_t GetItemAddress() const;
	size_t Address();

	PermebufBase* buffer_;
	size_t offset_;
};

// TODO: Implement
class PermebufListOfBooleans {};

// TODO: Implement
template <class T>
class PermebufListOfEnums {};

// TODO: Implement
template <class T>
class PermebufListOf {};

// TODO: Implement
template <class T>
class PermebufListOfOneofs {};

// TODO: Implement
template <class T>
class PermebufListOfStrings {};

// TODO: Implement
class PermebufListOfBytes {};

// TODO: Implement
template <class T>
class PermebufListOfNumbers {};

class PermebufBase {
public:
	uint8_t Read1Byte(size_t address) const;
	uint16_t Read2Bytes(size_t address) const;
	uint32_t Read4Bytes(size_t address) const;
	uint64_t Read8Bytes(size_t address) const;
	size_t ReadPointer(size_t address) const;
	size_t ReadVariableLengthNumber(size_t address) const;
	size_t ReadVariableLengthNumber(size_t address, size_t& bytes) const;

	void Write1Byte(size_t address, uint8_t value);
	void Write2Bytes(size_t address, uint16_t value);
	void Write4Bytes(size_t address, uint32_t value);
	void Write8Bytes(size_t address, uint64_t value);
	void WritePointer(size_t address, size_t value);
	void WriteVariableLengthNumber(size_t address, size_t value);


	void* GetRawPointer(size_t address, size_t data_length);

	// Returns the size in bytes of the Permebuf.
	size_t Size() const;

	PermebufString AllocateString(std::string_view value);
	PermebufBytes AllocateBytes(void* data, size_t length);

	// TODO - allocate array
	// TODO - allocate list
	template <class T> T AllocateMessage() {
		return T(this, AllocateMessage(T::GetSizeInBytes(this)));
	}

	PermebufAddressSize GetAddressSize() const;

protected:
	PermebufBase(PermebufAddressSize address_size);
	PermebufBase(void* start_of_memory, size_t size);
	virtual ~PermebufBase();

private:
	size_t AllocateMessage(size_t size);
	bool GrowTo(size_t size);
	size_t GetNumberOfAllocatedMemoryPages() const;
	size_t GetBytesNeededForVariableLengthNumber(size_t value) const;

	PermebufAddressSize address_size_;

	// Start of the first page.
	void* start_of_memory_;

	// Size of the Permebuf.
	size_t size_;
};

template <class T>
class Permebuf : public PermebufBase {
public:
	// Creates a new Permabuf.
	Permebuf(PermebufAddressSize address_size = PermebufAddressSize::BITS_16) : PermebufBase(address_size) {
		// Allocate the first message in the Permebuf.
		(void)AllocateMessage<T::Ref>();
	}

	// Wraps a Permabug around raw memory. This memory must be page aligned, and we take ownership
	// of the memory.
	Permebuf(void* start_of_memory, size_t size) :
		PermebufBase(start_of_memory, size) {}

	virtual ~Permebuf() {}

	// Copying is heavy weight, so it is forbidden. You can use .clone() instead.
	Permebuf(const Permebuf<T>&) = delete;
	void operator=(const Permebuf<T>&) = delete;

	Permebuf<T> Clone() const {
		// TODO
	}

	typedef typename T::Ref Ref;

	const Ref operator-> () const {
		return Ref(this, 1);
	}

	Ref operator-> () {
		return Ref(this, 1);
	}

private:

};


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


template <class T>
PermebufArrayOfEnums<T>::PermebufArrayOfEnums(PermebufBase* buffer, size_t offset) 
	: PermebufArray(buffer, offset) {}

template <class T>
T PermebufArrayOfEnums<T>::Get(int index) const {
	if (index >= length_) {
		return static_cast<T>(0);
	}

	return static_cast<T>(buffer_->Read2Bytes(first_item_address_ + index * 2));
}

template <class T>
void PermebufArrayOfEnums<T>::Set(int index, T value) {
	if (index >= length_) {
		return;
	}

	buffer_->Write2Bytes(first_item_address_ + index * 2, static_cast<uint16_t>(value));
}

template <class T>
PermebufArrayOf<T>::PermebufArrayOf(PermebufBase* buffer, size_t offset)
	: PermebufArray(buffer, offset) {}

template <class T>
typename T::Ref PermebufArrayOf<T>::Get(int index) const {
	if (index >= length_) {
		return T::Ref(buffer_, 0);
	}

	return T::Ref(buffer_, buffer_->ReadPointer(first_item_address_ +
		index << static_cast<size_t>(buffer_->GetAddressSize())));
}

template <class T>
bool PermebufArrayOf<T>::Has(int index) const {
	if (index >= length_) {
		return T::Ref(buffer_, 0);
	}

	return buffer_->ReadPointer(first_item_address_ +
		index << static_cast<size_t>(buffer_->GetAddressSize())) != 0;
}

template <class T>
void PermebufArrayOf<T>::Set(int index, typename T::Ref value) {
	if (index >= length_) {
		return;
	}

	buffer_->WritePointer(first_item_address_ +
		index << static_cast<size_t>(buffer_->GetAddressSize()),
		value.Address());
}

template <class T>
void PermebufArrayOf<T>::Clear(int index) {
	if (index >= length_) {
		return;
	}

	buffer_->WritePointer(first_item_address_ +
		index << static_cast<size_t>(buffer_->GetAddressSize()),
		0);
}

template <class T>
PermebufArrayOfOneofs<T>::PermebufArrayOfOneofs(PermebufBase* buffer, size_t offset)
	: PermebufArray(buffer, offset) {}

template <class T>
typename T::Ref PermebufArrayOfOneofs<T>::Get(int index) const {
	if (index >= length_) {
		return T::Ref(buffer_, 0);
	}

	size_t oneof_offset = first_item_address_ +
		index << static_cast<size_t>(buffer_->GetAddressSize()) +
		index * 2;

	return T::Ref(buffer_, oneof_offset);
}

template <class T>
bool PermebufArrayOfOneofs<T>::Has(int index) const {
	if (index >= length_) {
		return T::Ref(buffer_, 0);
	}

	size_t oneof_offset = first_item_address_ +
		index << static_cast<size_t>(buffer_->GetAddressSize()) +
		index * 2;

	return buffer_->ReadPointer(oneof_offset + 2) != 0;
}

template <class T>
void PermebufArrayOfOneofs<T>::Set(int index, typename T::Ref value) {
	if (index >= length_) {
		return;
	}

	size_t source_oneof_offset = value.Address();

	size_t destination_oneof_offset = first_item_address_ +
		index << static_cast<size_t>(buffer_->GetAddressSize()) +
		index * 2;

	buffer_->Write2Bytes(destination_oneof_offset,
		buffer_->Read2Bytes(source_oneof_offset));
	buffer_->WritePointer(destination_oneof_offset + 2,
		buffer_->ReadPointer(source_oneof_offset + 2));
}

template <class T>
void PermebufArrayOfOneofs<T>::Clear(int index) {
	if (index >= length_) {
		return;
	}

	size_t oneof_offset = first_item_address_ +
		index << static_cast<size_t>(buffer_->GetAddressSize()) +
		index * 2;

	buffer_->Write2Bytes(oneof_offset, 0);
	buffer_->WritePointer(oneof_offset + 2, 0);
}

PermebufArrayOfStrings::PermebufArrayOfStrings(PermebufBase* buffer, size_t offset)
	: PermebufArray(buffer, offset) {}

PermebufString PermebufArrayOfStrings::Get(int index) const {
	if (index >= length_) {
		return PermebufString(buffer_, 0);
	}

	size_t string_offset = first_item_address_ +
		index << static_cast<size_t>(buffer_->GetAddressSize());

	return PermebufString(buffer_, buffer_->ReadPointer(string_offset));
}

bool PermebufArrayOfStrings::Has(int index) const {
	if (index >= length_) {
		return false;
	}

	size_t string_offset = first_item_address_ +
		index << static_cast<size_t>(buffer_->GetAddressSize());

	return buffer_->ReadPointer(string_offset) != 0;
}

void PermebufArrayOfStrings::Set(int index, PermebufString value) {
	if (index >= length_) {
		return;
	}

	size_t string_offset = first_item_address_ +
		index << static_cast<size_t>(buffer_->GetAddressSize());

	buffer_->WritePointer(string_offset, value.Address());
}

void PermebufArrayOfStrings::Set(int index, std::string_view value) {
	if (index >= length_) {
		return;
	}

	size_t string_offset = first_item_address_ +
		index << static_cast<size_t>(buffer_->GetAddressSize());

	buffer_->WritePointer(string_offset, buffer_->AllocateString(value).Address());
}

void PermebufArrayOfStrings::Clear(int index) {
	if (index >= length_) {
		return;
	}

	size_t string_offset = first_item_address_ +
		index << static_cast<size_t>(buffer_->GetAddressSize());

	buffer_->WritePointer(string_offset, 0);
}

PermebufArrayOfBytes::PermebufArrayOfBytes(PermebufBase* buffer, size_t offset)
	: PermebufArray(buffer, offset) {}

PermebufBytes PermebufArrayOfBytes::Get(int index) const {
	if (index >= length_) {
		return PermebufBytes(buffer_, 0);
	}

	size_t string_offset = first_item_address_ +
		index << static_cast<size_t>(buffer_->GetAddressSize());

	return PermebufBytes(buffer_, buffer_->ReadPointer(string_offset));

}

bool PermebufArrayOfBytes::Has(int index) const {
	if (index >= length_) {
		return false;
	}

	size_t string_offset = first_item_address_ +
		index << static_cast<size_t>(buffer_->GetAddressSize());

	return buffer_->ReadPointer(string_offset) != 0;
}

void PermebufArrayOfBytes::Set(int index, PermebufBytes value) {
	if (index >= length_) {
		return;
	}

	size_t string_offset = first_item_address_ +
		index << static_cast<size_t>(buffer_->GetAddressSize());

	buffer_->WritePointer(string_offset, value.Address());
}

void PermebufArrayOfBytes::Set(int index, void* value, size_t length) {
	if (index >= length_) {
		return;
	}

	size_t string_offset = first_item_address_ +
		index << static_cast<size_t>(buffer_->GetAddressSize());

	buffer_->WritePointer(string_offset, buffer_->AllocateBytes(value, length).Address());
}

void PermebufArrayOfBytes::Clear(int index) {
	if (index >= length_) {
		return;
	}

	size_t string_offset = first_item_address_ +
		index << static_cast<size_t>(buffer_->GetAddressSize());

	buffer_->WritePointer(string_offset, 0);
}

template <class T>
PermebufArrayOfNumbers<T>::PermebufArrayOfNumbers(PermebufBase* buffer, size_t offset)
	: PermebufArray(buffer, offset) {}

template <class T>
T PermebufArrayOfNumbers<T>::Get(int index) const {
	if (index >= length_) {
		return 0;
	}

	switch(sizeof(T)) {
		case 1:
			return reinterpret_cast<T>(buffer_->Read1Byte(
				first_item_address_ + index));
		case 2:
			return reinterpret_cast<T>(buffer_->Read2Bytes(
				first_item_address_ + index * 2));
		case 4:
			return reinterpret_cast<T>(buffer_->Read4Bytes(
				first_item_address_ + index * 4));
		case 8:
			return reinterpret_cast<T>(buffer_->Read8Bytes(
				first_item_address_ + index * 8));
		default:
			return 0;
	}
}

template <class T>
void PermebufArrayOfNumbers<T>::Set(int index, T value) {
	if (index >= length_) {
		return;
	}

	switch(sizeof(T)) {
		case 1:
			buffer_->Write1Byte(
				first_item_address_ + index,
				reinterpret_cast<uint8_t>(value));
			break;
		case 2:
			buffer_->Write1Byte(
				first_item_address_ + index * 2,
				reinterpret_cast<uint16_t>(value));
			break;
		case 4:
			buffer_->Write1Byte(
				first_item_address_ + index * 4,
				reinterpret_cast<uint32_t>(value));
			break;
		case 8:
			buffer_->Write1Byte(
				first_item_address_ + index * 8,
				reinterpret_cast<uint64_t>(value));
			break;
		default:
			break;
	}
}

template <class T>
PermebufList<T>::PermebufList(PermebufBase* buffer, size_t offset)
	: buffer_(buffer), offset_(offset) {}

template <class T>
bool PermebufList<T>::IsValid() const {
	return offset_ != 0;
}

template <class T>
bool PermebufList<T>::HasNext() const {
	return buffer_->ReadPointer(offset_) != 0;
}

template <class T>
int PermebufList<T>::Count() const {
	if (!IsValid()) {
		return 0;
	}

	return 1 + Next().Count();
}

template <class T>
T PermebufList<T>::Next() const {
	if (!IsValid()) {
		return T(buffer_, 0);
	}

	return T(buffer_, buffer_->ReadPointer(offset_));
}

template <class T>
T PermebufList<T>::Get(int index) const {
	if (!IsValid()) {
		return T(buffer_, 0);
	}

	if (index <= 0) {
		return T(buffer_, offset_);
	}

	return Next().Get(index - 1);
}

template <class T>
T PermebufList<T>::RemoveNext() const {
	if (!IsValid()) {
		return;
	}

	size_t skip_item = Next().Next().Address();
	buffer_->WritePointer(offset_, skip_item);
}

template <class T>
T PermebufList<T>::RemoveAllAfter() const {
	if (!IsValid()) {
		return;
	}

	buffer_->WritePointer(offset_, 0);
}

template <class T>
size_t PermebufList<T>::GetItemAddress() const {
	return offset_ + 1 << static_cast<size_t>(buffer_->GetAddressSize());
}