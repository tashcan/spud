#pragma once

#include "aarch64.h"

#include <capstone/capstone.h>

#include <asmjit/a64.h>
#include <asmjit/asmjit.h>

#include <unordered_map>
#include <span>

namespace spud::detail::arm64 {

struct relocation_meta {
  uintptr_t size;
  void (*gen_relo_data)(std::span<uint8_t>, const relocation_entry &,
                        asmjit::Label data_label, asmjit::a64::Assembler &,
                        const relocation_info &relocation_info);
  void (*gen_relo_code)(std::span<uint8_t>, const relocation_entry &relo,
                        const relocation_info &relocation_info,
                        asmjit::Label relocation_data,
                        asmjit::a64::Assembler &assembler);
  bool copy_instruction = false;
};

const relocation_meta &
get_relocator_for_instruction(const cs_insn &instruction);

} // namespace spud::detail::x64
