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

#include <chrono>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "perception/fibers.h"
#include "perception/futex.h"
#include "perception/memory.h"
#include "perception/messages.h"
#include "perception/permissions.h"
#include "perception/processes.h"
#include "perception/registry.h"
#include "perception/scheduler.h"
#include "perception/service_client.h"
#include "perception/service_server.h"
#include "perception/services.h"
#include "perception/shared_memory.h"

namespace perception {

namespace {

struct TestRegistryKey {
  RegistryCorpus corpus;
  std::string r_namespace;
  std::string key;

  bool operator<(const TestRegistryKey& other) const {
    if (corpus != other.corpus) return corpus < other.corpus;
    if (r_namespace != other.r_namespace)
      return r_namespace < other.r_namespace;
    return key < other.key;
  }
};

struct TestRegistryListener {
  RegistryListenerToken token;
  TestRegistryKey key;
  std::function<void()> callback;
};

std::map<TestRegistryKey, serialization::Value>& GetTestRegistryStore() {
  static std::map<TestRegistryKey, serialization::Value> store;
  return store;
}

std::vector<TestRegistryListener>& GetTestRegistryListeners() {
  static std::vector<TestRegistryListener> listeners;
  return listeners;
}

void NotifyTestRegistryListeners(const TestRegistryKey& key) {
  std::vector<std::function<void()>> callbacks;
  for (const auto& listener : GetTestRegistryListeners()) {
    if (!(listener.key < key) && !(key < listener.key))
      callbacks.push_back(listener.callback);
  }
  for (const auto& cb : callbacks) {
    if (cb) cb();
  }
}

}  // namespace

bool WaitOnFutex(void* address, int value) {
  return false;
}

void WakeFutex(void* address, int value) {}

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

void DeferAfterEvents(std::function<void()>&& function) {
  if (function) function();
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

void DoesProcessHavePermission(ProcessId pid, Permission permission,
                               std::function<void(bool)> on_check) {
  if (on_check) on_check(true);
}

bool DoesProcessExist(ProcessId pid) { return false; }
void SetFocusedProcess(ProcessId pid) {}
void TerminateProcesss(ProcessId pid) {}

bool FindFirstInstanceOfService(std::string_view name, ProcessId& process_id,
                                MessageId& message_id) {
  return false;
}

StatusOr<serialization::Value> GetRegistryValue(RegistryCorpus corpus,
                                                std::string_view category,
                                                std::string_view key) {
  TestRegistryKey rk{corpus, std::string(category), std::string(key)};
  auto& store = GetTestRegistryStore();
  auto it = store.find(rk);
  if (it == store.end()) return Status::FILE_NOT_FOUND;
  return it->second;
}

StatusOr<serialization::Value> GetRegistryValue(std::string_view key) {
  return GetRegistryValue(RegistryCorpus::APPLICATIONS, "", key);
}

void SetRegistryValue(std::string_view key, const serialization::Value& value) {
  SetRegistryValue(RegistryCorpus::APPLICATIONS, "", key, value);
}

void SetRegistryValue(RegistryCorpus corpus, std::string_view r_namespace,
                      std::string_view key, const serialization::Value& value) {
  TestRegistryKey rk{corpus, std::string(r_namespace), std::string(key)};
  auto& store = GetTestRegistryStore();
  auto it = store.find(rk);
  bool changed = (it == store.end() || it->second != value);
  store[rk] = value;
  if (changed) NotifyTestRegistryListeners(rk);
}

Status SetRegistryValues(RegistryCorpus corpus, std::string_view r_namespace,
                         std::vector<RegistryKeyValue> values) {
  auto& store = GetTestRegistryStore();
  std::vector<TestRegistryKey> changed;
  for (const auto& kv : values) {
    TestRegistryKey rk{corpus, std::string(r_namespace), kv.key};
    auto it = store.find(rk);
    if (it == store.end() || it->second != kv.value) changed.push_back(rk);
    store[rk] = kv.value;
  }
  for (const auto& rk : changed) NotifyTestRegistryListeners(rk);
  return Status::OK;
}

void DeleteRegistryValue(std::string_view key) {
  DeleteRegistryValue(RegistryCorpus::APPLICATIONS, "", key);
}

void DeleteRegistryValue(RegistryCorpus corpus, std::string_view r_namespace,
                         std::string_view key) {
  TestRegistryKey rk{corpus, std::string(r_namespace), std::string(key)};
  auto& store = GetTestRegistryStore();
  if (store.erase(rk) > 0) NotifyTestRegistryListeners(rk);
}

StatusOr<std::vector<std::string>> GetRegistryKeys() {
  return GetRegistryKeys(RegistryCorpus::APPLICATIONS, "");
}

StatusOr<std::vector<std::string>> GetRegistryKeys(
    RegistryCorpus corpus, std::string_view r_namespace) {
  std::vector<std::string> keys;
  std::string ns(r_namespace);
  for (const auto& [rk, val] : GetTestRegistryStore()) {
    if (rk.corpus == corpus && rk.r_namespace == ns) keys.push_back(rk.key);
  }
  return keys;
}

StatusOr<std::vector<NamespaceInfo>> GetNamespacesInRegistry() {
  std::vector<NamespaceInfo> namespaces;
  return namespaces;
}

StatusOr<RegistryListenerToken> RegisterRegistryListener(
    std::string_view key, std::function<void()> callback) {
  return RegisterRegistryListener(RegistryCorpus::APPLICATIONS, "", key,
                                  std::move(callback));
}

StatusOr<RegistryListenerToken> RegisterRegistryListener(
    RegistryCorpus corpus, std::string_view r_namespace, std::string_view key,
    std::function<void()> callback) {
  static RegistryListenerToken next_token = 1;
  RegistryListenerToken token = next_token++;
  GetTestRegistryListeners().push_back(TestRegistryListener{
      token,
      TestRegistryKey{corpus, std::string(r_namespace), std::string(key)},
      std::move(callback)});
  return token;
}

Status UnregisterRegistryListener(RegistryListenerToken token) {
  auto& listeners = GetTestRegistryListeners();
  for (auto it = listeners.begin(); it != listeners.end(); ++it) {
    if (it->token == token) {
      listeners.erase(it);
      break;
    }
  }
  return Status::OK;
}

Status FlushRegistry() { return Status::OK; }

void RegistryKeyValue::Serialize(serialization::Serializer& serializer) {}

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

MessageId NotifyOnEachNewServiceInstance(
    std::string_view name,
    const std::function<void(ProcessId, MessageId)>& on_each_service) {
  return 0;
}

void StopNotifyingOnEachNewServiceInstance(MessageId message_id) {}

MessageId NotifyWhenServiceDisappears(
    ProcessId process_id, MessageId message_id,
    const std::function<void()>& on_disappearance) {
  return 0;
}

MessageId NotifyWhenServiceDisappears(
    const ServiceClient& service_client,
    const std::function<void()>& on_disappearance) {
  return 0;
}

void StopNotifyWhenServiceDisappears(MessageId message_id) {}

void RegisterService(MessageId message_id, std::string_view name) {}

void UnregisterService(MessageId message_id) {}

size_t RandomNumber() {
  static size_t counter = 123456789;
  return counter++;
}

namespace power {

void PowerOff() {}
void Restart() {}
void Sleep() {}
void Wake() {}

}  // namespace power

}  // namespace perception
