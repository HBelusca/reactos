/*
 * PROJECT:     ReactOS ComPort Library
 * LICENSE:     BSD - See COPYING.ARM in the top level directory
 * PURPOSE:     UART Initialization Routines for NS16550
 * COPYRIGHT:   Copyright 20xx ReactOS Portable Systems Group
 *              Copyright 2012-2022 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 *
 * NOTE: This library follows the precise serial port intialization steps documented
 * by Microsoft in some of their Server hardware guidance. Because they've clearly
 * documented their serial algorithms, we use the same ones to stay "compliant".
 * Do not change this code to "improve" it. It's done this way on purpose, at least on x86.
 * -- sir_richard
 *
 * REPLY: I reworked the COM-port testing code because the original one
 * (i.e. the Microsoft's documented one) doesn't work on Virtual PC 2007.
 * -- hbelusca
 *
 * NOTE: The original code supports Modem Control. We currently do not.
 */

/* INCLUDES *******************************************************************/

#include <intrin.h>
#include <ioaccess.h>
#include <cportlib/cportlib.h>
#include <drivers/serial/ns16550.h>
#include "hwinfo.h"

/* GLOBALS ********************************************************************/

UCHAR RingIndicator;

/* FUNCTIONS ******************************************************************/

VOID
NTAPI
Uart16550EnableFifo(
    _In_ PUCHAR Address,
    _In_ BOOLEAN Enable)
{
    /* Set FIFO and clear the receive/transmit buffers */
    WRITE_PORT_UCHAR(Address + FIFO_CONTROL_REGISTER,
                     Enable ? SERIAL_FCR_ENABLE | SERIAL_FCR_RCVR_RESET | SERIAL_FCR_TXMT_RESET
                            : SERIAL_FCR_DISABLE);
}

static BOOLEAN
ComPortTest1(_In_ PUCHAR Address)
{
    /*
     * See "Building Hardware and Firmware to Complement Microsoft Windows Headless Operation"
     * Out-of-Band Management Port Device Requirements:
     * The device must act as a 16550 or 16450 UART.
     * Windows Server 2003 will test this device using the following process:
     *     1. Save off the current modem status register.
     *     2. Place the UART into diagnostic mode (The UART is placed into loopback mode
     *        by writing SERIAL_MCR_LOOP to the modem control register).
     *     3. The modem status register is read and the high bits are checked. This means
     *        SERIAL_MSR_CTS, SERIAL_MSR_DSR, SERIAL_MSR_RI and SERIAL_MSR_DCD should
     *        all be clear.
     *     4. Place the UART in diagnostic mode and turn on OUTPUT (Loopback Mode and
     *         OUTPUT are both turned on by writing (SERIAL_MCR_LOOP | SERIAL_MCR_OUT1)
     *         to the modem control register).
     *     5. The modem status register is read and the ring indicator is checked.
     *        This means SERIAL_MSR_RI should be set.
     *     6. Restore original modem status register.
     *
     * REMARK: Strangely enough, the Virtual PC 2007 virtual machine
     *         doesn't pass this test.
     */

    BOOLEAN RetVal = FALSE;
    UCHAR Mcr, Msr;

    /* Save the Modem Control Register */
    Mcr = READ_PORT_UCHAR(Address + MODEM_CONTROL_REGISTER);

    /* Enable loop (diagnostic) mode (set Bit 4 of the MCR) */
    WRITE_PORT_UCHAR(Address + MODEM_CONTROL_REGISTER, SERIAL_MCR_LOOP);

    /* Clear all modem output bits */
    WRITE_PORT_UCHAR(Address + MODEM_CONTROL_REGISTER, SERIAL_MCR_LOOP);

    /* Read the Modem Status Register */
    Msr = READ_PORT_UCHAR(Address + MODEM_STATUS_REGISTER);

    /*
     * The upper nibble of the MSR (modem output bits) must be
     * equal to the lower nibble of the MCR (modem input bits).
     */
    if ((Msr & (SERIAL_MSR_CTS | SERIAL_MSR_DSR | SERIAL_MSR_RI | SERIAL_MSR_DCD)) == 0x00)
    {
        /* Set all modem output bits */
        WRITE_PORT_UCHAR(Address + MODEM_CONTROL_REGISTER,
                         SERIAL_MCR_OUT1 | SERIAL_MCR_LOOP); // Windows
/* ReactOS
        WRITE_PORT_UCHAR(Address + MODEM_CONTROL_REGISTER,
                         SERIAL_MCR_DTR | SERIAL_MCR_RTS | SERIAL_MCR_OUT1 | SERIAL_MCR_OUT2 | SERIAL_MCR_LOOP);
*/

        /* Read the Modem Status Register */
        Msr = READ_PORT_UCHAR(Address + MODEM_STATUS_REGISTER);

        /*
         * The upper nibble of the MSR (modem output bits) must be
         * equal to the lower nibble of the MCR (modem input bits).
         */
        if (Msr & SERIAL_MSR_RI) // Windows
        // if (Msr & (SERIAL_MSR_CTS | SERIAL_MSR_DSR | SERIAL_MSR_RI | SERIAL_MSR_DCD) == 0xF0) // ReactOS
        {
            RetVal = TRUE;
        }
    }

    /* Restore the MCR */
    WRITE_PORT_UCHAR(Address + MODEM_CONTROL_REGISTER, Mcr);

    return RetVal;
}

