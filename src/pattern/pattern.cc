#include <spud/arch.h>
#include <spud/pattern.h>

#include <array>
#include <bitset>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <immintrin.h>
#if SPUD_OS_WIN
#include <intrin.h>
#endif

#if SPUD_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winnt.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifndef __AVX2__
#define __AVX2__ 1
#endif
#endif

namespace spud {
namespace detail {
void generate_mask_and_data(std::string_view pattern, std::string &mask,
                            std::string &data) {
  for (auto ch = pattern.begin(); ch != pattern.end(); ++ch) {
    if (*ch == '?') {
      data += '\0';
      mask += '?';
    } else if (*ch != ' ') {
      const char str[] = {*ch, *(++ch)};
      const auto digit = strtol(str, nullptr, 16);
      data += static_cast<char>(digit);
      mask += '1';
    }
  }
}

// TODO: This should be elsewhere
static void run_cpuid(uint32_t eax, uint32_t ecx, uint32_t *abcd) {
#if defined(_MSC_VER)
  __cpuidex(reinterpret_cast<int *>(abcd), eax, ecx);
#else
  uint32_t ebx, edx;
#if defined(__i386__) &&                                                       \
    defined(__PIC__) /* in case of PIC under 32-bit EBX cannot be clobbered */
  __asm__("movl %%ebx, %%edi \n\t cpuid \n\t xchgl %%ebx, %%edi"
          : "=D"(ebx),
#else
  __asm__("cpuid"
          : "+b"(ebx),
#endif
            "+a"(eax), "+c"(ecx), "=d"(edx));
  abcd[0] = eax;
  abcd[1] = ebx;
  abcd[2] = ecx;
  abcd[3] = edx;
#endif
}

std::vector<PatternResult> find_matches(std::string_view mask,
                                        std::string_view data,
                                        std::span<uint8_t> search_buffer,
                                        uint32_t features) {
  const auto does_match = [&](uintptr_t offset) {
    for (size_t i = 0; i < mask.size(); ++i) {
      if (mask[i] == '?')
        continue;

      // Make sure we don't walk past the end of the search buffer
      if (search_buffer.size() < offset + i)
        return false;

      if (search_buffer[offset + i] != static_cast<uint8_t>(data[i]))
        return false;
    }
    return true;
  };

  const auto ctz = [](uint32_t v) {
    unsigned long index = 0;
#if SPUD_OS_WIN
    if (_BitScanForward(&index, v)) {
      return index;
    }
    return 32ul;
#else
    if (v == 0)
      return 32ul;
    return static_cast<unsigned long>(__builtin_clz(v));
#endif
  };
  const auto buffer_end = search_buffer.size() - mask.size();

  std::vector<PatternResult> results;

  uint32_t abcd[4];
  run_cpuid(7, 0, abcd);
  const auto cpu_has_avx2 = !!(abcd[1] & (1 << 5));
  const auto do_avx2 = cpu_has_avx2 && ((features & FEATURE_AVX2) == 1);

  run_cpuid(1, 0, abcd);
  const auto cpu_has_sse42 = !!(abcd[2] & (1 << 19));
  const auto do_sse42 = cpu_has_sse42 && ((features & FEATURE_SSE42) == 1);

#if __AVX2__
  if (do_avx2) {
    __m256i first = _mm256_set1_epi8(0);
    __m256i second = _mm256_set1_epi8(0);

    const auto pattern_size = std::min(mask.size(), size_t(32));

    size_t spread = 0;
    size_t first_i = 0;

    for (size_t i = 0; i < pattern_size; ++i) {
      if (mask[i] != '?') {
        first = _mm256_set1_epi8(data[i]);
        first_i = i;
        break;
      }
    }

    for (size_t i = pattern_size - 1; i >= 0; --i) {
      if (mask[i] != '?') {
        second = _mm256_set1_epi8(data[i]);
        spread = i - first_i;
        break;
      }
    }

    const auto end = buffer_end - spread - ((buffer_end - spread) % 32);

    for (size_t offset = 0; offset < end; offset += 32) {
      const auto first_block = _mm256_loadu_si256(
          reinterpret_cast<const __m256i *>(search_buffer.data() + offset));
      const auto second_block =
          _mm256_loadu_si256(reinterpret_cast<const __m256i *>(
              search_buffer.data() + offset + spread));

      const __m256i eq_first = _mm256_cmpeq_epi8(first, first_block);
      const __m256i eq_second = _mm256_cmpeq_epi8(second, second_block);

      uint32_t mm_mask =
          _mm256_movemask_epi8(_mm256_and_si256(eq_first, eq_second));

      while (mm_mask != 0) {
        const auto bit_pos = ctz(mm_mask);
        if (does_match(offset + bit_pos)) {
          results.emplace_back(search_buffer, offset + bit_pos);
        }
        mm_mask = mm_mask & (mm_mask - 1);
      }
    }

    // Scan the remainder
    for (size_t offset = end; offset < buffer_end; ++offset) {
      if (does_match(offset)) {
        results.emplace_back(PatternResult{search_buffer, offset});
      }
    }
  } else
#endif
      if (do_sse42) {
    __m128i first = _mm_set1_epi8(0);
    __m128i second = _mm_set1_epi8(0);

    const auto pattern_size = std::min(mask.size(), size_t(16));

    size_t spread = 0;
    size_t first_i = 0;

    for (size_t i = 0; i < pattern_size; ++i) {
      if (mask[i] != '?') {
        first = _mm_set1_epi8(data[i]);
        first_i = i;
        break;
      }
    }

    for (size_t i = pattern_size - 1; i >= 0; --i) {
      if (mask[i] != '?') {
        second = _mm_set1_epi8(data[i]);
        spread = i - first_i;
        break;
      }
    }

    const auto end = buffer_end - spread - ((buffer_end - spread) % 16);
    for (size_t offset = 0; offset < buffer_end; offset += 16) {
      const auto first_block = _mm_loadu_si128(
          reinterpret_cast<const __m128i *>(search_buffer.data() + offset));
      const auto second_block =
          _mm_loadu_si128(reinterpret_cast<const __m128i *>(
              search_buffer.data() + offset + spread));

      const __m128i eq_first = _mm_cmpeq_epi8(first, first_block);
      const __m128i eq_second = _mm_cmpeq_epi8(second, second_block);

      uint32_t mm_mask = _mm_movemask_epi8(_mm_and_si128(eq_first, eq_second));

      while (mm_mask != 0) {
        const auto bit_pos = ctz(mm_mask);
        if (does_match(offset + bit_pos)) {
          results.emplace_back(
              PatternResult{search_buffer, uintptr_t(offset + bit_pos)});
        }
        mm_mask = mm_mask & (mm_mask - 1);
      }
    }

    // Scan the remainder
    for (size_t offset = end; offset < buffer_end; ++offset) {
      if (does_match(offset)) {
        results.emplace_back(PatternResult{search_buffer, offset});
      }
    }
  } else {
    for (size_t offset = 0; offset < buffer_end; ++offset) {
      if (does_match(offset)) {
        results.emplace_back(PatternResult{search_buffer, offset});
      }
    }
  }

  return results;
}
} // namespace detail

#if SPUD_OS_WIN
PatternMatches find_in_module(std::string_view pattern, std::string_view module,
                              uint32_t features) {
  std::vector<std::span<uint8_t>> sections;

  const auto module_handle = reinterpret_cast<uintptr_t>(
      GetModuleHandleA(module.size() == 0 ? nullptr : module.data()));
  const auto dos_header = reinterpret_cast<PIMAGE_DOS_HEADER>(module_handle);
  if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
    return {};
  }

  const auto nt_headers =
      reinterpret_cast<PIMAGE_NT_HEADERS>(module_handle + dos_header->e_lfanew);
  if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
    return {};
  }

  sections.reserve(nt_headers->FileHeader.NumberOfSections);

  auto nt_section = IMAGE_FIRST_SECTION(nt_headers);
  for (size_t i = 0; i < nt_headers->FileHeader.NumberOfSections;
       i++, nt_section++) {
    if ((nt_section->Characteristics & IMAGE_SCN_MEM_READ) != 0) {
      auto start = reinterpret_cast<uint8_t *>(module_handle +
                                               nt_section->VirtualAddress);
      auto end = start + nt_section->Misc.VirtualSize;
      auto data = std::span(start, end);
      sections.emplace_back(data);
    }
  }

  std::string mask;
  std::string data;
  detail::generate_mask_and_data(pattern, mask, data);

  std::vector<detail::PatternResult> results;
  for (auto &section : sections) {
    auto matches = detail::find_matches(mask, data, section, features);
    results.insert(results.end(), matches.begin(), matches.end());
  }
  return results;
}
#endif

} // namespace spud
