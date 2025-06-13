#pragma once

#include <xbyak.h>
#include <cstdint>
#include <sys/mman.h>

template <typename Func>
struct Hook {
    static const unsigned long max_code_size = 128;

    size_t length;
    Func entry;
    uint8_t prologue[max_code_size];
    uint8_t hook[max_code_size];

    void apply() {
        memcpy_code((void *)entry, hook, length);
    }

    void restore() {
        memcpy_code((void *)entry, prologue, length);
    }

    void prepare(Xbyak::CodeGenerator &cgen, uintptr_t entry, bool apply) {
        cgen.ready();
        this->entry = (Func)entry;
        this->length = cgen.getSize();
        memcpy((void *)prologue, (void *)entry, cgen.getSize());

        if (apply)
            this->apply();
    }
};
