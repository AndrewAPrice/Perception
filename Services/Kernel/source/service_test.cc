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

#include "service.h"
#include "process.h"
#include "messages.h"
#include "object_pools.h"
#include "testing.h"
#include "kernel_string.h"
#include <stdlib.h>

namespace {

Process* CreateTestProcess(const char* name) {
  Process* p = CreateProcess(false, false);
  if (p != nullptr) {
    CopyString(name, PROCESS_NAME_LENGTH, strlen(name), p->name);
  }
  return p;
}

}  // namespace

TEST(ServiceRegistrationAndLookupTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeServices();

  Process* p1 = CreateTestProcess("Process1");
  ASSERT(p1 != nullptr, true);

  // Register a service
  RegisterService((char*)"my_cool_service", p1, 101);

  // Lookup the registered service
  Service* svc = FindServiceByProcessAndMid(p1->pid, 101);
  ASSERT(svc != nullptr, true);
  ASSERT(svc->process, p1);
  ASSERT(svc->message_id, (size_t)101);

  // Unregister the service
  UnregisterServiceByMessageId(p1, 101);
  svc = FindServiceByProcessAndMid(p1->pid, 101);
  ASSERT(svc == nullptr, true);
}

TEST(ServiceNotificationsTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeServices();

  Process* p1 = CreateTestProcess("Process1");
  Process* p2 = CreateTestProcess("Process2");
  ASSERT(p1 != nullptr, true);
  ASSERT(p2 != nullptr, true);

  // p2 listens for "my_cool_service"
  NotifyProcessWhenServiceAppears((char*)"my_cool_service", p2, 202);

  // Verify no notification received yet
  ASSERT(p2->messages_queued, (size_t)0);

  // Register service under p1
  RegisterService((char*)"my_cool_service", p1, 101);

  // Verify notification received by p2
  ASSERT(p2->messages_queued, (size_t)1);
  Message* msg = GetNextQueuedMessage(p2);
  ASSERT(msg != nullptr, true);
  ASSERT(msg->message_id, (size_t)202);
  ASSERT(msg->param1, p1->pid);
  ASSERT(msg->param2, (size_t)101);

  // Unregister the service
  UnregisterServiceByMessageId(p1, 101);

  // p2 registers for disappearance of the service (which is now gone)
  NotifyProcessWhenServiceDisappears(p2, p1->pid, 101, 303);

  // Since it's already gone, it should send the disappeared notification immediately
  ASSERT(p2->messages_queued, (size_t)1);
  msg = GetNextQueuedMessage(p2);
  ASSERT(msg != nullptr, true);
  ASSERT(msg->message_id, (size_t)303);
  ASSERT(msg->param1, (size_t)0);
}
