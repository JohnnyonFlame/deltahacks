#pragma once

#include <cstdint>
#include <cstddef>


extern uintptr_t align;
extern uintptr_t page;

template <typename T, typename T2>
static inline constexpr T bit_cast(T2 v)
{
    union {
        T2 v;
        T r;
    } temporary = {.v = v};

    return temporary.r;
}

template <typename T>
static uintptr_t ALIGN(const T ptr)
{
    return ((uintptr_t)ptr & align);
}

template <typename T1, typename T2>
static inline void memcpy_code(T1 dst, T2 src, size_t len)
{    
    void *align_dst = (void*)ALIGN(dst);
    mprotect(align_dst, page, PROT_READ | PROT_WRITE);
    memcpy((void *)dst, (void *)src, len);
    mprotect(align_dst, page, PROT_EXEC | PROT_READ);
    __builtin___clear_cache((void *)dst, (void *)((uintptr_t)dst + len));
}

template <typename T1, typename T2>
static inline void memcpy_const(T1 dst, T2 src, size_t len)
{    
    void *align_dst = (void*)ALIGN(dst);
    mprotect(align_dst, page, PROT_READ | PROT_WRITE);
    memcpy((void *)dst, (void *)src, len);
    mprotect(align_dst, page, PROT_READ);
}