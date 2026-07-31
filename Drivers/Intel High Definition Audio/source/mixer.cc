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

#include "mixer.h"

#include <immintrin.h>

#include <algorithm>
#include <cstring>
#include <iostream>

MixStereoFunc mix_stereo_func = nullptr;
MixMonoFunc mix_mono_func = nullptr;

namespace {

inline void GetCpuId(uint32_t leaf, uint32_t subleaf, uint32_t* eax,
                     uint32_t* ebx, uint32_t* ecx, uint32_t* edx) {
  asm volatile("cpuid"
               : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
               : "a"(leaf), "c"(subleaf));
}

inline uint64_t GetXcr0() {
  uint32_t eax, edx;
  asm volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
  return ((uint64_t)edx << 32) | eax;
}

// Scalar Stereo-to-Stereo Mixer
void MixStereoToStereo_Scalar(int16_t* dest, const int16_t* src,
                              size_t num_frames, float gain) {
  size_t num_samples = num_frames * 2;
  for (size_t i = 0; i < num_samples; ++i) {
    int32_t sample = static_cast<int32_t>(src[i] * gain);
    int32_t cur = dest[i];
    dest[i] = static_cast<int16_t>(std::clamp(cur + sample, -32768, 32767));
  }
}

// Scalar Mono-to-Stereo Mixer
void MixMonoToStereo_Scalar(int16_t* dest, const int16_t* src,
                            size_t num_frames, float gain) {
  for (size_t f = 0; f < num_frames; ++f) {
    int32_t sample = static_cast<int32_t>(src[f] * gain);
    int32_t cur_l = dest[f * 2 + 0];
    int32_t cur_r = dest[f * 2 + 1];
    dest[f * 2 + 0] =
        static_cast<int16_t>(std::clamp(cur_l + sample, -32768, 32767));
    dest[f * 2 + 1] =
        static_cast<int16_t>(std::clamp(cur_r + sample, -32768, 32767));
  }
}

// SSE2 Stereo-to-Stereo Mixer (processes 8 samples = 4 frames per iteration)
__attribute__((target("sse2"))) void MixStereoToStereo_SSE2(int16_t* dest,
                                                            const int16_t* src,
                                                            size_t num_frames,
                                                            float gain) {
  size_t num_samples = num_frames * 2;
  size_t i = 0;
  __m128 vgain = _mm_set1_ps(gain);
  for (; i + 8 <= num_samples; i += 8) {
    __m128i src16 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&src[i]));
    __m128i dest16 =
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(&dest[i]));

    __m128i src32_lo = _mm_srai_epi32(_mm_unpacklo_epi16(src16, src16), 16);
    __m128i src32_hi = _mm_srai_epi32(_mm_unpackhi_epi16(src16, src16), 16);

    __m128 src_flo = _mm_cvtepi32_ps(src32_lo);
    __m128 src_fhi = _mm_cvtepi32_ps(src32_hi);

    src_flo = _mm_mul_ps(src_flo, vgain);
    src_fhi = _mm_mul_ps(src_fhi, vgain);

    __m128i scaled_lo = _mm_cvtps_epi32(src_flo);
    __m128i scaled_hi = _mm_cvtps_epi32(src_fhi);

    __m128i dest32_lo = _mm_srai_epi32(_mm_unpacklo_epi16(dest16, dest16), 16);
    __m128i dest32_hi = _mm_srai_epi32(_mm_unpackhi_epi16(dest16, dest16), 16);

    __m128i mixed_lo = _mm_add_epi32(dest32_lo, scaled_lo);
    __m128i mixed_hi = _mm_add_epi32(dest32_hi, scaled_hi);

    __m128i result16 = _mm_packs_epi32(mixed_lo, mixed_hi);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(&dest[i]), result16);
  }
  for (; i < num_samples; ++i) {
    int32_t sample = static_cast<int32_t>(src[i] * gain);
    int32_t cur = dest[i];
    dest[i] = static_cast<int16_t>(std::clamp(cur + sample, -32768, 32767));
  }
}

