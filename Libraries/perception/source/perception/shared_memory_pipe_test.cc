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

#include "perception/shared_memory_pipe.h"

#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "perception/shared_memory_futex.h"
#include "testing.h"

namespace {

// Expected return value for non-blocking operations that would block (-EAGAIN).
constexpr long kExpectedAgain = -11;

// Expected return value when writing to a pipe with no readers (-EPIPE).
constexpr long kExpectedBrokenPipe = -32;

// Number of iterations per thread in the contended futex test.
constexpr int kFutexContentionIterations = 200;

// Chunk size used to exercise circular buffer wrap-around.
constexpr size_t kWrapChunkSize = 5000;

// Number of wrap-around cycles to run.
constexpr int kWrapIterations = 10;

// Test process ID for TerminalService client metadata.
constexpr ::perception::ProcessId kTestTerminalPid = 42;

// Test service ID for TerminalService client metadata.
constexpr ::perception::MessageId kTestTerminalServiceId = 99;

}  // namespace

using ::perception::SharedMemory;
using ::perception::SharedMemoryFutex;
using ::perception::SharedMemoryPipe;
using ::perception::TerminalService;

TEST(SharedMemoryFutexLockAndUnlock) {
  auto shm = SharedMemory::FromSize(4096, SharedMemory::kJoinersCanWrite);
  ASSERT(true, shm != nullptr);
  ASSERT(true, **shm != nullptr);

  auto* state = static_cast<std::atomic<uint32>*>(**shm);
  state->store(0, std::memory_order_relaxed);

  SharedMemoryFutex::Lock(*shm, 0, *state);
  EXPECT(static_cast<uint32>(1), state->load(std::memory_order_relaxed));
  SharedMemoryFutex::Unlock(*shm, 0, *state);
  EXPECT(static_cast<uint32>(0), state->load(std::memory_order_relaxed));

  int counter = 0;
  auto worker = [&]() {
    for (int i = 0; i < kFutexContentionIterations; ++i) {
      SharedMemoryFutex::Lock(*shm, 0, *state);
      ++counter;
      SharedMemoryFutex::Unlock(*shm, 0, *state);
    }
  };

  std::thread t1(worker);
  std::thread t2(worker);
  t1.join();
  t2.join();

  EXPECT(kFutexContentionIterations * 2, counter);
  EXPECT(static_cast<uint32>(0), state->load(std::memory_order_relaxed));
}

TEST(SharedMemoryPipeBasicReadWrite) {
  auto pipe = SharedMemoryPipe::Create();
  ASSERT(true, pipe != nullptr);
  EXPECT(true, pipe->IsValid());
  EXPECT(true, pipe->GetId() > 0);
  EXPECT(static_cast<size_t>(0), pipe->BufferedBytes());
  EXPECT(SharedMemoryPipe::kBufferCapacity, pipe->FreeSpace());

  pipe->AddReader();
  pipe->AddWriter();
  EXPECT(false, pipe->IsEof());
  EXPECT(false, pipe->IsBrokenPipe());

  auto joined = SharedMemoryPipe::FromSharedMemoryId(pipe->GetId());
  ASSERT(true, joined != nullptr);
  EXPECT(true, joined->IsValid());

  const std::string payload = "Hello Perception Terminal!";
  long written = pipe->Write(payload.data(), payload.size());
  EXPECT(static_cast<long>(payload.size()), written);
  EXPECT(payload.size(), joined->BufferedBytes());

  char read_buf[64] = {};
  long read_bytes = joined->Read(read_buf, sizeof(read_buf));
  EXPECT(static_cast<long>(payload.size()), read_bytes);
  EXPECT(payload, std::string(read_buf, static_cast<size_t>(read_bytes)));
  EXPECT(static_cast<size_t>(0), pipe->BufferedBytes());
}

