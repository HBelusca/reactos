#pragma once

#if defined(_M_IX86) || defined(_M_AMD64)
#include <arch/pc/hardware.h>
#include <arch/pc/pcbios.h>
#include <arch/pc/x86common.h>
#include <arch/pc/pxe.h>
#include <arch/i386/drivemap.h>
#endif
#if defined(_M_IX86)
#if defined(SARCH_PC98)
#include <arch/i386/machpc98.h>
#elif defined(SARCH_XBOX)
#include <arch/pc/machpc.h>
#include <arch/i386/machxbox.h>
#else
#include <arch/pc/machpc.h>
#endif
#include <arch/i386/i386.h>
#elif defined(_M_AMD64)
#include <arch/pc/machpc.h>
#include <arch/amd64/amd64.h>
#elif defined(_M_PPC)
#include <arch/powerpc/hardware.h>
#elif defined(_M_ARM)
#include <arch/arm/hardware.h>
#elif defined(_M_MIPS)
#include <arch/mips/arcbios.h>
#endif