// SSE2 Mono-to-Stereo Mixer (processes 4 mono frames -> 8 stereo samples per
// iteration)
__attribute__((target("sse2"))) void MixMonoToStereo_SSE2(int16_t* dest,
                                                          const int16_t* src,
                                                          size_t num_frames,
                                                          float gain) {
  size_t f = 0;
  __m128 vgain = _mm_set1_ps(gain);
  for (; f + 4 <= num_frames; f += 4) {
    __m128i mono64 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(&src[f]));
    __m128i stereo16 = _mm_unpacklo_epi16(mono64, mono64);

    __m128i src32_lo =
        _mm_srai_epi32(_mm_unpacklo_epi16(stereo16, stereo16), 16);
    __m128i src32_hi =
        _mm_srai_epi32(_mm_unpackhi_epi16(stereo16, stereo16), 16);

    __m128 src_flo = _mm_cvtepi32_ps(src32_lo);
    __m128 src_fhi = _mm_cvtepi32_ps(src32_hi);

    src_flo = _mm_mul_ps(src_flo, vgain);
    src_fhi = _mm_mul_ps(src_fhi, vgain);

    __m128i scaled_lo = _mm_cvtps_epi32(src_flo);
    __m128i scaled_hi = _mm_cvtps_epi32(src_fhi);

    __m128i dest16 =
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(&dest[f * 2]));
    __m128i dest32_lo = _mm_srai_epi32(_mm_unpacklo_epi16(dest16, dest16), 16);
    __m128i dest32_hi = _mm_srai_epi32(_mm_unpackhi_epi16(dest16, dest16), 16);

    __m128i mixed_lo = _mm_add_epi32(dest32_lo, scaled_lo);
    __m128i mixed_hi = _mm_add_epi32(dest32_hi, scaled_hi);

    __m128i result16 = _mm_packs_epi32(mixed_lo, mixed_hi);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(&dest[f * 2]), result16);
  }
  for (; f < num_frames; ++f) {
    int32_t sample = static_cast<int32_t>(src[f] * gain);
    int32_t cur_l = dest[f * 2 + 0];
    int32_t cur_r = dest[f * 2 + 1];
    dest[f * 2 + 0] =
        static_cast<int16_t>(std::clamp(cur_l + sample, -32768, 32767));
    dest[f * 2 + 1] =
        static_cast<int16_t>(std::clamp(cur_r + sample, -32768, 32767));
  }
}

// SSE4.1 Stereo-to-Stereo Mixer
__attribute__((target("sse4.1"))) void MixStereoToStereo_SSE41(
    int16_t* dest, const int16_t* src, size_t num_frames, float gain) {
  size_t num_samples = num_frames * 2;
  size_t i = 0;
  __m128 vgain = _mm_set1_ps(gain);
  for (; i + 8 <= num_samples; i += 8) {
    __m128i src16 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&src[i]));
    __m128i dest16 =
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(&dest[i]));

    __m128i src32_lo = _mm_cvtepi16_epi32(src16);
    __m128i src32_hi = _mm_cvtepi16_epi32(_mm_srli_si128(src16, 8));

    __m128 src_flo = _mm_cvtepi32_ps(src32_lo);
    __m128 src_fhi = _mm_cvtepi32_ps(src32_hi);

    src_flo = _mm_mul_ps(src_flo, vgain);
    src_fhi = _mm_mul_ps(src_fhi, vgain);

    __m128i scaled_lo = _mm_cvtps_epi32(src_flo);
    __m128i scaled_hi = _mm_cvtps_epi32(src_fhi);

    __m128i dest32_lo = _mm_cvtepi16_epi32(dest16);
    __m128i dest32_hi = _mm_cvtepi16_epi32(_mm_srli_si128(dest16, 8));

    __m128i mixed_lo = _mm_add_epi32(dest32_lo, scaled_lo);
    __m128i mixed_hi = _mm_add_epi32(dest32_hi, scaled_hi);

    __m128i result16 = _mm_packs_epi32(mixed_lo, mixed_hi);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(&dest[i]), result16);
  }
  for (; i < num_samples; ++i) {
    int32_t sample = static_cast<int32_t>(src[i] * gain);
    int32_t cur = dest[i];
    dest[i] = static_cast<int16_t>(std::clamp(cur + sample, -32768, 32767));
  }
}

