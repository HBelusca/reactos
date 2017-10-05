
#include "vhddump.h"

VOID
DisplayMessage(IN LPCWSTR Format, ...)
{
    va_list args;

    va_start(args, Format);
    vwprintf(Format, args);
    va_end(args);
}
