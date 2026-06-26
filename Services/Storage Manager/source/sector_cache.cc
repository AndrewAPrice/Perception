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

#include "sector_cache.h"

#include <cstring>

SectorCache::SectorCache(size_t sector_size, size_t max_sectors)
    : sector_size_(sector_size), max_sectors_(max_sectors) {}

bool SectorCache::Read(size_t sector, char* dest, size_t offset_in_sector,
                       size_t size) {
  std::scoped_lock lock(mutex_);
  auto it = cache_.find(sector);
  if (it != cache_.end()) {
    // Move to front of LRU list
    MoveToFront(it->second);
    std::memcpy(dest, it->second->data.data() + offset_in_sector, size);
    return true;
  }
  return false;
}

void SectorCache::Write(size_t sector, const char* src) {
  std::scoped_lock lock(mutex_);
  auto it = cache_.find(sector);
  if (it != cache_.end()) {
    std::memcpy(it->second->data.data(), src, sector_size_);
    MoveToFront(it->second);
    return;
  }

  if (lru_list_.size() >= max_sectors_) {
    // Evict oldest
    auto oldest = lru_list_.back();
    cache_.erase(oldest.sector);
    lru_list_.pop_back();
  }

  lru_list_.push_front(
      {sector, std::vector<char>(src, src + sector_size_), false});
  cache_[sector] = lru_list_.begin();
}

void SectorCache::MoveToFront(std::list<ListEntry>::iterator it) {
  lru_list_.splice(lru_list_.begin(), lru_list_, it);
}
