/*
 * PROJECT:     ReactOS study module
 * PURPOSE:     Public header of mymodule.dll
 */

#ifndef _MYMODULE_H
#define _MYMODULE_H

#pragma once

#include <ntdef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The signature of MyFunction depends on the OS version the consumer
 * is built for. The DLL internally selects the proper implementation
 * at runtime. When compiling the DLL itself (_MYMODULE_ defined),
 * MyFunction is not declared, since the DLL does not define it by that name.
 */
#ifndef _MYMODULE_
#if (NTDDI_VERSION >= NTDDI_VISTA)
UCHAR NTAPI MyFunction(int param1, int param2);
#else
UCHAR NTAPI MyFunction(int param1);
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* _MYMODULE_H */
