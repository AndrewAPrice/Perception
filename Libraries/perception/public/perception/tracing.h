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

#pragma once

// Switch for system tracing. Comment out to compile out all tracing code.
// #define ENABLE_TRACING

#ifdef ENABLE_TRACING

#ifndef KERNEL
#include <string_view>
#endif

#include "types.h"

namespace perception {

struct TraceContext {
  uint64 trace_id;
  uint64 span_id;
  uint64 parent_span_id;
};

// Gets the current thread's active trace context.
TraceContext GetCurrentTraceContext();

// Sets the current thread's active trace context.
void SetCurrentTraceContext(const TraceContext& context);

// Registers a string and returns a unique 16-bit string ID.
uint16 RegisterTraceString(const char* str);

// Emits raw binary trace data to Channel 2.
void EmitTraceBytes(const char* data, size_t size);

#ifndef KERNEL
// RAII helper for creating a named trace span.
class ScopedTraceSpan {
 public:
  // Creates a trace span with a name and optional category.
  ScopedTraceSpan(std::string_view name, std::string_view category = "app");

  // Closes the trace span.
  ~ScopedTraceSpan();

 private:
  uint64 span_id_;
};

// Explicit / lambda-captured helper for creating asynchronous trace spans.
class AsyncTraceSpan {
 public:
  AsyncTraceSpan(std::string_view name, std::string_view category = "rpc_out");
  ~AsyncTraceSpan();
  void End();

 private:
  uint64 span_id_;
  bool ended_ = false;
};

// Emits an instant trace event with an optional category.
void EmitInstantTraceEvent(std::string_view name,
                           std::string_view category = "app");
#endif

}  // namespace perception

#define PERCEPTION_TRACE_SPAN(name) \
  ::perception::ScopedTraceSpan trace_span_##__LINE__(name)

#define PERCEPTION_TRACE_SPAN_CAT(name, cat) \
  ::perception::ScopedTraceSpan trace_span_##__LINE__(name, cat)

#define PERCEPTION_TRACE_EVENT(name) ::perception::EmitInstantTraceEvent(name)

#define PERCEPTION_TRACE_EVENT_CAT(name, cat) \
  ::perception::EmitInstantTraceEvent(name, cat)

#else

namespace perception {
class AsyncTraceSpan;
}

#define PERCEPTION_TRACE_SPAN(name) ((void)0)
#define PERCEPTION_TRACE_SPAN_CAT(name, cat) ((void)0)
#define PERCEPTION_TRACE_EVENT(name) ((void)0)
#define PERCEPTION_TRACE_EVENT_CAT(name, cat) ((void)0)

#endif  // ENABLE_TRACING
