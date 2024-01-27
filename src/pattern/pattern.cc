#include <spud/arch.h>
#include <spud/pattern.h>

#include <array>
#include <bit>
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

// TODO: clean this up
// private define
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
      const char str[] = {*ch, *(++ch), '\0'};
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

void find_matches_sse(std::string_view mask, std::string_view data,
                      size_t buffer_end, std::span<uint8_t> search_buffer,
                      std::vector<PatternResult> &results);

void find_matches_avx2(std::string_view mask, std::string_view data,
                       size_t buffer_end, std::span<uint8_t> search_buffer,
                       std::vector<PatternResult> &results);

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

  const auto buffer_end = search_buffer.size() - mask.size();

  std::vector<PatternResult> results;

  const bool want_avx2 = ((features & FEATURE_AVX2) != 0);
  const bool want_sse42 = ((features & FEATURE_SSE42) != 0);

  uint32_t abcd[4];
  run_cpuid(7, 0, abcd);
  const auto cpu_has_avx2 = !!(abcd[1] & (1 << 5));
  const auto do_avx2 = cpu_has_avx2 && want_avx2;

  run_cpuid(1, 0, abcd);
  const auto cpu_has_sse42 = !!(abcd[2] & (1 << 19));
  const auto do_sse42 = cpu_has_sse42 && want_sse42;

  if (do_avx2) {
    find_matches_avx2(mask, data, buffer_end, search_buffer, results);
  } else if (do_sse42) {
    find_matches_sse(mask, data, buffer_end, search_buffer, results);
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

PatternMatches find_matches(std::string_view pattern,
                            std::span<uint8_t> search_buffer,
                            uint32_t features) {
  std::string mask;
  std::string data;
  detail::generate_mask_and_data(pattern, mask, data);

  std::vector<detail::PatternResult> results;
  auto matches = detail::find_matches(mask, data, search_buffer, features);
  results.insert(results.end(), matches.begin(), matches.end());
  return results;
}

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
