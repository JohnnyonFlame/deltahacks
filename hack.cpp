#include <sys/types.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>
#include <xbyak.h>
#include <limits.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#include "gamemaker.hpp"
#include "misc.hpp"
#include "hook.hpp"

uintptr_t align = 0;
uintptr_t page = 0;

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

        /* hook (disabled) */
        hook.prepare(*this, entry, false);
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
        //   Gamemaker is kinda silly, and forces files to be lowercase on Linux,
        // we're going to hack this function to check if the original name is valid
        // and restore it, this fixes some music not playing.
        //   If neither the original nor the lowercase version exists, we're going
        // to return a path into ~/.config/<GAME> in order to avoid saving into pwd.

        hook.restore();
        int ret = hook.entry(dst, unk, src);
        struct stat stt = {};
        
        if (stat(src, &stt) == 0) {
            // Does the original filename exist? Let's first look for that
            strcpy(dst, src);
        } else if (stat(dst, &stt) != 0) {
            // Does the lowercase filename exist? If not, let's return a path
            // into the savefolder, so that save files are redirected into the
            // correct folder.
            sprintf(dst, "%s%s", GetSavePrePendHook::hook.entry(), src);
        }
            
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

int open(const char *pathname, int flags, ...)
{
    #define MAX_DEV 128
    static bool is_blacklisted[MAX_DEV] = {};

    va_list args;
    va_start(args, flags);

    //fprintf(stderr, "open(%s, %d, ...);\n", pathname, flags);
    static int (*or_open)(const char *, int, ...) = NULL;
    if (!or_open && !(or_open = (decltype(or_open))dlsym(RTLD_NEXT, "open"))) {
        fprintf(stderr, "Catastrophic failure: dlsym(..., ""open"") == NULL.\n");
        exit(1);
    }

    int fd = -1;
    if (strncmp(pathname, "/dev/input/event", 16) == 0){
        int devno = strtol(&((const char*)pathname)[16], NULL, 10);
        if (devno < 0 || devno >= MAX_DEV || is_blacklisted[devno])
            goto open_end;

        if (flags & O_CREAT)
            fd = or_open(pathname, flags, va_arg(args, mode_t));
        else
            fd = or_open(pathname, flags);

        if (fd < 0) {
            goto open_end;
        }

        struct input_id id;
        if (ioctl(fd, EVIOCGID, &id) < 0) {
            goto open_fake_fail;
        }

        // We're skipping the following Steam Deck device:
        // Input device ID: bus 0x11 vendor 0x1 product 0x1 version 0xab83
        // Input device name: "AT Translated Set 2 keyboard"
        if (id.bustype == 0x11 && id.version == 0xab83) {
            is_blacklisted[devno] = true;
            goto open_fake_fail;
        }

        /* success: fall-through */
    } else { 
        if (flags & O_CREAT)
            fd = or_open(pathname, flags, va_arg(args, mode_t));
        else
            fd = or_open(pathname, flags);
    }

open_end:
    va_end(args);
    return fd;
open_fake_fail:
    va_end(args);
    close(fd);
    return -1;
}

void DeltaHacks() __attribute__((constructor));
void DeltaHacks()
{
    char linkname[PATH_MAX] = {};
    readlink("/proc/self/exe", linkname, PATH_MAX);

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