static BOOLEAN
ComPortTest2(_In_ PUCHAR Address)
{
    /*
     * This test checks whether the 16450/16550 scratch register is available.
     * If not, the serial port is considered as unexisting.
     */

    UCHAR Byte = 0;

    do
    {
        WRITE_PORT_UCHAR(Address + SCRATCH_REGISTER, Byte);

        if (READ_PORT_UCHAR(Address + SCRATCH_REGISTER) != Byte)
            return FALSE;

    } while (++Byte != 0);

    return TRUE;
}

BOOLEAN
NTAPI
Uart16550DoesPortExist(
    _In_ PUCHAR Address)
{
    return ( ComPortTest1(Address) || ComPortTest2(Address) );
}

BOOLEAN
NTAPI
Uart16550InitializePort(
    _In_opt_ PCSTR LoadOptions,
    _Inout_ PCPPORT Port,
    _In_ BOOLEAN MemoryMapped,
    _In_ UCHAR AccessSize,
    _In_ UCHAR BitWidth)
{
    PUCHAR Address;

    UNREFERENCED_PARAMETER(LoadOptions);

    /* Validity checks */
    if (Port == NULL || Port->Address == NULL || Port->BaudRate == 0)
        return FALSE;

    Address = Port->Address;

    if (!Uart16550DoesPortExist(Address))
        return STATUS_NOT_FOUND;

    /* Initialize port data */
    Port->Flags = 0;

    /* Disable the interrupts */
    WRITE_PORT_UCHAR(Address + LINE_CONTROL_REGISTER, 0);
    WRITE_PORT_UCHAR(Address + INTERRUPT_ENABLE_REGISTER, 0);

    /* Turn on DTR, RTS and OUT2 */
    WRITE_PORT_UCHAR(Address + MODEM_CONTROL_REGISTER,
                     SERIAL_MCR_DTR | SERIAL_MCR_RTS | SERIAL_MCR_OUT2);

    /* Set the baud rate */
    Uart16550SetBaud(Port, Port->BaudRate);

    /* Set 8 data bits, 1 stop bit, no parity, no break */
    WRITE_PORT_UCHAR(Port->Address + LINE_CONTROL_REGISTER,
                     SERIAL_8_DATA | SERIAL_1_STOP | SERIAL_NONE_PARITY);

    /* Turn on FIFO */
    // TODO: Check whether FIFO exists and turn it on in that case.
    CpEnableFifo(Address, TRUE); // for 16550

    /* Read junk out of the RBR */
    (VOID)READ_PORT_UCHAR(Address + RECEIVE_BUFFER_REGISTER);

    return TRUE;
}

#if defined(_M_IX86) || defined(_M_AMD64)
BOOLEAN
NTAPI
Legacy16550InitializePort(
    _In_opt_ PCSTR LoadOptions,
    _Inout_ PCPPORT Port,
    _In_ BOOLEAN MemoryMapped,
    _In_ UCHAR AccessSize,
    _In_ UCHAR BitWidth)
{
    ULONG ComPortNumber;

    /* Validity checks */
    if (Port == NULL)
        return FALSE;
    if (MemoryMapped != FALSE || /* AccessSize || */ BitWidth != 8)
        return FALSE;

    ComPortNumber = PtrToUlong(Port->Address);
    if (ComPortNumber <= 0 || ComPortNumber >= sizeof(BaseArray) / sizeof(BaseArray[0]))
        return FALSE;

    Port->Address = UlongToPtr(BaseArray[ComPortNumber]);
    ;

    return Uart16550InitializePort(LoadOptions, Port, FALSE, AccSize, 8);
}
#endif

BOOLEAN
NTAPI
Uart16550SetBaud(
    _In_ PCPPORT Port,
    _In_ ULONG BaudRate)
{
    UCHAR Lcr;
    ULONG Mode = CLOCK_RATE / BaudRate;

    /* Set the DLAB on */
    Lcr = READ_PORT_UCHAR(Port->Address + LINE_CONTROL_REGISTER);
    WRITE_PORT_UCHAR(Port->Address + LINE_CONTROL_REGISTER, Lcr | SERIAL_LCR_DLAB);

    /* Set the baud rate */
    WRITE_PORT_UCHAR(Port->Address + DIVISOR_LATCH_LSB, (UCHAR)(Mode & 0xFF));
    WRITE_PORT_UCHAR(Port->Address + DIVISOR_LATCH_MSB, (UCHAR)((Mode >> 8) & 0xFF));

    /* Reset DLAB */
    WRITE_PORT_UCHAR(Port->Address + LINE_CONTROL_REGISTER, Lcr);

    /* Save baud rate in port */
    Port->BaudRate = BaudRate;
    return TRUE;
}

