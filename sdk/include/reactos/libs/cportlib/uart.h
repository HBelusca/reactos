/*
 * PROJECT:     ReactOS ComPort Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Header for the ComPort Library
 * COPYRIGHT:   Copyright 2022 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 *
 * NOTE: The CPortLib has been updated in Windows 10 and is now documented:
 * https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/uart/
 */

/* INCLUDES *******************************************************************/

#pragma once

//
// CPPORT Flags.
//
#define PORT_DEFAULT_RATE   0x0001
#define PORT_RING_INDICATOR 0x0002      // CPPORT_FLAG_MODEM_CONTROL
#define PORT_MODEM_CONTROL  0x0004
#define PORT_SAVED          0x0008
#define PORT_FORCE_32BIT_IO 0x0010

#ifndef _CPORTLIB_
typedef struct _CPPORT
{
    PUCHAR Address;  //< The base address of the UART registers.
    ULONG  BaudRate; //< The UART hardware's baud rate in bits per second.
    USHORT Flags;    //< Bitmask of the port's internal flags.
#if 0
    UCHAR  ByteWidth;   //< The width of each of the UART hardware's registers as a number of bytes.
    UART_HARDWARE_READ_INDEXED_UCHAR  Read;  //< Callback used to read from a register on the UART hardware.
    UART_HARDWARE_WRITE_INDEXED_UCHAR Write; //< Callback used to write to a register on the UART hardware.
#endif
} CPPORT, *PCPPORT;
#endif

/**
 * @brief   Describes the status of a UART operation.
 **/
typedef enum _UART_STATUS
{
/* #define CP_GET_SUCCESS  0 */
    UartSuccess, //< The operation was successful, for example if data was available.
/* #define CP_GET_NODATA   1 */
    UartNoData,  //< No data is available, but not due to an error condition.
/* #define CP_GET_ERROR    2 */
    UartError,   //< A UART error such as overrun, parity, framing, etc.
    UartNotReady //< The device is not ready.
} UART_STATUS, *PUART_STATUS;

/**
 * @brief
 * Initializes or resets the UART hardware. This callback function is called
 * before calling any other driver functions.
 *
 * @param[in]   LoadOptions
 * An optional null-terminated load option string.
 *
 * @param[in,out]   Port
 * A pointer to a CPPORT structure that is filled with information
 * about port initialization.
 *
 * @param[in]   MemoryMapped
 * A boolean value that indicates whether the UART hardware is accessed
 * through memory-mapped registers or legacy port I/O.
 *
 * @param[in]   AccessSize
 * An ACPI Generic Access Size value that indicates the type of bus access
 * that should be performed when accessing the UART hardware.
 *
 * @param[in]   BitWidth
 * A number that indicates the width of the UART registers.
 *
 * @return
 * Returns TRUE if the port has been successfully initialized, FALSE otherwise.
 **/
typedef BOOLEAN
(NTAPI *UART_INITIALIZE_PORT)(
    _In_opt_ PCSTR LoadOptions,
    _Inout_ PCPPORT Port,
    _In_ BOOLEAN MemoryMapped,
    _In_ UCHAR AccessSize,
    _In_ UCHAR BitWidth);

/**
 * @brief
 * Changes the baud rate of the UART hardware.
 *
 * @param[in,out]   Port
 * A pointer to a CPPORT structure that contains the address of
 * the port object that describes the UART hardware.
 *
 * @param[in]   BaudRate
 * The baud rate to set in bits per second.
 *
 * @return
 * Returns TRUE if the baud rate was programmed, FALSE otherwise.
 **/
typedef BOOLEAN
(NTAPI *UART_SET_BAUD)(
    _Inout_ PCPPORT Port,
    _In_ ULONG BaudRate /*Rate*/);

/**
 * @brief
 * Reads a data byte from the UART device.
 *
 * @param[in]   Port
 * A pointer to a CPPORT structure that contains the address of
 * the port object that describes the UART hardware.
 *
 * @param[out]  Byte
 * A pointer to a variable that contains received byte.
 *
 * @return
 * Returns a UART_STATUS value that indicates success or failure
 * of the operation.
 **/
typedef UART_STATUS
(NTAPI *UART_GET_BYTE)(
    _In_ /*_Inout_*/ PCPPORT Port,
    _Out_ PUCHAR Byte);

/**
 * @brief
 * Writes a data byte to the UART device.
 *
 * @param[in]   Port
 * A pointer to a CPPORT structure that contains the address of
 * the port object that describes the UART hardware.
 *
 * @param[in]   Byte
 * The byte to write to the UART hardware.
 *
 * @param[in]   BusyWait
 * A flag to control whether this routine will busy-wait (spin)
 * for the UART hardware to be ready to transmit.
 *
 * @return
 * Returns a UART_STATUS value that indicates success or failure
 * of the operation.
 **/
typedef UART_STATUS
(NTAPI *UART_PUT_BYTE)(
    _In_ /*_Inout_*/ PCPPORT Port,
    _In_ UCHAR Byte,
    _In_ BOOLEAN BusyWait);

/**
 * @brief
 * Determines whether there is data pending in the UART hardware.
 *
 * @param[in]   Port
 * A pointer to a CPPORT structure that contains the address of
 * the port object that describes the UART hardware.
 *
 * @return
 * Returns TRUE if data is available, FALSE otherwise.
 **/
typedef BOOLEAN
(NTAPI *UART_RX_READY)(
    _In_ /*_Inout_*/ PCPPORT Port);


typedef struct _UART_HARDWARE_DRIVER
{
    UART_INITIALIZE_PORT InitializePort;
    UART_SET_BAUD        SetBaud;
    UART_GET_BYTE        GetByte;
    UART_PUT_BYTE        PutByte;
    UART_RX_READY        RxReady;
#if 0 // Not yet defined
    UART_SET_POWER_D0    SetPowerD0;
    UART_SET_POWER_D3    SetPowerD3;
#endif
} UART_HARDWARE_DRIVER, *PUART_HARDWARE_DRIVER;

/* EOF */
