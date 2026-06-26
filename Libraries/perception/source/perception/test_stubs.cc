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

#include <functional>
#include <iostream>
#include <cstdlib>
#include "perception/messages.h"
#include "perception/fibers.h"
#include "perception/scheduler.h"
#include "perception/service_client.h"
#include "perception/service_server.h"
#include "perception/shared_memory.h"
#include "perception/permissions.h"
#include "perception/processes.h"
#include "perception/memory.h"

namespace perception {

// Message stubs
MessageId GenerateUniqueMessageId() {
  static MessageId next_id = 1;
  return next_id++;
}

void RegisterMessageHandler(
    MessageId message_id,
    std::function<void(ProcessId, const MessageData&)> handler) {}

void UnregisterMessageHandler(MessageId message_id) {}

void RegisterWakeUpHandler(MessageId message_id) {}

void SleepAndGetRawMessage(MessageId message_id, ProcessId& sender,
                           MessageData& message_data) {}

Status SendMessage(ProcessId pid, const MessageData& message_data) {
  return Status::UNIMPLEMENTED;
}

void DealWithUnhandledMessage(ProcessId sender,
                              const MessageData& message_data) {}

// Scheduler / Fiber stubs
void Defer(const std::function<void()>& function) {
  function();
}

void Defer(std::function<void()>&& function) {
  function();
}

void Sleep() {}

Fiber::Fiber(bool custom_stack) {}
Fiber::Fiber(ThreadId thread_id) {}
Fiber::~Fiber() {}
void Fiber::WakeUp() {}
void Fiber::SwitchTo() {}
void Fiber::JumpTo() {}

Fiber* GetCurrentlyExecutingFiber() {
  static Fiber dummy(false);
  return &dummy;
}

// Memory Allocation stubs
void* AllocateMemoryPages(size_t number_of_pages) {
  return std::malloc(number_of_pages * 4096);
}

void ReleaseMemoryPages(void* ptr, size_t number_of_pages) {
  std::free(ptr);
}

// Process / Permissions stubs
ProcessId GetProcessId() {
  return 123; // Test Process ID
}

std::string GetProcessName(ProcessId pid) {
  return "TestProcess";
}

bool DoesProcessHavePermission(ProcessId pid, Permission permission) {
  return true; // Grant all permissions during tests
}

// ServiceClient stubs
ServiceClient::ServiceClient(ProcessId process_id, MessageId message_id)
    : process_id_(process_id), message_id_(message_id) {}
void ServiceClient::Serialize(serialization::Serializer& serializer) {}
ProcessId ServiceClient::ServerProcessId() const { return process_id_; }
MessageId ServiceClient::ServiceId() const { return message_id_; }
bool ServiceClient::operator<(const ServiceClient& rhs) const { return message_id_ < rhs.message_id_; }
bool ServiceClient::IsValid() const { return true; }
MessageId ServiceClient::NotifyOnDisappearance(const std::function<void()>& on_disappearance) { return 0; }
void ServiceClient::StopNotifyingOnDisappearance(MessageId message_id) {}
void ServiceClient::MaybeHandleUnexpectedMemoryInResponse(ProcessId process_id, const MessageData& message) {}
void ServiceClient::PrepareRequestMessage(size_t method_id, MessageData& message) {}
void ServiceClient::PrepareRequestMessageWithoutParameters(size_t method_id, MessageData& message) {}

// ServiceServer stubs
ServiceServer::ServiceServer(ServiceServerOptions options, std::string_view service_name)
    : options_(options), message_id_(0), service_name_(service_name) {}
ServiceServer::~ServiceServer() {}
ProcessId ServiceServer::ServerProcessId() const { return 0; }
MessageId ServiceServer::ServiceId() const { return message_id_; }
void ServiceServer::StartServing() {}
bool ServiceServer::operator<(const ServiceServer& rhs) const { return message_id_ < rhs.message_id_; }
void ServiceServer::HandleUnknownRequest(ProcessId sender, const MessageData& params) {}
void ServiceServer::HandleUnexpectedMessageInRequest(ProcessId sender, const MessageData& message) {}

// Shared memory helper
std::shared_ptr<SharedMemory> GetMemoryBufferForSendingToProcess(
    ProcessId process_id) {
  return nullptr;
}

std::shared_ptr<SharedMemory> GetMemoryBufferForSendingToProcessRegardlessOfIfInUse(
    ProcessId process_id, size_t shared_memory_id) {
  return nullptr;
}

std::shared_ptr<SharedMemory> GetMemoryBufferForReceivingFromProcess(
    ProcessId process_id, size_t shared_memory_id) {
  return nullptr;
}

void SetMemoryBufferAsReadyForSendingNextMessageToProcess(
    SharedMemory& shared_memory) {}

}  // namespace perception
