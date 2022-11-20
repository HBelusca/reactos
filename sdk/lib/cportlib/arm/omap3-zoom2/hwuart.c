/*
 * PROJECT:     ReactOS ComPort Library
 * LICENSE:     BSD - See COPYING.ARM in the top level directory
 * PURPOSE:     UART Initialization Routines for OMAP3 ZOOM2
 * COPYRIGHT:   Copyright 20xx ReactOS Portable Systems Group
 *              Copyright 2022 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#include <cportlib/cportlib.h>

#define SERIAL_REGISTER_STRIDE 2

#define Uart16550EnableFifo     Omap3EnableFifo
#define Uart16550DoesPortExist  Omap3DoesPortExist
#define Uart16550InitializePort Omap3InitializePort
#define Uart16550SetBaud        Omap3SetBaud
#define Uart16550GetByte        Omap3GetByte
#define Uart16550PutByte        Omap3PutByte
#define Uart16550RxReady        Omap3RxReady
#define Uart16550HardwareDriver Omap3HardwareDriver

#include "ns16550.c"

/* GLOBALS ********************************************************************/

#define SERIAL_TL16CP754C_QUAD0_BASE (PVOID)0x10000000
// #define SERIAL_TL16CP754C_BASE  0x10000000   /* Zoom2 Serial chip address */

#define QUAD_BASE_0     SERIAL_TL16CP754C_BASE
#define QUAD_BASE_1     (SERIAL_TL16CP754C_BASE + 0x100)
#define QUAD_BASE_2     (SERIAL_TL16CP754C_BASE + 0x200)
#define QUAD_BASE_3     (SERIAL_TL16CP754C_BASE + 0x300)

/* FUNCTIONS ******************************************************************/

VOID
NTAPI
LlbHwOmap3UartInitialize(VOID)
{
    CpInitialize(&LlbHwOmap3UartPorts[0], SERIAL_TL16CP754C_QUAD0_BASE, 115200);
}

// LlbHwUartSendChar(IN CHAR Char)
{
    /* Send the character */
    CpPutByte(&LlbHwOmap3UartPorts[0], Char);
}

ULONG
NTAPI
LlbHwGetUartBase(IN ULONG Port)
{
    if (Port == 0)
    {
        return SERIAL_TL16CP754C_QUAD0_BASE;
    }

    return 0;
}

/* EOF */
