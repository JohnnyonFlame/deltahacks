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

void game_change_reimpl(RValue *ret, void *self, void *other, int argc, RValue *args)
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
    exit(-1); // probably a better way than murdering the process...
}

void (*gpu_set_texfilter)(RValue *ret, void *self, void *other, int argc, RValue *args) = NULL;
void gpu_set_texfilter_reimpl(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    /* We're patching the runner to auto-convert to RGBA so report that. */
    args[0].kind = 0;
    args[0].rvalue.val = 0.0;
    gpu_set_texfilter(ret, self, other, argc, args);
}

uintptr_t align = 0;
uintptr_t page = 0;
uint8_t os_type_funct[128] = {};
uint8_t game_unx_funct[128] = {};
int (*GetSaveFileName_entry)(char *dst, int unk, char *src) = (decltype(GetSaveFileName_entry))0x00799b10;
uint8_t GetSaveFileName_trampoline[128] = {};
uint8_t GetSaveFileName_prologue[128] = {};
void (* Function_Add_entry)(const char *name, uintptr_t entry, int argc, char reg) = (decltype(Function_Add_entry))0x4323a0;
uint8_t Function_Add_trampoline[128] = {};
uint8_t Function_Add_prologue[128] = {};
void (* OsType_entry) = (decltype(OsType_entry))0x004bac00;
void (* AssetFolder_entry) = (decltype(AssetFolder_entry))0x00794090;


template <typename T, typename T2>
static constexpr T bit_cast(T2 v)
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
void memcpy_code(T1 dst, T2 src, size_t len)
{    
    void *align_dst = (void*)ALIGN(dst);
    mprotect(align_dst, page, PROT_READ | PROT_WRITE);
    memcpy((void *)dst, (void *)src, len);
    mprotect(align_dst, page, PROT_EXEC | PROT_READ);
    __builtin___clear_cache((void *)dst, (void *)((uintptr_t)dst + len));
}

template <typename T1, typename T2>
void memcpy_const(T1 dst, T2 src, size_t len)
{    
    void *align_dst = (void*)ALIGN(dst);
    mprotect(align_dst, page, PROT_READ | PROT_WRITE);
    memcpy((void *)dst, (void *)src, len);
    mprotect(align_dst, page, PROT_READ);
}

int GetSaveFileName_Hack(char *dst, int unk, char *src)
{
    // Gamemaker is kinda silly, and forces files to be lowercase on Linux,
    // we're going to hack this function to check if the original name is valid
    // and restore it, this fixes some music not playing.

    memcpy_code(GetSaveFileName_entry, GetSaveFileName_prologue, sizeof(GetSaveFileName_prologue));
    int ret = GetSaveFileName_entry(dst, unk, src);
    struct stat stt = {};
    if (stat(src, &stt) == 0)
        strcpy(dst, src);
        
    memcpy_code(GetSaveFileName_entry, GetSaveFileName_trampoline, sizeof(GetSaveFileName_trampoline));
    return ret;
}

void Function_Add_hack(const char *name, uintptr_t entry, int argc, char reg)
{
    // Pretty obvious - a function_add hook in order to rework functionality.
    memcpy_code(Function_Add_entry, Function_Add_prologue, sizeof(Function_Add_prologue));
    
    if (strcmp(name, "game_change") == 0)
    {
        printf("Function_Add_hack!! %s\n", name);
        Function_Add_entry(name, (uintptr_t)game_change_reimpl, argc, reg);
    }
    else if (strcmp(name, "gpu_set_texfilter") == 0)
    {
        printf("Function_Add_hack!! %s\n", name);
        Function_Add_entry(name, (uintptr_t)gpu_set_texfilter_reimpl, argc, reg);
        gpu_set_texfilter = (decltype(gpu_set_texfilter))entry;
    }
    else
    {
        Function_Add_entry(name, entry, argc, reg);
    }

    memcpy_code(Function_Add_entry, Function_Add_trampoline, sizeof(Function_Add_trampoline));
}

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

    /*
      Let's hack Function_Add so we can rework things!
    */
    struct FCT_ADD_PAYLOAD : Xbyak::CodeGenerator {
        FCT_ADD_PAYLOAD(uint8_t *ptr) : Xbyak::CodeGenerator(sizeof(Function_Add_trampoline), (void*) ptr)
        {
            push(rbx);
            mov(rbx, (uintptr_t)Function_Add_hack);
            call(rbx);
            pop(rbx);
            ret();
            nop(); /* buncha nops to make sure we dont get odd instructions */
            nop();
            nop();
            nop();
            nop();
            nop();
        }
    };

    FCT_ADD_PAYLOAD fct_add_payload(Function_Add_trampoline);
    fct_add_payload.ready();

    /*
      Let's improve the odd 'tolower' that gms does on file open...
    */
    struct FCT_GET_SAVE_FILENAME : Xbyak::CodeGenerator {
        FCT_GET_SAVE_FILENAME(uint8_t *ptr) : Xbyak::CodeGenerator(sizeof(GetSaveFileName_trampoline), (void*) ptr)
        {
            push(rbx);
            mov(rbx, (uintptr_t)GetSaveFileName_Hack);
            call(rbx);
            pop(rbx);
            ret();
            nop(); /* buncha nops to make sure we dont get odd instructions */
            nop();
            nop();
            nop();
            nop();
            nop();
        
        }
    };

    FCT_GET_SAVE_FILENAME fct_get_save_filename(GetSaveFileName_trampoline);
    fct_get_save_filename.ready();

    /*
      This is 100% a windows game, trust me, we're not on linux :)
    */
    struct FCT_OS_TYPE : Xbyak::CodeGenerator {
        FCT_OS_TYPE(uint8_t *ptr) : Xbyak::CodeGenerator(sizeof(os_type_funct), (void*) ptr)
        {
            mov(dword [rdx + 0xc], 0x0);
            mov(rax, bit_cast<uint64_t>((double)0.0)); //os_windows
            mov(qword [rdx], rax);
            mov(al, 0x1);
            ret();
        }
    };

    FCT_OS_TYPE fct_os_type(os_type_funct);
    fct_os_type.ready();

    /*
      Don't let the runner override -game data.win with -game game.unx
    */
    struct GAME_UNX_OVERLOAD : Xbyak::CodeGenerator {
        GAME_UNX_OVERLOAD(uint8_t *ptr) : Xbyak::CodeGenerator(5, (void*) ptr)
        {
            nop(5);
        }
    };

    GAME_UNX_OVERLOAD fct_game_unx(game_unx_funct);
    fct_game_unx.ready();

    memcpy(GetSaveFileName_prologue, (void *)GetSaveFileName_entry, sizeof(GetSaveFileName_prologue));
    memcpy_code(GetSaveFileName_entry, (void *)GetSaveFileName_trampoline, fct_get_save_filename.getSize());
    memcpy(Function_Add_prologue, (void *)Function_Add_entry, sizeof(Function_Add_prologue));
    memcpy_code(Function_Add_entry, Function_Add_trampoline, sizeof(Function_Add_trampoline));
    memcpy_code(OsType_entry, os_type_funct, fct_os_type.getSize());
    memcpy_code((void*)0x00794746, game_unx_funct, fct_game_unx.getSize());
    memcpy_const((void *)0x00849ffa, "./", 3); // use workdir instead of "assets/"
    // memcpy_const((void *)0x0088cd52, "-game data.win", 15); // don't force "-game game.unx" on my cmdline ya doofus
    /* all done! */
}