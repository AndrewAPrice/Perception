// Copyright 2026 Google LLC
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

#include <dlfcn.h>

#include <string>

#include "perception/loader.h"
#include "perception/services.h"

extern "C" {

namespace {
thread_local std::string last_error = "";
thread_local std::string returned_error = "";
}  // namespace

void* dlopen(const char* filename, int flag) {
  if (filename == nullptr || filename[0] == '\0') {
    // Return a dummy handle representing the process itself.
    return (void*)1;
  }
  last_error = "dlopen: loading external libraries is not supported";
  return nullptr;
}

void* dlsym(void* handle, const char* symbol) {
  if (handle != (void*)1) {
    last_error = "dlsym: invalid handle";
    return nullptr;
  }

  if (symbol == nullptr) {
    last_error = "dlsym: symbol name is null";
    return nullptr;
  }

  ::perception::ResolveSymbolRequest request;
  request.symbol_name = symbol;

  auto response =
      ::perception::GetService<::perception::Loader>().ResolveSymbol(request);
  if (!response.Ok()) {
    last_error = "dlsym: IPC failed";
    return nullptr;
  }

  if (response->address == 0) {
    last_error = "dlsym: symbol not found";
    return nullptr;
  }

  return (void*)response->address;
}

int dlclose(void* handle) {
  if (handle != (void*)1) {
    last_error = "dlclose: invalid handle";
    return -1;
  }
  return 0;
}

char* dlerror() {
  if (last_error.empty()) return nullptr;

  returned_error = last_error;
  last_error.clear();
  return returned_error.data();
}

}  // extern "C"