// SSE4.1 Mono-to-Stereo Mixer
__attribute__((target("sse4.1"))) void MixMonoToStereo_SSE41(int16_t* dest,
                                                             const int16_t* src,
                                                             size_t num_frames,
                                                             float gain) {
  size_t f = 0;
  __m128 vgain = _mm_set1_ps(gain);
  for (; f + 4 <= num_frames; f += 4) {
    __m128i mono64 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(&src[f]));
    __m128i stereo16 = _mm_unpacklo_epi16(mono64, mono64);

    __m128i src32_lo = _mm_cvtepi16_epi32(stereo16);
    __m128i src32_hi = _mm_cvtepi16_epi32(_mm_srli_si128(stereo16, 8));

    __m128 src_flo = _mm_cvtepi32_ps(src32_lo);
    __m128 src_fhi = _mm_cvtepi32_ps(src32_hi);

    src_flo = _mm_mul_ps(src_flo, vgain);
    src_fhi = _mm_mul_ps(src_fhi, vgain);

    __m128i scaled_lo = _mm_cvtps_epi32(src_flo);
    __m128i scaled_hi = _mm_cvtps_epi32(src_fhi);

    __m128i dest16 =
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(&dest[f * 2]));
    __m128i dest32_lo = _mm_cvtepi16_epi32(dest16);
    __m128i dest32_hi = _mm_cvtepi16_epi32(_mm_srli_si128(dest16, 8));

    __m128i mixed_lo = _mm_add_epi32(dest32_lo, scaled_lo);
    __m128i mixed_hi = _mm_add_epi32(dest32_hi, scaled_hi);

    __m128i result16 = _mm_packs_epi32(mixed_lo, mixed_hi);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(&dest[f * 2]), result16);
  }
  for (; f < num_frames; ++f) {
    int32_t sample = static_cast<int32_t>(src[f] * gain);
    int32_t cur_l = dest[f * 2 + 0];
    int32_t cur_r = dest[f * 2 + 1];
    dest[f * 2 + 0] =
        static_cast<int16_t>(std::clamp(cur_l + sample, -32768, 32767));
    dest[f * 2 + 1] =
        static_cast<int16_t>(std::clamp(cur_r + sample, -32768, 32767));
  }
}

// AVX2 Stereo-to-Stereo Mixer (processes 16 samples = 8 frames per iteration)
__attribute__((target("avx2,fma"))) void MixStereoToStereo_AVX2(
    int16_t* dest, const int16_t* src, size_t num_frames, float gain) {
  size_t num_samples = num_frames * 2;
  size_t i = 0;
  __m256 vgain = _mm256_set1_ps(gain);
  for (; i + 16 <= num_samples; i += 16) {
    __m256i src16 =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&src[i]));
    __m256i dest16 =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&dest[i]));

    __m128i src16_lo = _mm256_castsi256_si128(src16);
    __m128i src16_hi = _mm256_extracti128_si256(src16, 1);

    __m256i src32_lo = _mm256_cvtepi16_epi32(src16_lo);
    __m256i src32_hi = _mm256_cvtepi16_epi32(src16_hi);

    __m256 src_flo = _mm256_cvtepi32_ps(src32_lo);
    __m256 src_fhi = _mm256_cvtepi32_ps(src32_hi);

    src_flo = _mm256_mul_ps(src_flo, vgain);
    src_fhi = _mm256_mul_ps(src_fhi, vgain);

    __m256i scaled_lo = _mm256_cvtps_epi32(src_flo);
    __m256i scaled_hi = _mm256_cvtps_epi32(src_fhi);

    __m128i dest16_lo = _mm256_castsi256_si128(dest16);
    __m128i dest16_hi = _mm256_extracti128_si256(dest16, 1);

    __m256i dest32_lo = _mm256_cvtepi16_epi32(dest16_lo);
    __m256i dest32_hi = _mm256_cvtepi16_epi32(dest16_hi);

    __m256i mixed_lo = _mm256_add_epi32(dest32_lo, scaled_lo);
    __m256i mixed_hi = _mm256_add_epi32(dest32_hi, scaled_hi);

    __m256i packed = _mm256_packs_epi32(mixed_lo, mixed_hi);
    __m256i result16 =
        _mm256_permute4x64_epi64(packed, _MM_SHUFFLE(3, 1, 2, 0));

    _mm256_storeu_si256(reinterpret_cast<__m256i*>(&dest[i]), result16);
  }
  for (; i < num_samples; ++i) {
    int32_t sample = static_cast<int32_t>(src[i] * gain);
    int32_t cur = dest[i];
    dest[i] = static_cast<int16_t>(std::clamp(cur + sample, -32768, 32767));
  }
}

