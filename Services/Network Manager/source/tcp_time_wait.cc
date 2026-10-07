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

#include "tcp_time_wait.h"

namespace {

// Maximum Segment Lifetime assumed for the network.
constexpr TcpTime kMaximumSegmentLifetime = std::chrono::seconds(30);

}  // namespace

TcpTimeWait::TcpTimeWait(TcpTime now) : deadline_(now + Duration()) {}

bool TcpTimeWait::OnSegment(bool fin, TcpTime now) {
  if (!fin) return false;
  deadline_ = now + Duration();
  return true;
}

TcpTime TcpTimeWait::Duration() { return kMaximumSegmentLifetime * 2; }
