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

// This file is inlined into the header permebuf.h because it contains templated
// implementations and must be part of the header. But, they are in separate files
// for better organization.

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
			return static_cast<T>(0);
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
			buffer_->Write2Bytes(
				first_item_address_ + index * 2,
				reinterpret_cast<uint16_t>(value));
			break;
		case 4:
			buffer_->Write4Bytes(
				first_item_address_ + index * 4,
				reinterpret_cast<uint32_t>(value));
			break;
		case 8:
			buffer_->Write8Bytes(
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

template <class T>
size_t PermebufList<T>::Address() const {
	return offset_;
}

template <class T>
PermebufListOfEnums<T>::PermebufListOfEnums(PermebufBase* buffer, size_t offset)
	: PermebufList<PermebufListOfEnums<T>>(buffer, offset) {}

template <class T>
T PermebufListOfEnums<T>::Get() const {
	if (!this->IsValid()) {
		return static_cast<T>(0);
	}
	return static_cast<T>(this->buffer_->Read2Bytes(this->GetItemAddress()));
}

template <class T>
void PermebufListOfEnums<T>::Set(T value) {
	if (!this->IsValid()) {
		return;
	}
	this->buffer_->Write2Bytes(this->GetItemAddress(), static_cast<uint16_t>(value));
}

template <class T>
PermebufListOf<T>::PermebufListOf(PermebufBase* buffer, size_t offset)
	: PermebufList<PermebufListOf<T>>(buffer, offset) {}

template <class T>
bool PermebufListOf<T>::Has() const {
	if (!this->IsValid()) {
		return false;
	}
	return this->buffer_->ReadPointer(this->GetItemAddress()) != 0;
}

template <class T>
T PermebufListOf<T>::Get() const {
	if (!this->IsValid()) {
		return T(this->buffer_, 0);
	}
	return T(this->buffer_, this->buffer_->ReadPointer(this->GetItemAddress()));
}

template <class T>
void PermebufListOf<T>::Set(T value) {
	if (!this->IsValid()) {
		return;
	}
	this->buffer_->WritePointer(this->GetItemAddress(), value);
}

template <class T>
void PermebufListOf<T>::Clear() {
	if (!this->IsValid()) {
		return;
	}
	this->buffer_->WritePointer(this->GetItemAddress(), 0);
}

template <class T>
size_t PermebufListOf<T>::GetSizeInBytes(PermebufBase* buffer) {
		return buffer->GetAddressSizeInBytes() * 2;
	}

template <class T>
PermebufListOfOneofs<T>::PermebufListOfOneofs(PermebufBase* buffer, size_t offset)
	: PermebufList<PermebufListOfOneofs<T>>(buffer, offset) {}

template <class T>
bool PermebufListOfOneofs<T>::Has() const {
	if (!this->IsValid()) {
		return false;
	}
	return this->buffer_->ReadPointer(this->GetItemAddress() + 2) != 0;
}

template <class T>
T PermebufListOfOneofs<T>::Get() const {
	if (!this->IsValid()) {
		return T(this->buffer_, 0);
	}
	return T(this->buffer_, this->GetItemAddress());
}

template <class T>
void PermebufListOfOneofs<T>::Set(T value) {
	if (!this->IsValid()) {
		return;
	}

	size_t source_oneof_offset = value.Address();

	size_t destination_oneof_offset = this->GetItemAddress();

	this->buffer_->Write2Bytes(destination_oneof_offset,
		this->buffer_->Read2Bytes(source_oneof_offset));
	this->buffer_->WritePointer(destination_oneof_offset + 2,
		this->buffer_->ReadPointer(source_oneof_offset + 2));
}

template <class T>
void PermebufListOfOneofs<T>::Clear() {
	if (!this->IsValid()) {
		return;
	}

	size_t destination_oneof_offset = this->GetItemAddress();

	this->buffer_->Write2Bytes(destination_oneof_offset,
		0);
	this->buffer_->WritePointer(destination_oneof_offset + 2,
		0);
}

template <class T>
PermebufListOfNumbers<T>::PermebufListOfNumbers(PermebufBase* buffer, size_t offset)
	: PermebufList<PermebufListOfNumbers<T>>(buffer, offset) {}

template <class T>
T PermebufListOfNumbers<T>::Get() const {
	if (!this->IsValid()) {
		return T(this->buffer_, 0);
	}

	switch(sizeof(T)) {
		case 1:
			return reinterpret_cast<T>(this->buffer_->Read1Byte(
				this->GetItemAddress()));
		case 2:
			return reinterpret_cast<T>(this->buffer_->Read2Bytes(
				this->GetItemAddress()));
		case 4:
			return reinterpret_cast<T>(this->buffer_->Read4Bytes(
				this->GetItemAddress()));
		case 8:
			return reinterpret_cast<T>(this->buffer_->Read8Bytes(
				this->GetItemAddress()));
		default:
			return static_cast<T>(0);
	}
}

template <class T>
void PermebufListOfNumbers<T>::Set(T value) {
	if (!this->IsValid()) {
		return;
	}

	switch(sizeof(T)) {
		case 1:
			this->buffer_->Write1Byte(
				this->GetItemAddress(),
				reinterpret_cast<uint8_t>(value));
			break;
		case 2:
			this->buffer_->Write2Byte(
				this->GetItemAddress(),
				reinterpret_cast<uint16_t>(value));
			break;
		case 4:
			this->buffer_->Write3Byte(
				this->GetItemAddress(),
				reinterpret_cast<uint32_t>(value));
			break;
		case 8:
			this->buffer_->Write1Byte(
				this->GetItemAddress(),
				reinterpret_cast<uint64_t>(value));
			break;
		default:
			break;
	}
}
