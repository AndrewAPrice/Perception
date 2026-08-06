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

#include "synth_engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "instruments.h"
#include "perception/devices/audio_device.h"
#include "perception/fibers.h"
#include "perception/services.h"
#include "perception/time.h"
#include "reverb.h"
#include "track_manager.h"

namespace {

int sample_rate = 48000;

}  // namespace

double KeyIndexToFrequency(int key_index) {
  return TrackManager::KeyIndexToFrequency(key_index);
}

std::string KeyIndexToNoteName(int key_index) {
  return TrackManager::KeyIndexToNoteName(key_index);
}

std::vector<int16> SynthesizeNoteBuffer(int key_index,
                                        const Instrument* instrument,
                                        double volume,
                                        double duration_seconds) {
  double freq = KeyIndexToFrequency(key_index);
  int total_frames = static_cast<int>(sample_rate * duration_seconds);
  std::vector<int16> buffer(total_frames, 0);

  double scale = volume * 28000.0;
  double dt = 1.0 / sample_rate;
  double sample_block[128];

  for (int chunk_start = 0; chunk_start < total_frames; chunk_start += 128) {
    int chunk_size = std::min(128, total_frames - chunk_start);
    double t_start = static_cast<double>(chunk_start) / sample_rate;
    SynthesizeInstrumentBlock(instrument, freq, t_start, dt, duration_seconds,
                              sample_block, chunk_size);
#if defined(__clang__)
#pragma clang loop vectorize(enable)
#elif defined(__GNUG__)
#pragma GCC ivdep
#endif
    for (int i = 0; i < chunk_size; ++i) {
      double val = sample_block[i] * scale;
      buffer[chunk_start + i] = static_cast<int16>(
          std::clamp(val, -32767.0, 32767.0));
    }
  }

  return buffer;
}

::perception::AudioStreamID PlaySynthesizedNote(int key_index,
                                                const Instrument* instrument,
                                                double volume,
                                                double duration_seconds) {
  if (key_index < 0 || key_index >= 88) return 0;

  auto pcm_samples =
      SynthesizeNoteBuffer(key_index, instrument, volume, duration_seconds);
  size_t buffer_size_bytes = pcm_samples.size() * sizeof(int16);

  auto shared_mem = ::perception::SharedMemory::FromSize(buffer_size_bytes, 0);
  if (!shared_mem || **shared_mem == nullptr) return 0;

  std::memcpy(**shared_mem, pcm_samples.data(), buffer_size_bytes);

  return ::perception::PlayAudio(shared_mem, 1.0f, false, sample_rate, 1, 16);
}

namespace {

struct CacheKey {
  const Instrument* instrument;
  int key_index;
  int volume_bits;

