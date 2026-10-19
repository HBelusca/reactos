#include <ntdef.h>
#include <debug.h>

#define print_caller() DbgPrint("STACK PROTECTOR FAULT AT %p\n", __builtin_extract_return_addr(__builtin_return_address(0)))

/* Should be random :-/ */
void * __stack_chk_guard = (void*)0xb00fbeefbaafb00f;

void __stack_chk_fail()
{
    print_caller();
    __asm__("int $3");
}
