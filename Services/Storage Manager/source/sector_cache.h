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

#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>

class SectorCache {
 public:
  SectorCache(size_t sector_size, size_t max_sectors = 512);

  // Reads from the cache if the sector is present.
  // Returns true if cache hit, false otherwise.
  bool Read(size_t sector, char* dest, size_t offset_in_sector, size_t size);

  // Writes a sector's data into the cache.
  void Write(size_t sector, const char* src);

 private:
  struct ListEntry {
    size_t sector;
    std::vector<char> data;
    bool dirty;
  };

  void MoveToFront(std::list<ListEntry>::iterator it);

  size_t sector_size_;
  size_t max_sectors_;
  std::mutex mutex_;
  std::list<ListEntry> lru_list_;
  std::unordered_map<size_t, std::list<ListEntry>::iterator> cache_;
};