  bool operator==(const CacheKey& other) const {
    return instrument == other.instrument && key_index == other.key_index &&
           volume_bits == other.volume_bits;
  }
};

struct CacheKeyHash {
  std::size_t operator()(const CacheKey& k) const {
    return std::hash<const void*>()(k.instrument) ^
           (std::hash<int>()(k.key_index) << 1) ^
           (std::hash<int>()(k.volume_bits) << 2);
  }
};

// Maximum number of synthesized note audio buffers to store in LRU cache.
constexpr size_t kMaxCachedNoteBuffers = 64;
std::unordered_map<CacheKey, std::vector<int16>, CacheKeyHash> g_note_cache;
std::list<CacheKey> g_cache_lru;
std::mutex g_cache_mutex;

// Maximum number of active voice buffers allowed concurrently in synth engine.
constexpr int kMaxVoices = 32;
// Buffer capacity in seconds for individual voice playback.
constexpr double kVoiceBufferSizeSeconds = 4.0;


struct VoiceBuffer {
  bool active = false;
  int key_index = -1;
  ::perception::AudioStreamID stream_id = 0;
  std::shared_ptr<::perception::SharedMemory> shared_mem;
  std::chrono::time_point<std::chrono::steady_clock> start_time;
  double max_sustain_seconds = 4.0;
  double release_duration_seconds = 0.15;
  double actual_duration_seconds = 4.0;
  bool ignore_note_off = false;
};

VoiceBuffer g_voices[kMaxVoices];
std::mutex g_voices_mutex;

void SynthesizeNoteIntoBuffer(int key_index, const Instrument* instrument,
                              double volume, double duration_seconds,
                              ::perception::SharedMemory& shared_mem) {
  double freq = KeyIndexToFrequency(key_index);
  int total_frames = static_cast<int>(sample_rate * duration_seconds);
  int max_buffer_frames =
      static_cast<int>(shared_mem.GetSize() / sizeof(int16));
  if (total_frames > max_buffer_frames) total_frames = max_buffer_frames;

  int16* buffer = reinterpret_cast<int16*>(*shared_mem);
  if (!buffer) return;

  volume = std::clamp(volume, 0.0, 1.0);
  int vol_quantized = static_cast<int>(volume * 100.0);
  CacheKey key{instrument, key_index, vol_quantized};

  {
    std::scoped_lock lock(g_cache_mutex);
    auto it = g_note_cache.find(key);
    if (it != g_note_cache.end() &&
        it->second.size() >= static_cast<size_t>(total_frames)) {
      std::memcpy(buffer, it->second.data(), total_frames * sizeof(int16));
      for (int i = total_frames; i < max_buffer_frames; ++i) {
        buffer[i] = 0;
      }
      return;
    }
  }

  double scale = volume * 28000.0;
  double dt = 1.0 / sample_rate;
  double sample_block[128];

  for (int chunk_start = 0; chunk_start < total_frames; chunk_start += 128) {
    int chunk_size = std::min(128, total_frames - chunk_start);
    double t_start = static_cast<double>(chunk_start) / sample_rate;
    SynthesizeInstrumentBlock(instrument, freq, t_start, dt, duration_seconds,
                              sample_block, chunk_size);
#if defined(__clang__)
#pragma clang loop vectorize(enable)
#elif defined(__GNUG__)
#pragma GCC ivdep
#endif
    for (int i = 0; i < chunk_size; ++i) {
      double val = sample_block[i] * scale;
      buffer[chunk_start + i] = static_cast<int16>(
          std::clamp(val, -32767.0, 32767.0));
    }
  }

  {
    std::scoped_lock lock(g_cache_mutex);
    if (g_note_cache.size() >= kMaxCachedNoteBuffers && !g_cache_lru.empty()) {
      auto old_key = g_cache_lru.back();
      g_cache_lru.pop_back();
      g_note_cache.erase(old_key);
    }
    g_note_cache[key].assign(buffer, buffer + total_frames);
    g_cache_lru.push_front(key);
  }

  for (int i = total_frames; i < max_buffer_frames; ++i) {
    buffer[i] = 0;
  }

  if (GetGlobalReverb().IsEnabled()) {
    GetGlobalReverb().Process(buffer, total_frames);
  }
}

}  // namespace

void InitializeSynthEngine() {
  auto details_or =
      ::perception::GetService<::perception::devices::AudioDevice>()
          .GetDeviceDetails();
  if (details_or && details_or->sample_rate > 0) {
    sample_rate = static_cast<int>(details_or->sample_rate);
  }
  GetGlobalReverb().SetSampleRate(sample_rate);
}

void StartAudioStream() {
  std::scoped_lock lock(g_voices_mutex);
  for (int i = 0; i < kMaxVoices; ++i) {
    g_voices[i].active = false;
  }
}

void StopAudioStream() {
  std::scoped_lock lock(g_voices_mutex);
  for (int i = 0; i < kMaxVoices; ++i) {
    if (g_voices[i].active && g_voices[i].stream_id != 0) {
      ::perception::StopAudio(g_voices[i].stream_id);
      g_voices[i].active = false;
    }
    g_voices[i].shared_mem.reset();
  }
}

