#pragma once

#include <algorithm>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

class SymbolMap {
 public:
  struct Entry {
    std::string_view first;
    size_t second;
  };

  using iterator = const Entry*;
  using const_iterator = const Entry*;

  const_iterator begin() const { return symbols_.data(); }
  const_iterator end() const { return symbols_.data() + symbols_.size(); }

  const_iterator find(std::string_view name) const {
    auto it = std::lower_bound(symbols_.begin(), symbols_.end(), name,
                               [](const Entry& entry, std::string_view val) {
                                 return entry.first < val;
                               });
    if (it != symbols_.end() && it->first == name)
      return &(*it);
    return end();
  }

  Entry* find_mutable(std::string_view name) {
    auto it = std::lower_bound(symbols_.begin(), symbols_.end(), name,
                               [](const Entry& entry, std::string_view val) {
                                 return entry.first < val;
                               });
    if (it != symbols_.end() && it->first == name)
      return &(*it);
    return nullptr;
  }

  bool contains(std::string_view name) const { return find(name) != end(); }

  size_t& operator[](std::string_view name) {
    auto it = std::lower_bound(symbols_.begin(), symbols_.end(), name,
                               [](const Entry& entry, std::string_view val) {
                                 return entry.first < val;
                               });
    if (it != symbols_.end() && it->first == name)
      return it->second;
    auto inserted_it = symbols_.insert(it, Entry{name, 0});
    return inserted_it->second;
  }

  void reserve(size_t size) { symbols_.reserve(size); }

  void insert_bulk(std::vector<Entry>& new_symbols) {
    if (new_symbols.empty()) return;

    std::sort(new_symbols.begin(), new_symbols.end(),
              [](const Entry& a, const Entry& b) { return a.first < b.first; });

    auto new_unique_end = std::unique(
        new_symbols.begin(), new_symbols.end(),
        [](const Entry& a, const Entry& b) { return a.first == b.first; });
    new_symbols.erase(new_unique_end, new_symbols.end());

    if (symbols_.empty()) {
      symbols_ = std::move(new_symbols);
      return;
    }

    size_t old_size = symbols_.size();
    symbols_.insert(symbols_.end(), new_symbols.begin(), new_symbols.end());
    std::inplace_merge(
        symbols_.begin(), symbols_.begin() + old_size, symbols_.end(),
        [](const Entry& a, const Entry& b) { return a.first < b.first; });

    auto unique_end = std::unique(
        symbols_.begin(), symbols_.end(),
        [](const Entry& a, const Entry& b) { return a.first == b.first; });
    symbols_.erase(unique_end, symbols_.end());
  }

 private:
  std::vector<Entry> symbols_;
};
