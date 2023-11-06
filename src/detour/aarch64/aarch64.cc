#include "detour/detour_impl.h"

#include <spud/detour.h>
#include <spud/memory/protection.h>
#include <spud/utils.h>

#include <asmjit/a64.h>
#include <asmjit/asmjit.h>

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace spud {
namespace detail {
namespace arm64 {

constexpr auto kAbsoluteJumpSize = 16;

std::vector<uint8_t> create_absolute_jump(uintptr_t target_address,
                                          uintptr_t data) {
  using namespace asmjit;
  using namespace asmjit::a64;

  CodeHolder code;
  // TODO(tashcan): Do we have to fill the other values here somehow?
  code.init(Environment{asmjit::Arch::kAArch64});
  Assembler assembler(&code);

  Label L1 = assembler.newLabel();
  assembler.ldr(x9, ptr(L1));
  assembler.br(x9);
  assembler.bind(L1);
  assembler.embed(&target_address, sizeof(target_address));

  auto &buffer = code.textSection()->buffer();
  return {buffer.begin(), buffer.end()};
}

std::tuple<RelocationInfo, size_t> collect_relocations(uintptr_t address,
                                                       size_t jump_size) {

  RelocationInfo relocation_info;

  // Fill

  return {relocation_info, jump_size};
}
size_t get_trampoline_size(std::span<uint8_t> target,
                           const RelocationInfo &relocation_info) {
  //
  size_t required_space = target.size();

  auto target_start = reinterpret_cast<uintptr_t>(target.data());
  // for (const auto &relot : relocation_info.relocations) {
  //   auto &relo = std::get<RelocationEntry>(relot);

  //   auto &r_meta = relo_meta.at(relo.instruction);
  //   required_space += r_meta.size;
  // }
  required_space += kAbsoluteJumpSize;
  return required_space;
}

Trampoline create_trampoline(uintptr_t return_address,
                             std::span<uint8_t> target,
                             const RelocationInfo &relocation_infos) {
  //
  using namespace asmjit;
  using namespace asmjit::a64;

  CodeHolder code;
  code.init(Environment{asmjit::Arch::kAArch64}, 0x0);
  Assembler assembler(&code);

  const auto target_start = reinterpret_cast<uintptr_t>(target.data());
  const auto trampoline_start = code.textSection()->buffer().size();

  const auto is_far_relocate = true;
  assembler.embed(target.data(), target.size());
  Label L1 = assembler.newLabel();
  assembler.ldr(x9, ptr(L1));
  assembler.br(x9);
  assembler.bind(L1);
  assembler.embed(&return_address, sizeof(return_address));

  auto &buffer = code.textSection()->buffer();
  return {trampoline_start, {buffer.begin(), buffer.end()}};
}

} // namespace arm64
} // namespace detail
} // namespace spud
