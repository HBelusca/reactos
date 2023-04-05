/*
 * PROJECT:     ReactOS KDBG Kernel Debugger
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Kernel Debugger Initialization
 * COPYRIGHT:   Copyright 2020-2021 Hervé Poussineau <hpoussin@reactos.org>
 *              Copyright 2021 Jérôme Gardou <jerome.gardou@reactos.org>
 *              Copyright 2023 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/* INCLUDES ******************************************************************/

#if 0

#define NOEXTAPI
#include <ntifs.h>
#include <halfuncs.h>
#include <stdio.h>
#include <arc/arc.h>
#include <windbgkd.h>
#include <kddll.h>

#else

#include <ntoskrnl.h>

#endif

#include "kdb.h"

// See mm/ARM3/sysldr.c
extern PVOID
NTAPI
MiLocateExportName(IN PVOID DllBase,
                   IN PCHAR ExportName);

/* GLOBALS *******************************************************************/

PKD_TERMINAL pKdTerminal = NULL;

static ULONG KdbgNextApiNumber = DbgKdContinueApi;
static CONTEXT KdbgContext;
static EXCEPTION_RECORD64 KdbgExceptionRecord;
static BOOLEAN KdbgFirstChanceException;
static NTSTATUS KdbgContinueStatus = STATUS_SUCCESS;

/* FUNCTIONS *****************************************************************/

