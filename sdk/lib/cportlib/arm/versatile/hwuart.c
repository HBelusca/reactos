/*
 * PROJECT:     ReactOS ComPort Library
 * LICENSE:     BSD - See COPYING.ARM in the top level directory
 * PURPOSE:     UART Initialization Routines for Versatile
 * COPYRIGHT:   Copyright 20xx ReactOS Portable Systems Group
 *              Copyright 2022 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#include <arm/peripherals/pl011.h>

//
// UART Registers
//
#define UART_PL01x_DR            (LlbHwVersaUartBase + 0x00)
#define UART_PL01x_RSR           (LlbHwVersaUartBase + 0x04)
#define UART_PL01x_ECR           (LlbHwVersaUartBase + 0x04)
#define UART_PL01x_FR            (LlbHwVersaUartBase + 0x18)
#define UART_PL011_IBRD          (LlbHwVersaUartBase + 0x24)
#define UART_PL011_FBRD          (LlbHwVersaUartBase + 0x28)
#define UART_PL011_LCRH          (LlbHwVersaUartBase + 0x2C)
#define UART_PL011_CR            (LlbHwVersaUartBase + 0x30)
#define UART_PL011_IMSC          (LlbHwVersaUartBase + 0x38)

//
// LCR Values
//
#define UART_PL011_LCRH_WLEN_8   0x60
#define UART_PL011_LCRH_FEN      0x10

//
// FCR Values
//
#define UART_PL011_CR_UARTEN     0x01
#define UART_PL011_CR_TXE        0x100
#define UART_PL011_CR_RXE        0x200

//
// LSR Values
//
#define UART_PL01x_FR_RXFE       0x10
#define UART_PL01x_FR_TXFF       0x20

static const ULONG LlbHwVersaUartBase = 0x101F1000;

//
// We need to build this in the configuration root and use
// KeFindConfigurationEntry() to recover it later.
//
#define HACK 24000000

/* FUNCTIONS ******************************************************************/

VOID
NTAPI
VersaEnableFifo(
    _In_ PUCHAR Address,
    _In_ BOOLEAN Enable)
{
    /* FIFO is always enabled */
}

// LlbHwVersaUartInitialize(VOID)
BOOLEAN
NTAPI
VersaInitializePort(
    _In_opt_ PCSTR LoadOptions,
    _Inout_ PCPPORT Port,
    _In_ BOOLEAN MemoryMapped,
    _In_ UCHAR AccessSize,
    _In_ UCHAR BitWidth)
{
    PUCHAR Address;
    ULONG BaudRate;
    ULONG Divider, Remainder, Fraction, ClockRate;

    UNREFERENCED_PARAMETER(LoadOptions);

    /* Validity checks */
    if (Port == NULL || Port->Address == NULL || Port->BaudRate == 0)
        return FALSE;

    Address = Port->Address;

    /* Query peripheral rate, hardcode baudrate (FIXME??) */
    ClockRate = /*LlbHwGetPClk();*/ HACK;
    BaudRate  = Port->BaudRate = 115200; // 115200 bps

    /* Calculate baudrate clock divider and remainder */
    Divider   = ClockRate / (16 * BaudRate);
    Remainder = ClockRate % (16 * BaudRate);

    /* Calculate the fractional part */
    Fraction  = (8 * Remainder / BaudRate) >> 1;
    Fraction += (8 * Remainder / BaudRate) & 1;

    /* Disable interrupts */
    WRITE_REGISTER_ULONG((PULONG)UART_PL011_CR, 0);

    /* Set the baud rate */
    WRITE_REGISTER_ULONG((PULONG)UART_PL011_IBRD, Divider);
    WRITE_REGISTER_ULONG((PULONG)UART_PL011_FBRD, Fraction);

    /* Set 8 bits for data, 1 stop bit, no parity, FIFO enabled */
    WRITE_REGISTER_ULONG((PULONG)UART_PL011_LCRH,
                         UART_PL011_LCRH_WLEN_8 | UART_PL011_LCRH_FEN);

    /* Clear and enable FIFO */
    WRITE_REGISTER_ULONG((PULONG)UART_PL011_CR,
                         UART_PL011_CR_UARTEN |
                         UART_PL011_CR_TXE |
                         UART_PL011_CR_RXE);

    return TRUE;
}

static BOOLEAN
LlbHwUartTxReady(VOID)
{
    /* TX output buffer is ready? */
    return ((READ_REGISTER_ULONG((PULONG)UART_PL01x_FR) & UART_PL01x_FR_TXFF) == 0);
}

// LlbHwUartSendChar(IN CHAR Char)
VOID
NTAPI
CpPutByte(
    _In_ PCPPORT Port,
    _In_ UCHAR Byte)
{
    /* Wait for ready */
    while (!LlbHwUartTxReady());

    /* Send the character */
    WRITE_REGISTER_ULONG((PULONG)UART_PL01x_DR, Byte);
}

ULONG
NTAPI
LlbHwGetUartBase(IN ULONG Port)
{
    if (Port == 0)
    {
        return 0x101F1000;
    }
    else if (Port == 1)
    {
        return 0x101F2000;
    }

    return 0;
}

/* EOF */