TEST(SharedMemoryPipeCircularWrapAround) {
  auto pipe = SharedMemoryPipe::Create();
  ASSERT(true, pipe != nullptr);
  pipe->AddReader();
  pipe->AddWriter();

  std::vector<char> write_chunk(kWrapChunkSize);
  std::vector<char> read_chunk(kWrapChunkSize);

  for (int iter = 0; iter < kWrapIterations; ++iter) {
    for (size_t i = 0; i < kWrapChunkSize; ++i)
      write_chunk[i] = static_cast<char>((iter * 31 + i) & 0xFF);

    long written = pipe->Write(write_chunk.data(), write_chunk.size());
    ASSERT(static_cast<long>(kWrapChunkSize), written);
    EXPECT(kWrapChunkSize, pipe->BufferedBytes());

    std::memset(read_chunk.data(), 0, read_chunk.size());
    long read_bytes = pipe->Read(read_chunk.data(), read_chunk.size());
    ASSERT(static_cast<long>(kWrapChunkSize), read_bytes);
    EXPECT(static_cast<size_t>(0), pipe->BufferedBytes());
    EXPECT(0, std::memcmp(write_chunk.data(), read_chunk.data(),
                          kWrapChunkSize));
  }
}

TEST(SharedMemoryPipeNonBlockingAndFull) {
  auto pipe = SharedMemoryPipe::Create();
  ASSERT(true, pipe != nullptr);
  pipe->AddReader();
  pipe->AddWriter();

  char buf[16] = {};
  EXPECT(kExpectedAgain, pipe->Read(buf, sizeof(buf), /*non_blocking=*/true));

  std::vector<char> full_data(SharedMemoryPipe::kBufferCapacity, 'A');
  long written =
      pipe->Write(full_data.data(), full_data.size(), /*non_blocking=*/true);
  EXPECT(static_cast<long>(SharedMemoryPipe::kBufferCapacity), written);
  EXPECT(static_cast<size_t>(0), pipe->FreeSpace());

  EXPECT(kExpectedAgain,
         pipe->Write("B", 1, /*non_blocking=*/true));

  long drained = pipe->Read(buf, 10, /*non_blocking=*/true);
  EXPECT(10L, drained);
  EXPECT(static_cast<size_t>(10), pipe->FreeSpace());

  long partial_write =
      pipe->Write("0123456789ABCDEF", 16, /*non_blocking=*/true);
  EXPECT(10L, partial_write);
  EXPECT(static_cast<size_t>(0), pipe->FreeSpace());
}

TEST(SharedMemoryPipeEofAndBrokenPipe) {
  auto pipe = SharedMemoryPipe::Create();
  ASSERT(true, pipe != nullptr);
  pipe->AddReader();
  pipe->AddWriter();

  EXPECT(5L, pipe->Write("hello", 5));
  pipe->CloseWriter();
  EXPECT(true, pipe->IsEof());

  char buf[16] = {};
  EXPECT(5L, pipe->Read(buf, sizeof(buf)));
  EXPECT(std::string("hello"), std::string(buf, 5));
  EXPECT(0L, pipe->Read(buf, sizeof(buf)));

  pipe->AddWriter();
  EXPECT(false, pipe->IsEof());
  pipe->CloseReader();
  EXPECT(true, pipe->IsBrokenPipe());
  EXPECT(kExpectedBrokenPipe, pipe->Write("world", 5));
}

TEST(SharedMemoryPipeTerminalServiceAndCallback) {
  auto pipe = SharedMemoryPipe::Create();
  ASSERT(true, pipe != nullptr);
  pipe->AddReader();
  pipe->AddWriter();

  EXPECT(false, pipe->HasTerminalService());
  EXPECT(false, pipe->GetTerminalService().has_value());

  TerminalService::Client client(kTestTerminalPid, kTestTerminalServiceId);
  pipe->SetTerminalService(client);
  EXPECT(true, pipe->HasTerminalService());

  auto retrieved = pipe->GetTerminalService();
  ASSERT(true, retrieved.has_value());
  EXPECT(kTestTerminalPid, retrieved->ServerProcessId());
  EXPECT(kTestTerminalServiceId, retrieved->ServiceId());

  EXPECT(4L, pipe->Write("ping", 4));
  int callback_invocations = 0;
  pipe->OnDataAvailable([&callback_invocations]() { ++callback_invocations; });
  EXPECT(1, callback_invocations);
  pipe->OnDataAvailable(nullptr);
}
