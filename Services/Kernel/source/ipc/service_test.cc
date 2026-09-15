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

#include "ipc/service.h"
#include "ipc/service_directory.h"
#include "processes/process.h"
#include "ipc/messages.h"
#include "containers/object_pools.h"
#include "testing.h"
#include "common/kernel_string.h"
#include <stdlib.h>

using containers::InitializeObjectPools;
using ipc::FindServiceByProcessAndMid;
using ipc::GetNextQueuedMessage;
using ipc::InitializeServices;
using ipc::Message;
using ipc::NotifyProcessWhenServiceAppears;
using ipc::NotifyProcessWhenServiceDisappears;
using ipc::RegisterService;
using ipc::Service;
using ipc::ServiceDirectory;
using ipc::UnregisterServiceByMessageId;
using processes::CreateProcess;
using processes::InitializeProcesses;
using processes::kProcessNameLength;
using processes::Process;

namespace {

Process* CreateTestProcess(const char* name) {
  Process* p = CreateProcess(false, false);
  if (p != nullptr)
    common::CopyString(name, kProcessNameLength, strlen(name), p->name);
  return p;
}

void SetServiceName(char* dest, const char* src) {
  memset(dest, 0, 72);
  strncpy(dest, src, 71);
}

}  // namespace

TEST(ServiceRegistrationAndLookupTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeServices();

  Process* p1 = CreateTestProcess("Process1");
  ASSERT(p1 != nullptr, true);

  char name1[72];
  char name2[72];
  SetServiceName(name1, "my_cool_service");
  SetServiceName(name2, "another_cool_service");

  // Register two services
  RegisterService(name1, p1, 101);
  RegisterService(name2, p1, 102);

  // Lookup the registered services
  Service* svc1 = FindServiceByProcessAndMid(p1->pid, 101);
  ASSERT(svc1 != nullptr, true);
  ASSERT(svc1->message_id, (size_t)101);

  Service* svc2 = FindServiceByProcessAndMid(p1->pid, 102);
  ASSERT(svc2 != nullptr, true);
  ASSERT(svc2->message_id, (size_t)102);

  // Unregister the second service (larger message ID)
  UnregisterServiceByMessageId(p1, 102);
  svc2 = FindServiceByProcessAndMid(p1->pid, 102);
  ASSERT(svc2 == nullptr, true);

  // Unregister the first service
  UnregisterServiceByMessageId(p1, 101);
  svc1 = FindServiceByProcessAndMid(p1->pid, 101);
  ASSERT(svc1 == nullptr, true);
}

TEST(ServiceNotificationsTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeServices();

  Process* p1 = CreateTestProcess("Process1");
  Process* p2 = CreateTestProcess("Process2");
  ASSERT(p1 != nullptr, true);
  ASSERT(p2 != nullptr, true);

  char name[72];
  SetServiceName(name, "my_cool_service");

  // p2 listens for "my_cool_service"
  NotifyProcessWhenServiceAppears(name, p2, 202);

  // Verify no notification received yet
  ASSERT(p2->messages_queued, (size_t)0);

  // Register service under p1
  RegisterService(name, p1, 101);

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

TEST(ServiceDirectoryDirectMethodsTest) {
  InitializeObjectPools();
  InitializeProcesses();
  ServiceDirectory::Get().Initialize();

  Process* p = CreateTestProcess("ProcessDirTest");
  ASSERT(p != nullptr, true);

  char name[72];
  SetServiceName(name, "direct_service");

  ServiceDirectory::Get().Register(name, p, 555);
  Service* svc = ServiceDirectory::Get().FindByProcessAndMid(p->pid, 555);
  ASSERT(svc != nullptr, true);
  ASSERT(svc->message_id, (size_t)555);

  ServiceDirectory::Get().UnregisterByMessageId(p, 555);
  svc = ServiceDirectory::Get().FindByProcessAndMid(p->pid, 555);
  ASSERT(svc == nullptr, true);
}

