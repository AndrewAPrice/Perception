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
#include "types.h"
#include "status.h"

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
	T Get(int index) const;
	bool Has(int index) const;
	void Set(int index, T value);
	void Clear(int index);

private:
	PermebufBase* buffer_;
	size_t length_;
	size_t first_item_address_;
};

template <class T>
class PermebufArrayOfOneOfs : public PermebufArray {
public:
	PermebufArrayOfOneOfs(PermebufBase* buffer, size_t offset);
	T Get(int index) const;
	bool Has(int index) const;
	void Set(int index, T value);
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
	PermebufList(PermebufBase* buffer = 0, size_t offset = 0);
	bool IsValid() const;
	bool HasNext() const;
	int Count() const;
	T InsertAfter() const;
	void SetNext(T next);
	T Next() const;
	T Get(int index) const;
	T RemoveNext() const;
	T RemoveAllAfter() const;
	size_t GetItemAddress() const;
	size_t Address() const;

	// For range for iteration:
	class Iterator {
	public:
		Iterator(PermebufBase* buffer, size_t offset) :
			buffer_(buffer), offset_(offset) {}
		Iterator operator++() {
			offset_ = T(buffer_, offset_).Next().offset_;
			return *this;
		}
		bool operator!=(const Iterator& other) const {
			return offset_ != other.offset_;
		}
		const auto operator*() const {
			return T(buffer_, offset_).Get();
		}
	private:
		PermebufBase* buffer_;
		size_t offset_;
	};

	Iterator begin() const {
		return Iterator(buffer_, offset_);
	}
	Iterator end() const { return Iterator(buffer_, 0); }

protected:
	PermebufBase* buffer_;
	size_t offset_;
};

class PermebufListOfBooleans : public PermebufList<PermebufListOfBooleans> {
public:
	PermebufListOfBooleans(PermebufBase* buffer = 0, size_t offset = 0);
	bool Get() const;
	void Set(bool value);
	static size_t GetSizeInBytes(PermebufBase* buffer);
	static PermebufListOfBooleans Allocate(PermebufBase* buffer);
};

template <class T>
class PermebufListOfEnums : public PermebufList<PermebufListOfEnums<T>> {
public:
	PermebufListOfEnums(PermebufBase* buffer = 0, size_t offset = 0);
	T Get() const;
	void Set(T value);
	static size_t GetSizeInBytes(PermebufBase* buffer);
	static PermebufListOfEnums<T> Allocate(PermebufBase* buffer);
};

template <class T>
class PermebufListOf : public PermebufList<PermebufListOf<T>> {
public:
	PermebufListOf(PermebufBase* buffer = 0, size_t offset = 0);
	bool Has() const;
	T Get() const;
	void Set(T value);
	void Clear();
	static size_t GetSizeInBytes(PermebufBase* buffer);
	static PermebufListOf<T> Allocate(PermebufBase* buffer);
};

template <class T>
class PermebufListOfOneOfs : public PermebufList<PermebufListOfOneOfs<T>> {
public:
	PermebufListOfOneOfs(PermebufBase* buffer = 0, size_t offset = 0);
	bool Has() const;
	T Get() const;
	void Set(T value);
	void Clear();
	static size_t GetSizeInBytes(PermebufBase* buffer);
	static PermebufListOfOneOfs<T> Allocate(PermebufBase* buffer);
};


class PermebufListOfStrings : public PermebufList<PermebufListOfStrings> {
public:
	PermebufListOfStrings(PermebufBase* buffer = 0, size_t offset = 0);
	PermebufString Get() const;
	bool Has() const;
	void Set(PermebufString value);
	void Set(std::string_view value);
	void Clear();
	static size_t GetSizeInBytes(PermebufBase* buffer);
	static PermebufListOfStrings Allocate(PermebufBase* buffer);
};

class PermebufListOfBytes : public PermebufList<PermebufListOfBytes> {
public:
	PermebufListOfBytes(PermebufBase* buffer = 0, size_t offset = 0);
	bool Has() const;
	PermebufBytes Get() const;
	void Set(PermebufBytes value);
	void Set(void* value, size_t length);
	void Clear();
	static size_t GetSizeInBytes(PermebufBase* buffer);
	static PermebufListOfBytes Allocate(PermebufBase* buffer);
};

template <class T>
class PermebufListOfNumbers : public PermebufList<PermebufListOfNumbers<T>> {
public:
	PermebufListOfNumbers(PermebufBase* buffer = 0, size_t offset = 0);
	T Get() const;
	void Set(T const);
	static size_t GetSizeInBytes(PermebufBase* buffer);
	static PermebufListOfNumbers<T> Allocate(PermebufBase* buffer);
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
	static size_t GetBytesNeededForVariableLengthNumber(size_t value);

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

	template <class T> PermebufListOfEnums<T> AllocateListOfEnums() {
		return PermebufListOfEnums<T>(this,
			AllocateMessage(PermebufListOfEnums<T>::AllocateMemory(this)));
	}

