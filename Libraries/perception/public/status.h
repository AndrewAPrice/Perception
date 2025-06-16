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
  OUT_OF_MEMORY = 5,
  INVALID_ARGUMENT = 6,
  OVERFLOW = 7,
  MISSING_MEDIA = 8,
  NOT_ALLOWED = 9,
  FILE_NOT_FOUND = 10
};

}

template <class T>
class StatusOr {
 public:
  StatusOr() : status_(::perception::Status::UNIMPLEMENTED) {}
  StatusOr(T&& value)
      : status_(::perception::Status::OK), value_(std::move(value)) {}
  StatusOr(T& value) : status_(::perception::Status::OK), value_(value) {}
  StatusOr(::perception::Status status) : status_(status) {}

  bool Ok() const { return status_ == ::perception::Status::OK; }

  operator bool() const { return Ok(); }

  ::perception::Status Status() const { return status_; }

  T& operator*() { return value_; }

  const T& operator*() const { return value_; }

  T* operator->() { return &value_; }

  const T* operator->() const { return &value_; }

 private:
  ::perception::Status status_;
  T value_;
};

namespace perception {

// Converts a Status -> Status. This seems silly, but is used for the macros
// below.
Status ToStatus(Status status);

// Converts a StatusOr -> Status.
template <class T>
Status ToStatus(const StatusOr<T>& status_or) {
  return status_or.Status();
}

}  // namespace perception

#define VAR_NAME_WITH_LINE2(id, name) id##name
#define VAR_NAME_WITH_LINE(id, name) VAR_NAME_WITH_LINE2(id, name)

#define RETURN_ON_ERROR(expr)                                               \
  auto VAR_NAME_WITH_LINE(__status__, __LINE__) =                           \
      ::perception::ToStatus(expr);                                         \
  if (VAR_NAME_WITH_LINE(__status__, __LINE__) != ::perception::Status::OK) \
    return VAR_NAME_WITH_LINE(__status__, __LINE__);

#define ASSIGN_OR_RETURN(var, expr)                                 \
  auto VAR_NAME_WITH_LINE(__status_or_var__, __LINE__) = (expr);    \
  RETURN_ON_ERROR(VAR_NAME_WITH_LINE(__status_or_var__, __LINE__)); \
  var = std::move(*std::move(VAR_NAME_WITH_LINE(__status_or_var__, __LINE__)));

// #undef VAR_NAME_WITH_LINE
