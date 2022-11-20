/*
 * COPYRIGHT:       GPL, see COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            drivers/base/kddll/kdserial.c
 * PURPOSE:         Serial communication functions for the kernel debugger.
 * PROGRAMMER:      Timo Kreuzer (timo.kreuzer@reactos.org)
 */

#include "kddll.h"

/* FUNCTIONS ******************************************************************/

/******************************************************************************
 * \name KdpSendBuffer
 * \brief Sends a buffer of data to the serial KD port.
 * \param Buffer Pointer to the data.
 * \param Size Size of data in bytes.
 */
VOID
NTAPI
KdpSendBuffer(
    IN PVOID Buffer,
    IN ULONG Size)
{
    PUCHAR ByteBuffer = Buffer;

    while (Size-- > 0)
    {
        KdpSendByte(*ByteBuffer++);
    }
}

/******************************************************************************
 * \name KdpReceiveBuffer
 * \brief Receives data from the KD port and fills a buffer.
 * \param Buffer Pointer to a buffer that receives the data.
 * \param Size Size of data to receive in bytes.
 * \return KdPacketReceived if successful.
 *         KdPacketTimedOut if the receive timed out.
 */
KDSTATUS
NTAPI
KdpReceiveBuffer(
    OUT PVOID Buffer,
    IN  ULONG Size)
{
    KDSTATUS Status;
    PUCHAR ByteBuffer = Buffer;
    UCHAR Byte;

    while (Size-- > 0)
    {
        /* Try to get a byte from the port */
        Status = KdpReceiveByte(&Byte);
        if (Status != KdPacketReceived)
            return Status;

        *ByteBuffer++ = Byte;
    }

    return KdPacketReceived;
}


/******************************************************************************
 * \name KdpReceivePacketLeader
 * \brief Receives a packet leader from the KD port.
 * \param PacketLeader Pointer to an ULONG that receives the packet leader.
 * \return KdPacketReceived if successful.
 *         KdPacketTimedOut if the receive timed out.
 *         KdPacketNeedsResend if a breakin byte was detected.
 */
KDSTATUS
NTAPI
KdpReceivePacketLeader(
    OUT PULONG PacketLeader)
{
    KDSTATUS KdStatus;
    UCHAR Index = 0, Byte, Buffer[4];

    /* Set first character to 0 */
    Buffer[0] = 0;

    do
    {
        /* Receive a single byte */
        KdStatus = KdpReceiveByte(&Byte);

        /* Check for timeout */
        if (KdStatus == KdPacketTimedOut)
        {
            /* Check if we already got a breakin byte */
            if (Buffer[0] == BREAKIN_PACKET_BYTE)
            {
                return KdPacketNeedsResend;
            }

            /* Report timeout */
            return KdPacketTimedOut;
        }

        /* Check if we received a byte */
        if (KdStatus == KdPacketReceived)
        {
            /* Check if this is a valid packet leader byte */
            if (Byte == PACKET_LEADER_BYTE ||
                Byte == CONTROL_PACKET_LEADER_BYTE)
            {
                /* Check if we match the first byte */
                if (Byte != Buffer[0])
                {
                    /* No, this is the new byte 0! */
                    Index = 0;
                }

                /* Store the byte in the buffer */
                Buffer[Index] = Byte;

                /* Continue with next byte */
                Index++;
                continue;
            }

            /* Check for breakin byte */
            if (Byte == BREAKIN_PACKET_BYTE)
            {
                KDDBGPRINT("BREAKIN_PACKET_BYTE\n");
                Index = 0;
                Buffer[0] = Byte;
                continue;
            }
        }

        /* Restart */
        Index = 0;
        Buffer[0] = 0;
    }
    while (Index < 4);

    /* Enable the debugger */
    KD_DEBUGGER_NOT_PRESENT = FALSE;
    SharedUserData->KdDebuggerEnabled |= 0x00000002;

    /* Return the received packet leader */
    *PacketLeader = *(PULONG)Buffer;

    return KdPacketReceived;
}

/* EOF */