// AVX2 Mono-to-Stereo Mixer (processes 8 mono frames -> 16 stereo samples per
// iteration)
__attribute__((target("avx2,fma"))) void MixMonoToStereo_AVX2(
    int16_t* dest, const int16_t* src, size_t num_frames, float gain) {
  size_t f = 0;
  __m256 vgain = _mm256_set1_ps(gain);
  for (; f + 8 <= num_frames; f += 8) {
    __m128i mono128 =
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(&src[f]));
    __m128i stereo_lo = _mm_unpacklo_epi16(mono128, mono128);
    __m128i stereo_hi = _mm_unpackhi_epi16(mono128, mono128);

    __m256i src32_lo = _mm256_cvtepi16_epi32(stereo_lo);
    __m256i src32_hi = _mm256_cvtepi16_epi32(stereo_hi);

    __m256 src_flo = _mm256_cvtepi32_ps(src32_lo);
    __m256 src_fhi = _mm256_cvtepi32_ps(src32_hi);

    src_flo = _mm256_mul_ps(src_flo, vgain);
    src_fhi = _mm256_mul_ps(src_fhi, vgain);

    __m256i scaled_lo = _mm256_cvtps_epi32(src_flo);
    __m256i scaled_hi = _mm256_cvtps_epi32(src_fhi);

    __m256i dest16 =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&dest[f * 2]));
    __m128i dest16_lo = _mm256_castsi256_si128(dest16);
    __m128i dest16_hi = _mm256_extracti128_si256(dest16, 1);

    __m256i dest32_lo = _mm256_cvtepi16_epi32(dest16_lo);
    __m256i dest32_hi = _mm256_cvtepi16_epi32(dest16_hi);

    __m256i mixed_lo = _mm256_add_epi32(dest32_lo, scaled_lo);
    __m256i mixed_hi = _mm256_add_epi32(dest32_hi, scaled_hi);

    __m256i packed = _mm256_packs_epi32(mixed_lo, mixed_hi);
    __m256i result16 =
        _mm256_permute4x64_epi64(packed, _MM_SHUFFLE(3, 1, 2, 0));

    _mm256_storeu_si256(reinterpret_cast<__m256i*>(&dest[f * 2]), result16);
  }
  for (; f < num_frames; ++f) {
    int32_t sample = static_cast<int32_t>(src[f] * gain);
    int32_t cur_l = dest[f * 2 + 0];
    int32_t cur_r = dest[f * 2 + 1];
    dest[f * 2 + 0] =
        static_cast<int16_t>(std::clamp(cur_l + sample, -32768, 32767));
    dest[f * 2 + 1] =
        static_cast<int16_t>(std::clamp(cur_r + sample, -32768, 32767));
  }
}

}  // namespace

void InitializeMixerFunctions() {
  uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
  GetCpuId(1, 0, &eax, &ebx, &ecx, &edx);

  bool sse2_supported = (edx & (1U << 26)) != 0;
  bool sse41_supported = (ecx & (1U << 19)) != 0;
  bool avx_supported = (ecx & (1U << 28)) != 0;
  bool osxsave_supported = (ecx & (1U << 27)) != 0;

  bool avx2_supported = false;
  if (avx_supported) {
    GetCpuId(7, 0, &eax, &ebx, &ecx, &edx);
    avx2_supported = (ebx & (1U << 5)) != 0;
  }

  bool ymm_enabled = false;
  if (osxsave_supported) {
    uint64_t xcr0 = GetXcr0();
    ymm_enabled = (xcr0 & 6) == 6;  // XMM bit 1 and YMM bit 2
  }

  if (avx2_supported && ymm_enabled) {
    mix_stereo_func = MixStereoToStereo_AVX2;
    mix_mono_func = MixMonoToStereo_AVX2;
  } else if (sse41_supported) {
    mix_stereo_func = MixStereoToStereo_SSE41;
    mix_mono_func = MixMonoToStereo_SSE41;
  } else if (sse2_supported) {
    mix_stereo_func = MixStereoToStereo_SSE2;
    mix_mono_func = MixMonoToStereo_SSE2;
  } else {
    mix_stereo_func = MixStereoToStereo_Scalar;
    mix_mono_func = MixMonoToStereo_Scalar;
  }
}
