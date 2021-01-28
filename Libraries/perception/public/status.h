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

#include <utility>

namespace perception {

enum class Status {
	OK = 0,
	UNIMPLEMENTED = 1,
	INTERNAL_ERROR = 2,
	PROCESS_DOESNT_EXIST = 3,
	SERVICE_DOESNT_EXIST = 4,
	OUT_OF_MEMORY = 5
};

}

template<class T>
class StatusOr {
public:
	StatusOr(T& value) : status_(::perception::Status::OK), value_(std::move(value)) {}
	StatusOr(::perception::Status status) : status_(status) {}

	bool Ok() const {
		return status_ == ::perception::Status::OK;
	}

	::perception::Status Status() const {
		return status_;
	}

	T& operator*() {
		return value_;
	}

	const T& operator*() const {
		return value_;
	}

	T& operator->() {
		return value_;
	}

	const T& operator->() const {
		return value_;
	}
private:
	::perception::Status status_;
	T value_;
};
