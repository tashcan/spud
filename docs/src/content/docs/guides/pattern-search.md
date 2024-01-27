---
title: Search for a pattern
description: Introduction to pattern search in memory.
---

## Overview

Often you'd want to search for a specific sequence of instructions to find a function to hook/patch. Oftentimes this also involves having wildcards as part of that search.
For this spud has functions that enable fast search in a specific memory range
utilizing the `spud::find_matches` facilities.

## Example

Provided the function prologue of what we are looking for is the following
```
mov     [rsp-8+arg_8], rbx
mov     [rsp-8+arg_10], rsi
push    rbp
push    rdi
push    r12
push    r14
push    r15
lea     rbp, 
sub     rsp, 0D0h
```
an appropriate pattern would be something like this
`48 89 5C 24 ? 48 89 74 24 ? 55 57 41 54 41 56 41 57 48 8D 6C 24 ? 48 81 EC ? ? ? ?`

now, we would like to find this function in memory, independent of where it was mapped to.
To do so we can use the following code.

```c++
const auto search_buffer = std::span(start, end);
const auto result = spud::detail::find_matches("48 89 5C 24 ? 48 89 74 24 ? 55 57 41 54 41 56 41 57 48 8D 6C 24 ? 48 81 EC ? ? ? ?", search_buffer);

// Get address of match
const auto function_addr = result.address();

// Do patching
...
```


## Windows specific

On windows, you can use `spud::find_in_module` to search for a pattern in a specific module.

```c++
const auto result = spud::find_in_module("48 89 5C 24 ? 48 89 74 24 ? 55 57 41 54 41 56 41 57 48 8D 6C 24 ? 48 81 EC ? ? ? ?", "Game.exe");
// Or search in the main module, you can omit the module
const auto result = spud::find_in_module("48 89 5C 24 ? 48 89 74 24 ? 55 57 41 54 41 56 41 57 48 8D 6C 24 ? 48 81 EC ? ? ? ?");
```