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

#include "instruments.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace {
std::vector<const Instrument*>& GetInstrumentsMutable() {
  static std::vector<const Instrument*> instruments;
  return instruments;
}
bool g_instruments_sorted = false;
void EnsureSorted() {
  if (!g_instruments_sorted) {
    auto& insts = GetInstrumentsMutable();
    std::stable_sort(insts.begin(), insts.end(),
                     [](const Instrument* a, const Instrument* b) {
                       if (!a || !b) return a < b;
                       if (a->category != b->category) {
                         return a->category < b->category;
                       }
                       return a->sort_order < b->sort_order;
                     });
    g_instruments_sorted = true;
  }
}
}  // namespace

const std::vector<const Instrument*>& GetInstruments() {
  EnsureSorted();
  return GetInstrumentsMutable();
}

void RegisterInstrument(const Instrument* instrument) {
  if (!instrument) return;
  GetInstrumentsMutable().push_back(instrument);
}

const Instrument* GetInstrument(std::string_view name) {
  EnsureSorted();
  for (const Instrument* inst : GetInstrumentsMutable()) {
    if (inst && inst->name == name) {
      return inst;
    }
  }
  return GetDefaultInstrument();
}

namespace {
double DefaultPianoSynthesize(double freq, double t, double duration_seconds) {
  double attack = std::min(1.0, t / 0.005);
  double decay = std::exp(-2.2 * t / duration_seconds);
  return attack * decay * std::sin(2.0 * 3.14159265358979323846 * freq * t);
}

const Instrument kFallbackPiano = {
    "Acoustic Piano", "Keyboards", &DefaultPianoSynthesize, 0.15, false, 4.0, 0};
}  // namespace

const Instrument* GetDefaultInstrument() {
  EnsureSorted();
  const auto& insts = GetInstrumentsMutable();
  if (insts.empty()) {
    GetInstrumentsMutable().push_back(&kFallbackPiano);
  }
  return GetInstrumentsMutable()[0];
}

std::vector<std::string> GetInstrumentNames() {
  std::vector<std::string> names;
  for (const Instrument* inst : GetInstruments()) {
    if (inst) names.push_back(inst->name);
  }
  return names;
}

std::vector<CategorizedInstrumentOption> GetCategorizedInstrumentOptions() {
  EnsureSorted();
  std::vector<CategorizedInstrumentOption> options;
  std::string last_category;

  for (const Instrument* inst : GetInstruments()) {
    if (!inst) continue;
    if (inst->category != last_category) {
      last_category = inst->category;
      std::string category_title = last_category;
      for (char& c : category_title) {
        if (c >= 'a' && c <= 'z') c -= 32;
      }
      options.push_back(CategorizedInstrumentOption{
          .text = category_title, .is_category = true});
    }
    options.push_back(CategorizedInstrumentOption{
        .text = inst->name, .is_category = false});
  }
  return options;
}

const Instrument* GetInstrumentFromOptionIndex(int option_index) {
  EnsureSorted();
  auto options = GetCategorizedInstrumentOptions();
  if (option_index < 0 || option_index >= static_cast<int>(options.size())) {
    return GetDefaultInstrument();
  }
  if (options[option_index].is_category) {
    return GetDefaultInstrument();
  }

  std::string_view name = options[option_index].text;
  for (const Instrument* inst : GetInstruments()) {
    if (inst && inst->name == name) {
      return inst;
    }
  }
  return GetDefaultInstrument();
}

int GetOptionIndexForInstrument(const Instrument* inst) {
  if (!inst) inst = GetDefaultInstrument();
  auto options = GetCategorizedInstrumentOptions();
  for (size_t i = 0; i < options.size(); ++i) {
    if (!options[i].is_category && options[i].text == inst->name) {
      return static_cast<int>(i);
    }
  }
  return 0;
}

