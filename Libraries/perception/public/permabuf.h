#pragma once

#include <stddef.h>
#include <stdint.h>

// Determines the address size. Larger addresses allow the overall Permabuf to grow larger,
// however, data structures take up more memory.
enum class PermabufAddressSize {
	// 8-bit addresses allow Permabufs up to 256 bytes.
	BITS_8 = 0,
	// 16-bit addresses allow Permabufs up to 64 KB.
	BITS_16 = 1,
	// 32-bit adddresses allow Permabufs up to 4 GB.
	BITS_32 = 2,
	// 64-bit addresses allow Permabufs up to 16 EB.
	BITS_64 = 3
};

class PermabufBase;

class PermabufString {
public:
	PermabufString(PermabufBase* buffer, size_t offset);
};

class PermabufArray/* todo */ {
public:
};

class PermabufBase {
public:
	uint8_t Read1Byte(size_t address) const;
	uint16_t Read2Bytes(size_t address) const;
	uint32_t Read4Bytes(size_t address) const;
	uint64_t Read8Bytes(size_t address) const;
	size_t ReadPointer(size_t address) const;
	size_t ReadVariableLengthNumber(size_t address, int& bytes) const;

	void Write1Byte(size_t address, uint8_t value);
	void Write2Bytes(size_t address, uint16_t value);
	void Write4Bytes(size_t address, uint32_t value);
	void Write8Bytes(size_t address, uint64_t value);
	void WritePointer(size_t address);

	PermabufString AllocateString(std::string_view value);
	template <class T> T AllocateMessage() {
		return T(this, AllocateMessage(T::GetSizeInBytes(this)));
	}
protected:
	Permabuf(PermabufAddressSize address_size);
	Permabuf(const void* ptr, size_t size);
	virtual ~Permabuf();

private:
	size_t AllocateMessage(size_t size);
};

template <class T>
class Permabuf : PermabufBase {
public:
	Permabuf(PermabufAddressSize address_size = PermabufAddressSize::BITS_16) : PermabufBase(address_size) {
		AllocateMessage<T::Ref>();
	}
	Permabuf(const void* ptr, size_t size) : PermabufBase(ptr, size);
	virtual ~Permabuf() {}

	// Copying is heavy weight, so it is forbidden. You can use .clone() instead.
	Permabuf(const Permabuf<T>&) = delete;
	void operator=(const Permabuf<T>&) = delete;

	Permabuf<T> Clone() const {
		// TODO
	}

	const T::Ref operator-> () const {
		return T::Ref(this, first_byte);
	}

	T::Ref operator-> () {
		return T::Ref(this, first_byte);
	}

private:

}