	template <class T> PermebufListOf<T> AllocateListOf() {
		return PermebufListOf<T>(this, AllocateMemory(PermebufListOf<
			T>::GetSizeInBytes(this)));
	}

	template <class T> PermebufListOfOneOfs<T> AllocateListOfOneOfs() {
		return PermebufListOfOneOfs<T>(this,
			AllocateMemory(PermebufListOfOneOfs<T>::GetSizeInBytes(this)));
	}

	PermebufListOfBooleans AllocateListOfBooleans() {
		return PermebufListOfBooleans(this,
			AllocateMemory(PermebufListOfBooleans::GetSizeInBytes(this)));
	}

	PermebufListOfStrings AllocateListOfStrings() {
		return PermebufListOfStrings(this,
			AllocateMemory(PermebufListOfStrings::GetSizeInBytes(this)));
	}
	
	PermebufListOfBytes AllocateListOfBytes() {
		return PermebufListOfBytes(this,
			AllocateMemory(PermebufListOfBytes::GetSizeInBytes(this)));
	}

	template <class T> PermebufListOfNumbers<T> AllocateListOfNumbers() {
		return PermebufListOfNumbers<T>(this,
			AllocateMemory(PermebufListOfNumbers<T>::GetSizeInBytes(this)));
	}

	template <class T> T AllocateMessage() {
		return T(this, AllocateMessage(T::GetSizeInBytes(this)));
	}

	template <class T> T AllocateOneOf() {
		return T(this, AllocateMemory(T::GetSizeInBytes(this)));
	}

	PermebufAddressSize GetAddressSize() const;
	size_t GetAddressSizeInBytes() const { return 1 << static_cast<size_t>(GetAddressSize()); }

	// Release the memory. Writing to the Permebuf after this is bad!!!
	// Returns true if the operation was successful.
	bool ReleaseMemory(void** start, size_t* pages, size_t* size);

protected:
	PermebufBase(PermebufAddressSize address_size);
	PermebufBase(void* start_of_memory, size_t size);
	virtual ~PermebufBase();

	PermebufAddressSize address_size_;

	// Start of the first page.
	void* start_of_memory_;

	// Size of the Permebuf.
	size_t size_;

private:
	size_t AllocateMessage(size_t size);
	size_t AllocateMemory(size_t size);
	bool GrowTo(size_t size);
	size_t GetNumberOfAllocatedMemoryPages() const;
};

template <class T>
class Permebuf : public PermebufBase {
public:
	// Creates a new Permebuf.
	Permebuf(PermebufAddressSize address_size = PermebufAddressSize::BITS_16) : PermebufBase(address_size) {
		// Allocate the first message in the Permebuf.
		root_ = AllocateMessage<T>();
	}

	// Wraps a Permabug around raw memory. This memory must be page aligned, and we take ownership
	// of the memory.
	Permebuf(void* start_of_memory, size_t size) :
		PermebufBase(start_of_memory, size) {
		// Set the first message to be at the start of the Permebuf, just
		// beyond the initial metadata.
		root_ = T(this, 1);
	}

	// Allow moving the object with std::move.
  	Permebuf(Permebuf<T>&& other) :
  		PermebufBase(other.start_of_memory_, other.size_) {
  		root_ = T(this, 1);
  		other.start_of_memory_ = nullptr;
  		other.size_ = 0;
  	}

	Permebuf<T>& operator=(Permebuf<T>&& other) {
		start_of_memory_ = other.start_of_memory_;
		size_ = other.size_;
		address_size_ = other.address_size_;
  		root_ = other.root_;
  		other.start_of_memory_ = nullptr;
  		other.size_ = 0;
		return *this;
  	}

	virtual ~Permebuf() {}

	// Copying is heavy weight, so it is forbidden. You can use .clone() instead.
	Permebuf(const Permebuf<T>&) = delete;
	void operator=(const Permebuf<T>&) = delete;

	Permebuf<T> Clone() const {
		// TODO
	}

	const T* operator->() const {
		return &root_;
	}

	T* operator->() {
		return &root_;
	}

	const T& operator*() const {
		return root_;
	}

	T& operator*() {
		return root_;
	}

private:
	T root_;
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

class PermebufServer;

class PermebufService {
public:
	PermebufService();
	PermebufService(::perception::ProcessId process_id, ::perception::MessageId message_id);

	::perception::ProcessId GetProcessId() const;

	::perception::MessageId GetMessageId() const;

	// Does this service refer to the same instance as another service?
	bool operator==(const PermebufService& other) const;
	bool operator==(const PermebufServer& other) const;

	// Is this a valid instance of a service?
	operator bool() const {
		// The kernel can't have services.
		return process_id_ != 0;
	}

	bool operator<(const PermebufService& other) const {
		return process_id_ < other.process_id_ ||
			(process_id_ == other.process_id_ &&
			message_id_ < other.message_id_);
	}