void CleanupFinishedVoices() {
  auto now = std::chrono::steady_clock::now();
  for (int i = 0; i < kMaxVoices; ++i) {
    auto& voice = g_voices[i];
    if (!voice.active) continue;
    double elapsed =
        std::chrono::duration<double>(now - voice.start_time).count();
    if (elapsed >= voice.actual_duration_seconds) {
      if (voice.stream_id != 0) {
        ::perception::StopAudio(voice.stream_id);
        voice.stream_id = 0;
      }
      voice.active = false;
    }
  }
}

void NoteOn(int key_index, const Instrument* instrument, double volume) {
  std::scoped_lock lock(g_voices_mutex);
  if (key_index < 0 || key_index >= 88) return;

  CleanupFinishedVoices();

  double max_sustain = instrument ? instrument->max_sustain_seconds : 4.0;
  double release_dur = instrument ? instrument->release_duration_seconds : 0.15;
  bool ignore_off = instrument ? instrument->ignore_note_off : false;

  int target_voice = -1;
  if (!ignore_off) {
    for (int i = 0; i < kMaxVoices; ++i) {
      if (g_voices[i].active && g_voices[i].key_index == key_index) {
        target_voice = i;
        break;
      }
    }
  }
  if (target_voice == -1) {
    for (int i = 0; i < kMaxVoices; ++i) {
      if (!g_voices[i].active) {
        target_voice = i;
        break;
      }
    }
  }
  if (target_voice == -1) {
    auto oldest_time = std::chrono::steady_clock::time_point::max();
    for (int i = 0; i < kMaxVoices; ++i) {
      if (g_voices[i].start_time < oldest_time) {
        oldest_time = g_voices[i].start_time;
        target_voice = i;
      }
    }
  }

  auto& voice = g_voices[target_voice];
  if (voice.stream_id != 0) {
    ::perception::StopAudio(voice.stream_id);
    voice.stream_id = 0;
  }
  voice.active = false;

  if (!voice.shared_mem) {
    size_t buffer_bytes = static_cast<size_t>(kVoiceBufferSizeSeconds *
                                              sample_rate * sizeof(int16));
    voice.shared_mem = ::perception::SharedMemory::FromSize(buffer_bytes, 0);
  }
  if (!voice.shared_mem || **voice.shared_mem == nullptr) return;

  SynthesizeNoteIntoBuffer(key_index, instrument, volume,
                           std::min(max_sustain, kVoiceBufferSizeSeconds),
                           *voice.shared_mem);

  voice.stream_id = ::perception::PlayAudio(voice.shared_mem, 1.0f, false,
                                            sample_rate, 1, 16);
  voice.active = true;
  voice.key_index = key_index;
  voice.start_time = std::chrono::steady_clock::now();
  voice.max_sustain_seconds = max_sustain;
  voice.release_duration_seconds = release_dur;
  voice.actual_duration_seconds =
      std::min(max_sustain, kVoiceBufferSizeSeconds);
  voice.ignore_note_off = ignore_off;
}

void NoteOff(int key_index) {
  std::scoped_lock lock(g_voices_mutex);
  if (key_index < 0 || key_index >= 88) return;

  for (int i = 0; i < kMaxVoices; ++i) {
    auto& voice = g_voices[i];
    if (!voice.active || voice.key_index != key_index) continue;

    if (voice.ignore_note_off) {
      continue;
    }

    auto now = std::chrono::steady_clock::now();
    double elapsed =
        std::chrono::duration<double>(now - voice.start_time).count();
    if (elapsed >= voice.max_sustain_seconds) {
      if (voice.stream_id != 0) {
        ::perception::StopAudio(voice.stream_id);
        voice.stream_id = 0;
      }
      voice.active = false;
      continue;
    }

    size_t start_fade_frame =
        static_cast<size_t>((elapsed + 0.03) * sample_rate);
    size_t fade_frames =
        static_cast<size_t>(voice.release_duration_seconds * sample_rate);
    size_t total_frames = voice.shared_mem->GetSize() / sizeof(int16);

    if (start_fade_frame < total_frames && voice.shared_mem) {
      int16* pcm = reinterpret_cast<int16*>(**voice.shared_mem);
      for (size_t f = 0; f < fade_frames; ++f) {
        size_t idx = start_fade_frame + f;
        if (idx >= total_frames) break;
        double gain = 1.0 - static_cast<double>(f) / fade_frames;
        pcm[idx] = static_cast<int16>(pcm[idx] * gain);
      }
      for (size_t idx = start_fade_frame + fade_frames; idx < total_frames;
           ++idx) {
        pcm[idx] = 0;
      }
    }
    voice.actual_duration_seconds =
        std::min(voice.actual_duration_seconds,
                 elapsed + voice.release_duration_seconds + 0.05);
  }
}

