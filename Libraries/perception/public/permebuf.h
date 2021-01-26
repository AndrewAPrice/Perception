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
	size_t GetItemAddress() const;
	size_t Address() const;

protected:
	PermebufBase* buffer_;
	size_t offset_;
};

class PermebufListOfBooleans : public PermebufList<PermebufListOfBooleans> {
public:
	PermebufListOfBooleans(PermebufBase* buffer, size_t offset);
	bool Get() const;
	void Set(bool value);
	static size_t GetSizeInBytes(PermebufBase* buffer);
};

template <class T>
class PermebufListOfEnums : public PermebufList<PermebufListOfEnums<T>> {
public:
	PermebufListOfEnums(PermebufBase* buffer, size_t offset);
	T Get() const;
	void Set(T value);
/*
	static template <class T> size_t GetSizeInBytes(PermebufBase* buffer) {
		return buffer->GetAddressSizeInBytes() + 2;
	}*/
};

template <class T>
class PermebufListOf : public PermebufList<PermebufListOf<T>> {
public:
	PermebufListOf(PermebufBase* buffer, size_t offset);
	bool Has() const;
	T Get() const;
	void Set(T value);
	void Clear();
	static size_t GetSizeInBytes(PermebufBase* buffer);
};

template <class T>
class PermebufListOfOneofs : public PermebufList<PermebufListOfOneofs<T>> {
public:
	PermebufListOfOneofs(PermebufBase* buffer, size_t offset);
	bool Has() const;
	T Get() const;
	void Set(T value);
	void Clear();
};


class PermebufListOfStrings : public PermebufList<PermebufListOfStrings> {
public:
	PermebufListOfStrings(PermebufBase* buffer, size_t offset);
	PermebufString Get() const;
	bool Has() const;
	void Set(PermebufString value);
	void Set(std::string_view value);
	void Clear();
};

class PermebufListOfBytes : public PermebufList<PermebufListOfBytes> {
public:
	PermebufListOfBytes(PermebufBase* buffer, size_t offset);
	bool Has() const;
	PermebufBytes Get() const;
	void Set(PermebufBytes value);
	void Set(void* value, size_t length);
	void Clear();
};

template <class T>
class PermebufListOfNumbers : public PermebufList<PermebufListOfNumbers<T>> {
public:
	PermebufListOfNumbers(PermebufBase* buffer, size_t offset);
	T Get() const;
	void Set(T const);
};

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

	template <class T> PermebufListOf<T> AllocateListOf() {
		return PermebufListOf<T>(this, AllocateMessage(PermebufListOf<T>::GetSizeInBytes(this)));
	}

	template <class T> T AllocateMessage() {
		return T(this, AllocateMessage(T::GetSizeInBytes(this)));
	}

	PermebufAddressSize GetAddressSize() const;
	size_t GetAddressSizeInBytes() const { return 1 << static_cast<size_t>(GetAddressSize()); }

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
	// Creates a new Permebuf.
	Permebuf(PermebufAddressSize address_size = PermebufAddressSize::BITS_16) : PermebufBase(address_size) {
		// Allocate the first message in the Permebuf.
		(void)AllocateMessage<Ref>();
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

// Represents a small 32-byte message.
class PermebufMiniMessage {
public:
	PermebufMiniMessage();
	void Serialize(size_t& a, size_t& b, size_t& c, size_t& d) const;
	void Deserialize(size_t a, size_t b, size_t c, size_t d);

protected:
	union {
		uint8_t bytes[32];
		size_t words[4];
	};
};

#include "permebuf_implementation.inl"