	bool IsValid() const {
		return process_id_ != 0;
	}

protected:
	::perception::ProcessId process_id_;
	::perception::MessageId message_id_;

	template <class O>
	::perception::Status SendMiniMessage(size_t message_id,
		const O& request) const;

	template <class O>
	::perception::Status SendMessage(size_t message_id, Permebuf<O> request)
		const;

	template <class O, class I>
	StatusOr<I> SendMiniMessageAndWaitForMiniMessage(
		size_t function_id, const O& request) const;

	template <class O, class I>
	StatusOr<Permebuf<I>>
		SendMiniMessageAndWaitForMessage(size_t function_id,
			const O& request) const;

	template <class O, class I>
	void SendMiniMessageAndNotifyOnMiniMessage(
		size_t function_id, const O& request,
		const std::function<void(StatusOr<I>)>& on_response) const;

	template <class O, class I>
	void SendMiniMessageAndNotifyOnMessage(
		size_t function_id, const O& request,
		const std::function<
			void(StatusOr<Permebuf<I>>)>& on_response) const;

	template <class O, class I>
	StatusOr<I> SendMessageAndWaitForMiniMessage(
		size_t function_id, Permebuf<O> request) const;

	template <class O, class I>
	StatusOr<Permebuf<I>>
	SendMessageAndWaitForMessage(size_t function_id,
		Permebuf<O> request) const;

	template <class O, class I>
	void SendMessageAndNotifyOnMiniMessage(
		size_t function_id, Permebuf<O> request,
		const std::function<void(StatusOr<I>)>& on_response) const;

	template <class O, class I>
	void SendMessageAndNotifyOnMessage(
		size_t function_id, Permebuf<O> request,
		const std::function<
			void(StatusOr<Permebuf<I>>)>& on_response) const;

	// TODO: Implement streams.
};

// TODO: Implement streams.

class PermebufServer {
public:
	PermebufServer(std::string_view service_name);
	virtual ~PermebufServer();

	::perception::ProcessId GetProcessId() const;

	::perception::MessageId GetMessageId() const;

	// Does this service refer to the same instance as another service?
	bool operator==(const PermebufService& other) const;
	bool operator==(const PermebufServer& other) const;

	template <class I>
	bool ProcessMiniMessage(::perception::ProcessId sender,
		size_t metadata, size_t param1, size_t param2, size_t param3,
		size_t param4, size_t param5,
		const std::function<void(::perception::ProcessId,
			const I&)>& handler);

	template <class I, class O>
	bool ProcessMiniMessageForMiniMessage(::perception::ProcessId sender,
		size_t metadata, size_t param1, size_t param2, size_t param3,
		size_t param4, size_t param5,
		const std::function<StatusOr<O>(::perception::ProcessId,
			const I&)>& handler);

	template <class I, class O>
	bool ProcessMiniMessageForMessage(::perception::ProcessId sender,
		size_t metadata, size_t param1, size_t param2, size_t param3,
		size_t param4, size_t param5,
		const std::function<StatusOr<Permebuf<O>>(::perception::ProcessId,
			const I&)>& handler);

	template <class I>
	bool ProcessMessage(::perception::ProcessId sender,
		size_t metadata, size_t param1, size_t param2, size_t param3,
		size_t param4, size_t param5,
		const std::function<void(::perception::ProcessId,
			Permebuf<I>)>& handler);

	template <class I, class O>
	bool ProcessMessageForMiniMessage(::perception::ProcessId sender,
		size_t metadata, size_t param1, size_t param2, size_t param3,
		size_t param4, size_t param5,
		const std::function<StatusOr<O>(::perception::ProcessId,
			Permebuf<I>)>& handler);

	template <class I, class O>
	bool ProcessMessageForMessage(::perception::ProcessId sender,
		size_t metadata, size_t param1, size_t param2, size_t param3,
		size_t param4, size_t param5,
		const std::function<StatusOr<Permebuf<O>>(::perception::ProcessId,
			Permebuf<I>)>& handler);

protected:
	size_t GetFunctionNumberFromMetadata(size_t metadata);

	virtual bool DelegateMessage(::perception::ProcessId sender,
		size_t metadata, size_t param_1, size_t param_2,
		size_t param_3, size_t param_4, size_t param_5) = 0;

	void MessageHandler(::perception::ProcessId sender,
		size_t metadata, size_t param_1, size_t param_2,
		size_t param_3, size_t param_4, size_t param_5);

	template <class O>
	void ReplyWithStatusOrMiniMessage(
		::perception::ProcessId process,
		::perception::MessageId response_channel,
		StatusOr<O> status_or_minimessage);

	template <class O>
	void ReplyWithStatusOrMessage(
		::perception::ProcessId process,
		::perception::MessageId response_channel,
		StatusOr<Permebuf<O>> status_or_message);

	void ReplyWithStatus(
		::perception::ProcessId process,
		::perception::MessageId response_channel,
		::perception::Status status);

	::perception::MessageId message_id_;

};

#include "permebuf_implementation.inl"