NTSTATUS
NTAPI
KdDebuggerInitialize0(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
#undef KdDebuggerInitialize0
#define pKdDebuggerInitialize0 KdDebuggerInitialize0
{
    static const UNICODE_STRING KdComLoadedName =
#ifdef _NTOSKRNL_
        RTL_CONSTANT_STRING(L"kdcom.dll");
#else
        RTL_CONSTANT_STRING(L"kdterm.dll");
#endif

    NTSTATUS Status;
    PLIST_ENTRY NextEntry;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    PCHAR CommandLine;

    /* Call KdTerm */
    Status = pKdDebuggerInitialize0(LoaderBlock);
    if (!NT_SUCCESS(Status))
        return Status;

    //
    // BUGBUG: FIXME: What happens when the KD DLL gets relocated when
    // Mm initializes?? Might be done "position-independent" by using:
    // LdrEntry->DllBase + (KdTerminal_address - original_DllBase).
    //
    /*
     * Check whether the KD transport DLL exports "KdTerminal" (only
     * present if this is KdTerm; otherwise this is a regular KD DLL).
     */
    /* Loop the loaded module list and find the KD DLL base address */
    for (NextEntry = LoaderBlock->LoadOrderListHead.Flink;
         NextEntry != &LoaderBlock->LoadOrderListHead;
         NextEntry = NextEntry->Flink)
    {
        /* Get the loader entry */
        LdrEntry = CONTAINING_RECORD(NextEntry,
                                     LDR_DATA_TABLE_ENTRY,
                                     InLoadOrderLinks);

        if (RtlEqualUnicodeString(&KdComLoadedName, &LdrEntry->BaseDllName, TRUE))
        {
            /* KD DLL found, check for the "KdTerminal" export */
            pKdTerminal = MiLocateExportName(LdrEntry->DllBase, "KdTerminal");
            break;
        }
    }

    if (LoaderBlock)
    {
        /* Check if we have a command line */
        CommandLine = LoaderBlock->LoadOptions;
        if (CommandLine)
        {
            /* Upcase it */
            _strupr(CommandLine);

            /* Get the KDBG Settings */
            KdbpGetCommandLineSettings(CommandLine);
        }
    }

    return KdbInitialize(0);
}

NTSTATUS
NTAPI
KdDebuggerInitialize1(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
#undef KdDebuggerInitialize1
#define pKdDebuggerInitialize1 KdDebuggerInitialize1
{
    NTSTATUS Status;

    /* Call KdTerm */
    Status = pKdDebuggerInitialize1(LoaderBlock);
    if (!NT_SUCCESS(Status))
        return Status;

    NtGlobalFlag |= FLG_STOP_ON_EXCEPTION;

    return KdbInitialize(1);
}

NTSTATUS
NTAPI
KdD0Transition(VOID)
#undef KdD0Transition
#define pKdD0Transition KdD0Transition
{
    /* Call KdTerm */
    return pKdD0Transition();
}

NTSTATUS
NTAPI
KdD3Transition(VOID)
#undef KdD3Transition
#define pKdD3Transition KdD3Transition
{
    /* Call KdTerm */
    return pKdD3Transition();
}

NTSTATUS
NTAPI
KdSave(
    _In_ BOOLEAN SleepTransition)
#undef KdSave
#define pKdSave KdSave
{
    /* Call KdTerm */
    return pKdSave(SleepTransition);
}

NTSTATUS
NTAPI
KdRestore(
    _In_ BOOLEAN SleepTransition)
#undef KdRestore
#define pKdRestore KdRestore
{
    /* Call KdTerm */
    return pKdRestore(SleepTransition);
}

VOID
NTAPI
KdSendPacket(
    _In_ ULONG PacketType,
    _In_ PSTRING MessageHeader,
    _In_opt_ PSTRING MessageData,
    _Inout_ PKD_CONTEXT Context)
#undef KdSendPacket
{
    if (PacketType == PACKET_TYPE_KD_DEBUG_IO)
    {
        PDBGKD_DEBUG_IO DebugIo = (PDBGKD_DEBUG_IO)MessageHeader->Buffer;
        ULONG ApiNumber;
        PCHAR Buffer;

        /* Validate API call */
        if (MessageHeader->Length != sizeof(DBGKD_DEBUG_IO))
            return;
        if ((DebugIo->ApiNumber != DbgKdPrintStringApi) &&
            (DebugIo->ApiNumber != DbgKdGetStringApi))
        {
            return;
        }
        if (!MessageData)
            return;

        /*
         * WinDbg expects, for DbgKdGetStringApi, to be sent only one such
         * packet and emit a reply to it.  Since we want to insert our own
         * KDBG prompt just after the original prompt and before receiving
         * the reply, we cannot send the original prompt as part of the
         * DbgKdGetStringApi call, then print our own prompt with a
         * DbgKdPrintStringApi call, and expect to receive a reply for the
         * original DbgKdGetStringApi call.  WinDbg would block the output
         * after the sent DbgKdGetStringApi to wait for user prompt, and
         * resume output afterwards.  Only then our KDBG prompt would be
         * displayed, and trigger a desynchronization with WinDbg for when
         * the next reply for DbgKdGetStringApi is expected.
         *
         * The only solution to this problem is to first print the original
         * prompt via a DbgKdPrintStringApi call, and then, print our own
         * KDBG prompt as part of the original DbgKdGetStringApi call.
         */

        ApiNumber = DebugIo->ApiNumber;
        if (ApiNumber == DbgKdGetStringApi)
        {
            /*
             * To print the original prompt, change its ApiNumber to
             * DbgKdPrintStringApi so that WinDbg thinks it's a regular
             * string being printed, and does not wait for user input.
             * NOTE: DebugIo points inside MessageHeader data.
             * Save also the original I/O buffer as we will modify it.
             */
            DebugIo->ApiNumber = DbgKdPrintStringApi;
            Buffer = MessageData->Buffer;

            /* Acquire the terminal since we are called for a prompt */
            if (&KD_TERM) KD_TERM.SetState(TRUE);
        }

        /* Print the string or the original prompt */
        KdbpPrintPacket(MessageHeader, MessageData, Context);

        if (ApiNumber == DbgKdGetStringApi)
        {
            extern const CSTRING KdbPromptStr;

            /* The original prompt string has been printed; go to the
             * new line and print the kdb prompt -- for SYSREG2 support. */
            // KdbPrintf("\n%Z", &KdbPromptStr); // Alternatively, use "Input> "

            // KdbPuts("\n");
            DebugIo->u.PrintString.LengthOfString = 1;
            MessageData->Length = DebugIo->u.PrintString.LengthOfString;
            MessageData->Buffer = "\n";
            KdbpPrintPacket(MessageHeader, MessageData, Context);

            /* Restore the original ApiNumber and print our prompt
             * as part of the original DbgKdGetStringApi call. */
            DebugIo->ApiNumber = ApiNumber;

            // KdbPuts(KdbPromptStr.Buffer);
            DebugIo->u.GetString.LengthOfPromptString = KdbPromptStr.Length;
            MessageData->Length = DebugIo->u.GetString.LengthOfPromptString;
            MessageData->Buffer = (PCHAR)KdbPromptStr.Buffer;
            KdbpPrintPacket(MessageHeader, MessageData, Context);

            /* And restore the original buffer that will receive the reply */
            MessageData->Buffer = Buffer;
        }

        return;
    }
    else
    /* Debugger-only packets */
    if (PacketType == PACKET_TYPE_KD_STATE_CHANGE64)
    {
        PDBGKD_ANY_WAIT_STATE_CHANGE WaitStateChange = (PDBGKD_ANY_WAIT_STATE_CHANGE)MessageHeader->Buffer;
        if (WaitStateChange->NewState == DbgKdLoadSymbolsStateChange)
        {
            /* Load or unload symbols */
            PLDR_DATA_TABLE_ENTRY LdrEntry;
            if (KdbpSymFindModule((PVOID)(ULONG_PTR)WaitStateChange->u.LoadSymbols.BaseOfDll, -1, &LdrEntry))
            {
                KdbSymProcessSymbols(LdrEntry, !WaitStateChange->u.LoadSymbols.UnloadSymbols);
            }
            return;
        }
        else if (WaitStateChange->NewState == DbgKdExceptionStateChange)
        {
            KdbgNextApiNumber = DbgKdGetContextApi;
            KdbgExceptionRecord = WaitStateChange->u.Exception.ExceptionRecord;
            KdbgFirstChanceException = WaitStateChange->u.Exception.FirstChance;
            return;
        }
    }
    else if (PacketType == PACKET_TYPE_KD_STATE_MANIPULATE)
    {
        PDBGKD_MANIPULATE_STATE64 ManipulateState = (PDBGKD_MANIPULATE_STATE64)MessageHeader->Buffer;
        if (ManipulateState->ApiNumber == DbgKdGetContextApi)
        {
            KD_CONTINUE_TYPE Result;

            /* Check if this is an assertion failure */
            if (KdbgExceptionRecord.ExceptionCode == STATUS_ASSERTION_FAILURE)
            {
                /* Bump EIP to the instruction following the int 2C */
                KeSetContextPc(&KdbgContext, KeGetContextPc(&KdbgContext) + 2);
            }

            Result = KdbEnterDebuggerException(&KdbgExceptionRecord,
                                               KdbgContext.SegCs & 1,
                                               &KdbgContext,
                                               KdbgFirstChanceException);
#if 0
            /* Manually dump the stack for the user */
            KeRosDumpStackFrames(NULL, 0);
            Result = kdHandleException;
#endif
            if (Result != kdHandleException)
                KdbgContinueStatus = STATUS_SUCCESS;
            else
                KdbgContinueStatus = STATUS_UNSUCCESSFUL;
            KdbgNextApiNumber = DbgKdSetContextApi;
            return;
        }
        else if (ManipulateState->ApiNumber == DbgKdSetContextApi)
        {
            KdbgNextApiNumber = DbgKdContinueApi;
            return;
        }
    }
    return;
}

KDSTATUS
NTAPI
KdReceivePacket(
    _In_ ULONG PacketType,
    _Out_ PSTRING MessageHeader,
    _Out_ PSTRING MessageData,
    _Out_ PULONG DataLength,
    _Inout_ PKD_CONTEXT Context)
#undef KdReceivePacket
{
    if (PacketType == PACKET_TYPE_KD_POLL_BREAKIN)
    {
        // FIXME TODO: Implement break-in for the debugger
        // and return KdPacketReceived when handled properly.
        return KdPacketTimedOut;
    }

    if (PacketType == PACKET_TYPE_KD_DEBUG_IO)
    {
        PDBGKD_DEBUG_IO DebugIo = (PDBGKD_DEBUG_IO)MessageHeader->Buffer;
        KDSTATUS Status;

        /* Validate API call */
        if (MessageHeader->MaximumLength != sizeof(DBGKD_DEBUG_IO))
            return KdPacketNeedsResend;
        if (DebugIo->ApiNumber != DbgKdGetStringApi)
            return KdPacketNeedsResend;
        if (!MessageData)
            return KdPacketNeedsResend;

        Status = KdbpPromptPacket(MessageHeader, MessageData, DataLength, Context);

        if (Status == KdPacketReceived || Status == KdPacketNeedsResend)
        {
            /* Release the terminal acquired in KdSendPacket() */
            if (&KD_TERM) KD_TERM.SetState(FALSE);
        }

        return Status;
    }
    else
    /* Debugger-only packets */
    if (PacketType == PACKET_TYPE_KD_STATE_MANIPULATE)
    {
        PDBGKD_MANIPULATE_STATE64 ManipulateState = (PDBGKD_MANIPULATE_STATE64)MessageHeader->Buffer;
        RtlZeroMemory(MessageHeader->Buffer, MessageHeader->MaximumLength);
        if (KdbgNextApiNumber == DbgKdGetContextApi)
        {
            ManipulateState->ApiNumber = DbgKdGetContextApi;
            MessageData->Length = 0;
            MessageData->Buffer = (PCHAR)&KdbgContext;
            return KdPacketReceived;
        }
        else if (KdbgNextApiNumber == DbgKdSetContextApi)
        {
            ManipulateState->ApiNumber = DbgKdSetContextApi;
            MessageData->Length = sizeof(KdbgContext);
            MessageData->Buffer = (PCHAR)&KdbgContext;
            return KdPacketReceived;
        }
        else if (KdbgNextApiNumber != DbgKdContinueApi)
        {
            KdbPrintf("%s:%d is UNIMPLEMENTED\n", __FUNCTION__, __LINE__);
        }
        ManipulateState->ApiNumber = DbgKdContinueApi;
        ManipulateState->u.Continue.ContinueStatus = KdbgContinueStatus;

        /* Prepare for next time */
        KdbgNextApiNumber = DbgKdContinueApi;
        KdbgContinueStatus = STATUS_SUCCESS;

        return KdPacketReceived;
    }

    KdbPrintf("%s: PacketType %d is UNIMPLEMENTED\n", __FUNCTION__, PacketType);
    return KdPacketTimedOut;
}

/* EOF */