void PlayNote(int key_index, const Instrument* instrument, double volume,
              double duration_seconds) {
  std::scoped_lock lock(g_voices_mutex);
  if (key_index < 0 || key_index >= 88) return;

  CleanupFinishedVoices();

  double max_sustain = instrument ? instrument->max_sustain_seconds : 4.0;
  double release_dur = instrument ? instrument->release_duration_seconds : 0.15;
  bool ignore_off = instrument ? instrument->ignore_note_off : false;

  int target_voice = -1;
  for (int i = 0; i < kMaxVoices; ++i) {
    if (!g_voices[i].active) {
      target_voice = i;
      break;
    }
  }
  if (target_voice == -1) {
    auto oldest_time = std::chrono::steady_clock::time_point::max();
    for (int i = 0; i < kMaxVoices; ++i) {
      if (g_voices[i].start_time < oldest_time) {
        oldest_time = g_voices[i].start_time;
        target_voice = i;
      }
    }
  }

  auto& voice = g_voices[target_voice];
  if (voice.stream_id != 0) {
    ::perception::StopAudio(voice.stream_id);
    voice.stream_id = 0;
  }
  voice.active = false;

  if (!voice.shared_mem) {
    size_t buffer_bytes = static_cast<size_t>(kVoiceBufferSizeSeconds *
                                              sample_rate * sizeof(int16));
    voice.shared_mem = ::perception::SharedMemory::FromSize(buffer_bytes, 0);
  }
  if (!voice.shared_mem || **voice.shared_mem == nullptr) return;

  double total_render_duration =
      ignore_off
          ? std::min(max_sustain, kVoiceBufferSizeSeconds)
          : std::min(duration_seconds + release_dur, kVoiceBufferSizeSeconds);

  SynthesizeNoteIntoBuffer(key_index, instrument, volume, total_render_duration,
                           *voice.shared_mem);

  if (!ignore_off) {
    size_t start_fade_frame =
        static_cast<size_t>(duration_seconds * sample_rate);
    size_t fade_frames = static_cast<size_t>(release_dur * sample_rate);
    size_t total_frames = voice.shared_mem->GetSize() / sizeof(int16);

    if (start_fade_frame < total_frames) {
      int16* pcm = reinterpret_cast<int16*>(**voice.shared_mem);
      for (size_t f = 0; f < fade_frames; ++f) {
        size_t idx = start_fade_frame + f;
        if (idx >= total_frames) break;
        double gain = 1.0 - static_cast<double>(f) / fade_frames;
        pcm[idx] = static_cast<int16>(pcm[idx] * gain);
      }
      for (size_t idx = start_fade_frame + fade_frames; idx < total_frames;
           ++idx) {
        pcm[idx] = 0;
      }
    }
  }

  voice.stream_id = ::perception::PlayAudio(voice.shared_mem, 1.0f, false,
                                            sample_rate, /*channels=*/1, 16);
  voice.active = true;
  voice.key_index = key_index;
  voice.start_time = std::chrono::steady_clock::now();
  voice.max_sustain_seconds = max_sustain;
  voice.release_duration_seconds = release_dur;
  voice.actual_duration_seconds =
      ignore_off
          ? std::min(max_sustain, kVoiceBufferSizeSeconds)
          : std::min(duration_seconds + release_dur, kVoiceBufferSizeSeconds);
  voice.ignore_note_off = ignore_off;
}
