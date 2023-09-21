#pragma once

#include "x86_64.h"

#include <Zydis/Utils.h>
#include <Zydis/Zydis.h>

#include <asmjit/asmjit.h>

#include <unordered_map>

namespace spud::detail::x64 {

struct RelocationMeta {
  uintptr_t size;
  uintptr_t (*expand)(const RelocationEntry &);
  bool (*gen_relo_data)(std::span<uint8_t>, const RelocationEntry &,
                        asmjit::x86::Assembler &,
                        const RelocationInfo &relocation_info);
  void (*gen_relo_code)(uintptr_t trampoline_address, std::span<uint8_t>,
                        const RelocationEntry &relo,
                        const RelocationInfo &relocation_info, bool has_data,
                        uintptr_t relocation_data,
                        asmjit::x86::Assembler &assembler);
  bool copy_instruction = false;
};

struct ReloInstruction {
  ZydisMnemonic i = ZYDIS_MNEMONIC_INVALID;
  ZydisBranchType b = ZYDIS_BRANCH_TYPE_NONE;

  constexpr ReloInstruction(ZydisMnemonic mn, ZydisBranchType b)
      : i(mn), b(b) {}
  constexpr ReloInstruction(ZydisMnemonic mn) : i(mn) {}
  constexpr ReloInstruction(ZydisDecodedInstruction instruction) {
    if (instruction.meta.branch_type != ZYDIS_BRANCH_TYPE_NONE) {
      i = ZYDIS_MNEMONIC_INVALID;
      b = ZYDIS_BRANCH_TYPE_MAX_VALUE;
    } else {
      i = instruction.mnemonic;
    }
  }

  bool operator==(const ReloInstruction &other) const {
    return (this->i == other.i && this->b == other.b);
  }
};

constexpr ReloInstruction JUMP_RELO_JMP_INSTRUCTION = {
    ZYDIS_MNEMONIC_INVALID, ZYDIS_BRANCH_TYPE_MAX_VALUE};

struct ReloInstructionHasher {
  std::size_t operator()(const ReloInstruction &relo) const {
    return relo.i << 16 | relo.b;
  }
};

extern const std::unordered_map<ReloInstruction, RelocationMeta,
                                ReloInstructionHasher>
    relo_meta;

} // namespace spud::detail::x64
