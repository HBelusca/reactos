/*
 * PROJECT:     ReactOS study module
 * PURPOSE:     Export of a function whose signature depends on the OS version
 *              (1 parameter on NT5.x, 2 parameters on NT6+), with the actual
 *              implementation chosen at runtime, at its first invocation.
 *
 * Flow:
 *   consumer --> MyFunction (export, == MyFunction_Fwd)
 *                  `--> jmp *MyFunction_Stub
 *                        - 1st call: MyFunction_Trampoline, which calls
 *                          __init_MyFunction() to select NT5 or NT6, stores it
 *                          into MyFunction_Stub, then tail-jumps to it;
 *                        - next calls: MyFunction_NT5 / MyFunction_NT6 directly.
 *
 * On x86, the callee (stdcall) pops its own arguments (4 or 8 bytes). The
 * trampoline and the forwarder cannot know how many were pushed, so they must
 * never return by themselves: they tail-jump with the caller's stack untouched.
 * On x64 there is no callee cleanup, so a plain C call is enough.
 */

#include <ntddk.h>

/* Do not see the public MyFunction prototype here */
#ifndef _MYMODULE_
#define _MYMODULE_
#endif
#include "mymodule.h"

/* Compiler-specific helpers */
#if defined(_MSC_VER)
#define NOINLINE    __declspec(noinline)
#else
#define NOINLINE    __attribute__((noinline))
#endif

/* The real implementations */
UCHAR NTAPI MyFunction_NT5(int param1)
{
    return (UCHAR)(param1 + 5);
}

UCHAR NTAPI MyFunction_NT6(int param1, int param2)
{
    return (UCHAR)(param1 + param2 + 6);
}

/* Forward declaration; the trampoline is the initial target of the stub */
UCHAR __cdecl MyFunction_Trampoline(int param1, int param2);

typedef UCHAR (NTAPI *PMYFUNCTION_NT5)(int param1);
typedef UCHAR (NTAPI *PMYFUNCTION_NT6)(int param1, int param2);

/* Generic type of the stub; its actual signature is NT5 or NT6 */
typedef UCHAR (NTAPI *PMYFUNCTION_STUB)(int param1, int param2);

/* Initially points to the trampoline, then to MyFunction_NT5/NT6 */
static PMYFUNCTION_STUB MyFunction_Stub = (PMYFUNCTION_STUB)MyFunction_Trampoline;

/* Selects the implementation. Never returns NULL. */
static NOINLINE PVOID __cdecl
__init_MyFunction(VOID)
{
    ULONG MajorVersion; //, MinorVersion;
    PMYFUNCTION_STUB Stub;
    PMYFUNCTION_NT5 pfnNT5 = MyFunction_NT5;
    PMYFUNCTION_NT6 pfnNT6 = MyFunction_NT6;

    (void)PsGetVersion(&MajorVersion, NULL /*&MinorVersion*/, NULL, NULL);
    if (MajorVersion < 6)
        Stub = (PMYFUNCTION_STUB)pfnNT5;
    else // if (MajorVersion >= 6)
        Stub = (PMYFUNCTION_STUB)pfnNT6;

    MyFunction_Stub = Stub;
    return (PVOID)Stub;
}

/*
 * Per-function code generation control. These functions must not get
 * stack-checking / runtime-check instrumentation, and must not have a
 * frame that we don't know about when we jump out of them.
 */
#if defined(_MSC_VER)
#pragma runtime_checks("", off)
#pragma check_stack(off)
#pragma strict_gs_check(push, off)
#pragma warning(push)
#pragma warning(disable:4731) // frame pointer register 'ebp' modified by inline assembly code
#define NO_CHECKS   __declspec(safebuffers)
#elif defined(__GNUC__)
#if (__GNUC__ >= 11)
#define NO_CHECKS   __attribute__((no_stack_protector))
#else
#define NO_CHECKS   /* Nothing: no arrays nor address-taken locals => no canary */
#endif
#endif

#if defined(_M_IX86)

/*
 * x86: the epilogue of the function is skipped. The frame is ebp-based and
 * holds only the locals, so "leave" restores esp/ebp as they were on entry.
 */
#if defined(_MSC_VER)

#define TAIL_JUMP_TO_REG(fn) \
    __asm { mov eax, fn } __asm { mov esp, ebp } __asm { pop ebp } __asm { jmp eax }
#define TAIL_JUMP_TO_MEM(mem) \
    __asm { mov esp, ebp } __asm { pop ebp } __asm { jmp dword ptr [mem] }
#define FWD_CC      __cdecl /* spec2def --ms: undecorated forwarder target */
#define TRAMP_ATTR  NO_CHECKS

#else // GCC

#define TAIL_JUMP_TO_REG(fn) \
    __asm__ __volatile__("leave\n\tjmp *%%eax" : : "a"(fn)); __builtin_unreachable()
#define TAIL_JUMP_TO_MEM(mem) \
    __asm__ __volatile__("jmp *%0" : : "m"(mem)); __builtin_unreachable()
#define FWD_CC      NTAPI   /* spec2def GCC: MyFunction=MyFunction_Fwd@N */
#define TRAMP_ATTR  NO_CHECKS __attribute__((optimize("no-omit-frame-pointer")))

#endif

TRAMP_ATTR UCHAR __cdecl
MyFunction_Trampoline(int param1, int param2)
{
    PVOID Target = __init_MyFunction();
    UNREFERENCED_PARAMETER(param1);
    UNREFERENCED_PARAMETER(param2);
    TAIL_JUMP_TO_REG(Target);
}

#else // x64 (and others): no callee stack cleanup, a normal call is fine.

#if defined(__GNUC__)
/* Optimized, for the call below to become a direct tail-jump */
__attribute__((optimize("O2")))
#endif
UCHAR __cdecl
MyFunction_Trampoline(int param1, int param2)
{
    PMYFUNCTION_STUB Target = (PMYFUNCTION_STUB)__init_MyFunction();
    return Target(param1, param2);
}

#endif

/*
 * The exported function. Its arguments are not touched, whatever their number.
 * It must be reached via the MyFunction=MyFunction_Fwd redirection in the .spec
 */
#if (NTDDI_VERSION >= NTDDI_VISTA)
#define FWD_PARAMS  int param1, int param2
#else
#define FWD_PARAMS  int param1
#endif

#if defined(_M_IX86)
#if defined(__GNUC__)
/* Frameless, so that the jump leaves the caller's stack untouched. */
__attribute__((optimize("O2", "omit-frame-pointer")))
#endif
UCHAR FWD_CC
MyFunction_Fwd(FWD_PARAMS)
{
    TAIL_JUMP_TO_MEM(MyFunction_Stub);
}
#else
#if defined(__GNUC__)
__attribute__((optimize("O2")))
#endif
UCHAR NTAPI
MyFunction_Fwd(int param1, int param2)
{
    return MyFunction_Stub(param1, param2);
}
#endif

#if defined(_MSC_VER)
#pragma warning(pop)
#pragma strict_gs_check(pop)
#endif
