#pragma once

#include <stddef.h>
#include <stdint.h>

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
};

class PermebufArray/* todo */ {
public:
};

class PermebufBase {
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

	PermebufString AllocateString(std::string_view value);
	template <class T> T AllocateMessage() {
		return T(this, AllocateMessage(T::GetSizeInBytes(this)));
	}
protected:
	Permebuf(PermebufAddressSize address_size);
	Permebuf(const void* ptr, size_t size);
	virtual ~Permebuf();

private:
	size_t AllocateMessage(size_t size);
};

template <class T>
class Permebuf : PermebufBase {
public:
	Permebuf(PermebufAddressSize address_size = PermebufAddressSize::BITS_16) : PermebufBase(address_size) {
		AllocateMessage<T::Ref>();
	}
	Permebuf(const void* ptr, size_t size) : PermebufBase(ptr, size);
	virtual ~Permebuf() {}

	// Copying is heavy weight, so it is forbidden. You can use .clone() instead.
	Permebuf(const Permebuf<T>&) = delete;
	void operator=(const Permebuf<T>&) = delete;

	Permebuf<T> Clone() const {
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