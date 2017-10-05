
#ifndef __VHDDUMP_H__
#define __VHDDUMP_H__

#pragma once

#include <stdio.h>

#define WIN32_NO_STATUS
#include <windows.h>
//#include <windef.h>
//#include <winbase.h>

VOID DisplayMessage(IN LPCWSTR Format, ...);

#endif // __VHDDUMP_H__