static UCHAR
CpReadLsr(
    _In_ PCPPORT Port,
    _In_ UCHAR ExpectedValue)
{
    UCHAR Lsr, Msr;

    /* Read the LSR and check if the expected value is present */
    Lsr = READ_PORT_UCHAR(Port->Address + LINE_STATUS_REGISTER);
    if (!(Lsr & ExpectedValue))
    {
        /* Check the MSR for ring indicator toggle */
        Msr = READ_PORT_UCHAR(Port->Address + MODEM_STATUS_REGISTER);

        /* If the indicator reaches 3, we've seen this on/off twice */
        RingIndicator |= (Msr & SERIAL_MSR_RI) ? 1 : 2;
        if (RingIndicator == 3) Port->Flags |= CPPORT_FLAG_MODEM_CONTROL;
    }

    return Lsr;
}

UART_STATUS
NTAPI
Uart16550GetByte(
    _In_ /*_Inout_*/ PCPPORT Port,
    _Out_ PUCHAR Byte) //, IN BOOLEAN Poll
{
    UCHAR Lsr;

    /* Handle early read-before-init */
    if (!Port->Address)
        return UartNoData;

    /* Read LSR for data ready */
    Lsr = CpReadLsr(Port, SERIAL_LSR_DR);
    if ((Lsr & SERIAL_LSR_DR) == SERIAL_LSR_DR)
    {
        /* If an error happened, clear the byte and fail */
        if (Lsr & (SERIAL_LSR_FE | SERIAL_LSR_PE | SERIAL_LSR_OE))
        {
            *Byte = 0;
            return UartError;
        }

        // /* If only polling was requested by caller, return now */
        // if (Poll) return UartSuccess;

        /* Otherwise read the byte and return it */
        *Byte = READ_PORT_UCHAR(Port->Address + RECEIVE_BUFFER_REGISTER);

        /* Handle CD if port is in modem control mode */
        if (Port->Flags & CPPORT_FLAG_MODEM_CONTROL)
        {
            /* Not implemented yet */
            // DPRINT1("CP: CPPORT_FLAG_MODEM_CONTROL unexpected\n");
        }

        /* Byte was read */
        return UartSuccess;
    }

    /* Reset LSR, no data was found */
    CpReadLsr(Port, 0);
    return UartNoData;
}

UART_STATUS
NTAPI
Uart16550PutByte(
    _In_ /*_Inout_*/ PCPPORT Port,
    _In_ UCHAR Byte,
    _In_ BOOLEAN BusyWait)
{
    /* Check if port is in modem control to handle CD */
    // while (Port->Flags & CPPORT_FLAG_MODEM_CONTROL)  // Commented for the moment.
    if (Port->Flags & CPPORT_FLAG_MODEM_CONTROL)    // To be removed when this becomes implemented.
    {
        /* Not implemented yet */
        // DPRINT1("CP: CPPORT_FLAG_MODEM_CONTROL unexpected\n");
    }

    /* Wait for LSR to say we can go ahead */
    while ((CpReadLsr(Port, SERIAL_LSR_THRE) & SERIAL_LSR_THRE) == 0x00)
    {
        if (!BusyWait)
            return UartNotReady;
    }

    /* Send the byte */
    WRITE_PORT_UCHAR(Port->Address + TRANSMIT_HOLDING_REGISTER, Byte);
    return UartSuccess;
}

BOOLEAN
NTAPI
Uart16550RxReady(
    _In_ /*_Inout_*/ PCPPORT Port)
{
    /* Read LSR for data ready */
    Lsr = CpReadLsr(Port, SERIAL_LSR_DR);
    if ((Lsr & SERIAL_LSR_DR) == SERIAL_LSR_DR)
    {
        /* If an error happened, fail */
        if (Lsr & (SERIAL_LSR_FE | SERIAL_LSR_PE | SERIAL_LSR_OE))
            return FALSE; // UartError;

        return TRUE; // UartSuccess;
    }

    return FALSE; // UartNoData;
}


ENABLE_FIFO xxx = Uart16550EnableFifo;
DOES_PORT_EXIST xxx = Uart16550DoesPortExist;

#if defined(_M_IX86) || defined(_M_AMD64)
UART_HARDWARE_DRIVER
Legacy16550HardwareDrive =
{
    Legacy16550InitializePort,
    Uart16550SetBaud,
    Uart16550GetByte,
    Uart16550PutByte,
    Uart16550RxReady
};
#endif

UART_HARDWARE_DRIVER
Uart16550HardwareDriver =
{
    Uart16550InitializePort,
    Uart16550SetBaud,
    Uart16550GetByte,
    Uart16550PutByte,
    Uart16550RxReady
};

UART_HARDWARE_DRIVER
MM16550HardwareDriver =
{
    Uart16550InitializePort, // FIXME
    Uart16550SetBaud,
    Uart16550GetByte,
    Uart16550PutByte,
    Uart16550RxReady
};

/* EOF */
