
// #ifdef _USE_ML
// #include <asm.inc>
// #include <kxarm.h>

#ifdef _M_IX86

#include <asm.inc>
.code32

END

#elif defined(_M_AMD64)

#include <asm.inc>
.code64

#if defined(_USE_ML) && (_MSC_VER < 1700)

#undef lgdt
#undef lidt

// void __fastfail(unsigned int Code<rcx>);
PUBLIC __fastfail
__fastfail:
    // mov ecx, [rsp + 4]
    int HEX(29)

// void __lgdt(void *Source<rcx>);
PUBLIC __lgdt
__lgdt:
#ifdef _USE_ML
    lgdt fword ptr [rcx]
#else
    lgdt cs:[rcx]
#endif
    ret

// void __lidt(void *Source<rcx>);
PUBLIC __lidt
__lidt:
#ifdef _USE_ML
    lidt fword ptr [rcx]
#else
    lidt cs:[rcx]
#endif
    ret

// void _sgdt(void *Destination<rcx>);
// void __sgdt(void *Destination<rcx>);
PUBLIC _sgdt
PUBLIC __sgdt
_sgdt:
__sgdt:
#ifdef _USE_ML
    sgdt fword ptr [rcx]
#else
    sgdt cs:[rcx]
#endif
    ret

// void _sidt(void *Destination<rcx>);
// void __sidt(void *Destination<rcx>);
// PUBLIC _sidt
PUBLIC __sidt
// _sidt:
__sidt:
#ifdef _USE_ML
    sidt fword ptr [rcx]
#else
    sidt cs:[rcx]
#endif
    ret

#endif /* defined(_USE_ML) && (_MSC_VER < 1700) */

END

#elif defined(_M_ARM)

#include <kxarm.h>
    END

#else
#error "Unsupported platform."
#endif
