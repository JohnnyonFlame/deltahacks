#include <sys/types.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>
#include <xbyak.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "misc.hpp"

struct Ref {
    void *m_thing;
    int m_refCOunt;
    int m_size;
};

struct RValue {
    union {
        int v32;
        long long v64;
        double val;

        Ref *str;
    } rvalue;

    int flags;
    int kind;
};

uintptr_t align = 0;
uintptr_t page = 0;

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

struct FunctionAddHook : Xbyak::CodeGenerator {
    static inline Hook<void (*)(const char *, uintptr_t entry, int, char)> hook = {};

    static inline void (*game_end)(RValue *ret, void *self, void *other, int argc, RValue *args) = NULL;
    static inline void (*gpu_set_texfilter)(RValue *ret, void *self, void *other, int argc, RValue *args) = NULL;

    static void game_change_reimpl(RValue *ret, void *self, void *other, int argc, RValue *args)
    {
        const char *working_directory = (const char *)args[0].rvalue.str->m_thing;
        const char *launch_parameters = (const char *)args[1].rvalue.str->m_thing;

        printf("GAMECHANGE: %s %s\n", working_directory, launch_parameters);
        FILE *launch = fopen("/tmp/deltarune-launch-hack.lock", "w");
        if (!launch)
        {
            printf("CHANGEGAME FAILED!\n");
            abort();   
        }

        fprintf(launch, "%s\n", working_directory);
        fprintf(launch, "%s\n", launch_parameters);
        fclose(launch);
        game_end(ret, self, other, 0, args);
        // exit(-1); // probably a better way than murdering the process...
    }

    static void gpu_set_texfilter_reimpl(RValue *ret, void *self, void *other, int argc, RValue *args)
    {
        args[0].kind = 0;
        args[0].rvalue.val = 0.0;
        gpu_set_texfilter(ret, self, other, argc, args);
    }

    static void Function_Add_hack(const char *name, uintptr_t funct, int argc, char reg)
    {
        hook.restore();
        
        if (strcmp(name, "game_change") == 0)
        {
            printf("Function_Add_hack!! %s\n", name);
            hook.entry(name, (uintptr_t)game_change_reimpl, argc, reg);
        }
        else if (strcmp(name, "gpu_set_texfilter") == 0)
        {
            printf("Function_Add_hack!! %s\n", name);
            hook.entry(name, (uintptr_t)gpu_set_texfilter_reimpl, argc, reg);
            gpu_set_texfilter = (decltype(gpu_set_texfilter))funct;
        }
        else
        {
            if (strcmp(name, "game_end") == 0)
                game_end = (decltype(game_end))funct;
            hook.entry(name, funct, argc, reg);
        }

        hook.apply();
    }

    FunctionAddHook(uintptr_t entry) : Xbyak::CodeGenerator(hook.max_code_size, (void *)hook.hook) {
        /* assemble */
        push(rbx);
        mov(rbx, (uintptr_t)&Function_Add_hack);
        call(rbx);
        pop(rbx);
        ret();

        /* hook */
        hook.prepare(*this, entry, true);
    }
};

struct GetSavePrePendHook : Xbyak::CodeGenerator {
    static inline Hook<char *(*)(void)> hook = {};
    
    static char * GetSavePrePend_Hack()
    {
        hook.restore();
        char * ret = hook.entry();
        hook.apply();
        printf("GetSavePrePend: %s\n", ret);
        return ret;
    }

    GetSavePrePendHook(uintptr_t entry) : Xbyak::CodeGenerator(hook.max_code_size, (void *)hook.hook) {
        /* assemble */
        push(rbx);
        mov(rbx, (uintptr_t)&GetSavePrePend_Hack);
        call(rbx);
        pop(rbx);
        ret();

        /* hook */
        hook.prepare(*this, entry, true);
    }
};

struct OsTypeHook : Xbyak::CodeGenerator {
    static inline Hook<void *> hook = {};

    OsTypeHook(uintptr_t entry) : Xbyak::CodeGenerator(hook.max_code_size, (void *)hook.hook)
    {
        /* assemble */
        mov(dword [rdx + 0xc], 0x0);
        mov(rax, bit_cast<uint64_t>((double)0.0)); //os_windows
        mov(qword [rdx], rax);
        mov(al, 0x1);
        ret();

        /* hook */
        hook.prepare(*this, entry, true);
    }
};

struct GetSaveFilenameHook : Xbyak::CodeGenerator {
    static inline Hook<int (*)(char *, int, char *)> hook = {};

    static int GetSaveFileName_Hack(char *dst, int unk, char *src)
    {
        // Gamemaker is kinda silly, and forces files to be lowercase on Linux,
        // we're going to hack this function to check if the original name is valid
        // and restore it, this fixes some music not playing.

        hook.restore();
        int ret = hook.entry(dst, unk, src);
        struct stat stt = {};
        printf("%s %s\n", src, dst);
        if (stat(src, &stt) == 0)
            strcpy(dst, src);
            
        hook.apply();
        return ret;
    }

    GetSaveFilenameHook(uintptr_t entry) : Xbyak::CodeGenerator(hook.max_code_size, (void *)hook.hook)
    {
        /* assemble */
        push(rbx);
        mov(rbx, (uintptr_t)GetSaveFileName_Hack);
        call(rbx);
        pop(rbx);
        ret();

        /* hook */
        hook.prepare(*this, entry, true);
    }
};

struct GameUnxHook : Xbyak::CodeGenerator {
    static inline Hook<void *> hook = {};

    GameUnxHook(uintptr_t entry) : Xbyak::CodeGenerator(hook.max_code_size, (void *)hook.hook)
    {
        /* assemble */
        nop(5);

        /* hook */
        hook.prepare(*this, entry, true);
    }
};

void DeltaHacks() __attribute__((constructor));
void DeltaHacks()
{
    char linkname[PATH_MAX] = {};
    readlink("/proc/self/exe", linkname, PATH_MAX);
    printf("%s\n", linkname);

    if (strstr(linkname, "runner") == NULL)
        return;

    // consts
    page = getpagesize();
    align = ~(page - 1);

    FunctionAddHook(0x004323a0); // Let's detour a few GameMaker functions.
    GetSaveFilenameHook(0x00799b10); // GameMaker on Linux forces lowercase filenames, check if the non-lowercase exists first
    GetSavePrePendHook(0x007940a0); // Let's get some debugging on SavePrePend.
    OsTypeHook(0x004bac00); // Pretend we're running on Windows instead.
    GameUnxHook(0x00794746); // Don't let the runner override -game data.win with -game game.unx
    memcpy_const((void *)0x00849ffa, "./", 3); // use workdir instead of "assets/"
    /* all done! */
}