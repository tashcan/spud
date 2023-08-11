#pragma once

// From:
// http://stackoverflow.com/questions/1082192/how-to-generate-random-variable-names-in-c-using-macros/17624752#17624752
//
#define SPUD_PP_CAT(a, b) SPUD_PP_CAT_I(a, b)
#define SPUD_PP_CAT_I(a, b) SPUD_PP_CAT_II(~, a##b)
#define SPUD_PP_CAT_II(p, res) res

namespace spud {
void *alloc_executable_memory(size_t size);
}
