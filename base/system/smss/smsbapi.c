/*
 * PROJECT:     ReactOS NT-Compatible Session Manager
 * LICENSE:     BSD 2-Clause License (https://spdx.org/licenses/BSD-2-Clause)
 * PURPOSE:     SM Callbacks (SB) to Client Stubs
 * COPYRIGHT:   Copyright 2012 Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2022 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/* INCLUDES *******************************************************************/

#include "smss.h"

#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

#if DBG
const PCSTR SmpSubSystemNames[] =
{
    "Unknown",
    "Native",
    "Windows GUI",
    "Windows CUI",
    NULL,
    "OS/2 CUI",
    NULL,
    "Posix CUI"
};
#endif

/* FUNCTIONS ******************************************************************/

/**
 * @brief
 * Requests a subsystem to create and initialize a new environment session
 * for a process being launched.
 *
 * @param[in]   MuSessionId
 * The session ID of the Terminal Services session on which the new
 * environment subsystem is started. Specify -1 in order for the function
 * to determine it automatically, based on the given process information.
 *
 * @param[in]   ParentSubsystem
 * Optional pointer to the parent subsystem that created the new one.
 *
 * @param[in,out]   ProcessInformation
 * A process description as returned by RtlCreateUserProcess().
 *
 * @param[in]   DbgSessionId
 * @param[in]   DbgUiClientId
 * Optional session and client IDs under which the process being launched
 * is being debugged. Part of native applications debugging legacy support,
 * done with the LPC-based debugging subsystem (DbgSs) and deprecated
 * since WinXP+.
 *
 * @return
 * Success status as handed by the subsystem reply; otherwise a failure
 * status code.
 *
 * @remark
 * This function has been adapted so that it can be reused in SmpLoadSubSystem()
 * besides its initial usage in SmpExecPgm(), instead of duplicating code.
 * As such, it is closer to the Vista+ one. The one in Windows <= 2003 differs
 * in the following ways:
 * - 1st parameter: is an unused PVOID.
 * - In case no suitable subsystem is found for the image being started,
 *   and that latter one is not a native NT image, the returned Status code
 *   is STATUS_UNSUCCESSFUL, instead of STATUS_NO_SUCH_PACKAGE.
 * - On exit, the ProcessHandle and ThreadHandle members of ProcessInformation
 *   are always closed.
 **/
NTSTATUS
NTAPI
SmpSbCreateSession(
    _In_ ULONG MuSessionId,
    _In_opt_ PSMP_SUBSYSTEM ParentSubsystem,
    _Inout_ PRTL_USER_PROCESS_INFORMATION ProcessInformation,
    _In_opt_ ULONG DbgSessionId,
    _In_opt_ PCLIENT_ID DbgUiClientId)
{
    NTSTATUS Status;
    ULONG SubSystemType = ProcessInformation->ImageInformation.SubSystemType;
    ULONG SessionId;
    PSMP_SUBSYSTEM Subsystem;
    SB_API_MSG SbApiMsg = {0};
    PSB_CREATE_SESSION_MSG CreateSession = &SbApiMsg.u.CreateSession;

    /* Write out the create session message including its initial process */
    CreateSession->ProcessInfo = *ProcessInformation;
    CreateSession->DbgSessionId = DbgSessionId;
    if (DbgUiClientId)
    {
        CreateSession->DbgUiClientId = *DbgUiClientId;
    }
    else
    {
        CreateSession->DbgUiClientId.UniqueThread = NULL;
        CreateSession->DbgUiClientId.UniqueProcess = NULL;
    }

    if (MuSessionId == -1)
    {
        /* Find a subsystem responsible for this session */
        SmpGetProcessMuSessionId(ProcessInformation->ProcessHandle, &MuSessionId);
        if (!SmpCheckDuplicateMuSessionId(MuSessionId))
        {
            Status = STATUS_OBJECT_NAME_NOT_FOUND;
            DPRINT1("SMSS: CreateSession status=%x\n", Status);
            return Status;
        }
    }

    /* Find the subsystem suitable for this process */
    Subsystem = SmpLocateKnownSubSysByType(MuSessionId, SubSystemType);
    if (Subsystem)
    {
        /* Duplicate the parent process handle for the subsystem to have */
        Status = NtDuplicateObject(NtCurrentProcess(),
                                   ProcessInformation->ProcessHandle,
                                   Subsystem->ProcessHandle,
                                   &CreateSession->ProcessInfo.ProcessHandle,
                                   PROCESS_ALL_ACCESS,
                                   0,
                                   0);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SmpSbCreateSession: NtDuplicateObject (Process) Failed with Status %lx for SessionId %lu\n",
                    Status, MuSessionId);

            /* Close everything on failure */
            SmpDereferenceKnownSubSys(Subsystem);
            return Status;
        }

        /* Duplicate the initial thread handle for the subsystem to have */
        Status = NtDuplicateObject(NtCurrentProcess(),
                                   ProcessInformation->ThreadHandle,
                                   Subsystem->ProcessHandle,
                                   &CreateSession->ProcessInfo.ThreadHandle,
                                   THREAD_ALL_ACCESS,
                                   0,
                                   0);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SmpSbCreateSession: NtDuplicateObject (Thread) Failed with Status %lx for SessionId %lu\n",
                    Status, MuSessionId);

            /* Close everything on failure */
            NtDuplicateObject(Subsystem->ProcessHandle,
                              CreateSession->ProcessInfo.ProcessHandle,
                              NULL, NULL, 0, 0, DUPLICATE_CLOSE_SOURCE);
            SmpDereferenceKnownSubSys(Subsystem);
            return Status;
        }

        /* Finally, allocate a new SMSS session ID for this session */
        SessionId = SmpAllocateSessionId(Subsystem, ParentSubsystem);
        CreateSession->SessionId = SessionId;

        /* Fill out the Port Message Header */
        SbApiMsg.ApiNumber = SbpCreateSession;
        SbApiMsg.h.u2.ZeroInit = 0;
        SbApiMsg.h.u1.s1.DataLength = sizeof(*CreateSession) +
            FIELD_OFFSET(SB_API_MSG, u) - sizeof(SbApiMsg.h);
        // SbApiMsg.h.u1.s1.TotalLength = sizeof(SbApiMsg);
        SbApiMsg.h.u1.s1.TotalLength = SbApiMsg.h.u1.s1.DataLength + sizeof(SbApiMsg.h);

        /* Send the LPC message and wait for a reply */
        Status = NtRequestWaitReplyPort(Subsystem->SbApiPort,
                                        &SbApiMsg.h,
                                        &SbApiMsg.h);
        if (!NT_SUCCESS(Status))
        {
            /* Bail out */
            DPRINT1("SmpSbCreateSession: NtRequestWaitReplyPort Failed with Status %lx for SessionId %lu\n",
                    Status, MuSessionId);
        }
        else
        {
            /* If the API succeeded, get the result value from the LPC */
            Status = SbApiMsg.ReturnValue;
        }

        if (!NT_SUCCESS(Status))
        {
            /* Delete the session on any kind of failure */
            SmpDeleteSession(SessionId);

            /* And close the duplicated handles as well */
            NtDuplicateObject(Subsystem->ProcessHandle,
                              CreateSession->ProcessInfo.ThreadHandle,
                              NULL, NULL, 0, 0, DUPLICATE_CLOSE_SOURCE);
            NtDuplicateObject(Subsystem->ProcessHandle,
                              CreateSession->ProcessInfo.ProcessHandle,
                              NULL, NULL, 0, 0, DUPLICATE_CLOSE_SOURCE);
        }

        /* Dereference the subsystem and return the status of the LPC call */
        SmpDereferenceKnownSubSys(Subsystem);
        return Status;
    }

    /* If we don't yet have a subsystem, only native images can be launched */
    if (SubSystemType != IMAGE_SUBSYSTEM_NATIVE)
    {
        /* Fail */
#if DBG
        PCSTR SubSysName = NULL;
        CHAR SubSysTypeName[sizeof("Type 0x")+8];

        if (SubSystemType < RTL_NUMBER_OF(SmpSubSystemNames))
            SubSysName = SmpSubSystemNames[SubSystemType];
        if (!SubSysName)
        {
            SubSysName = SubSysTypeName;
            sprintf(SubSysTypeName, "Type 0x%08lx", SubSystemType);
        }
        DPRINT1("SMSS: %s SubSystem not found (either not started or destroyed).\n", SubSysName);
#endif
        /* Odd failure code */
        Status = STATUS_NO_SUCH_PACKAGE;
        DPRINT1("SMSS: SmpSbCreateSession - SmpLocateKnownSubSysByType Failed with Status %lx for SessionId %lu\n",
                Status, MuSessionId);
        return Status;
    }

#if 0
    /*
     * This code is part of the LPC-based legacy debugging support for native
     * applications, implemented with the debug client interface (DbgUi) and
     * debug subsystem (DbgSs). It is now vestigial since WinXP+ and is here
     * for informational purposes only.
     */
    if ((*(ULONGLONG)&CreateSession.DbgUiClientId) && SmpDbgSsLoaded)
    {
        Process = RtlAllocateHeap(SmpHeap, SmBaseTag, sizeof(SMP_PROCESS));
        if (!Process)
        {
            DPRINT1("Unable to initialize debugging for Native App %lx.%lx -- out of memory\n",
                    ProcessInformation->ClientId.UniqueProcess,
                    ProcessInformation->ClientId.UniqueThread);
            return STATUS_NO_MEMORY;
        }

        Process->DbgUiClientId = CreateSession->DbgUiClientId;
        Process->ClientId = ProcessInformation->ClientId;
        InsertHeadList(&NativeProcessList, &Process->Entry);
        DPRINT1("Native Debug App %lx.%lx\n",
                Process->ClientId.UniqueProcess,
                Process->ClientId.UniqueThread);

        Status = NtSetInformationProcess(ProcessInformation->ProcessHandle,
                                         ProcessDebugPort,
                                         &SmpDebugPort,
                                         sizeof(SmpDebugPort));
        ASSERT(NT_SUCCESS(Status));
    }
#endif

    /* This is a native application being started as the initial command */
    DPRINT("Subsystem active, starting thread\n");
    Status = NtResumeThread(ProcessInformation->ThreadHandle, NULL);
    if (!NT_SUCCESS(Status))
        DPRINT1("SMSS: Could not resume native process, Status %lx\n", Status);

    return Status;
}

/**
 * @brief
 * Requests a subsystem to create and start a process on behalf of SMSS.
 *
 * @param[in]   PortHandle
 * LPC port handle to the subsystem session callback port (SbApiPort).
 *
 * @param[in]   FileName
 * Fully qualified NT path to the executable image to load.
 *
 * @param[in]   Directory
 *
 * @param[in]   CommandLine
 * Command line to be provided to the process being started.
 *
 * @param[in]   Flags
 * A combination of SMP_*** flags that determine how to start the process.
 * See SmpExecuteImage() for more information.
 *
 * @param[out]  ProcessInformation
 * A process description as returned by RtlCreateUserProcess().
 * On success, its initialized fields are:
 * - ProcessHandle, ThreadHandle: Handles to the created process
 *   and its main thread;
 * - ClientId: The ID of the created process;
 * - ImageInformation.SubSystemType: A valid IMAGE_SUBSYSTEM_xxx value
 *   indicating which subsystem can handle this PE image.
 *
 * @return
 * Success status as handed by the subsystem reply; otherwise a failure
 * status code.
 *
 * @remark
 * This is a general internal function as present in Vista+. A reduced version
 * of this function is named SmpCallCsrCreateProcess() in Windows <= 2003.
 **/
NTSTATUS
NTAPI
SmpSbCreateProcess(
    _In_ HANDLE PortHandle,
    _In_ PUNICODE_STRING FileName,
    _In_ PUNICODE_STRING Directory,
    _In_ PUNICODE_STRING CommandLine,
    _In_ ULONG Flags,
    _Out_ PRTL_USER_PROCESS_INFORMATION ProcessInformation)
{
    NTSTATUS Status;
    SB_API_MSG SbApiMsg = {0};
    PSB_CREATE_PROCESS_MSG CreateProcess = &SbApiMsg.u.CreateProcess;

    CreateProcess->In.ImageName = FileName;
    CreateProcess->In.CurrentDirectory = Directory;
    CreateProcess->In.CommandLine = CommandLine;
    CreateProcess->In.DllPath = SmpDefaultLibPath.Length ?
                                &SmpDefaultLibPath : NULL;
    CreateProcess->In.Flags = Flags; // Windows 7+ remaps SMP_DEFERRED_FLAG (0x20) to 0x04
    CreateProcess->In.DebugFlags = SmpDebug;

    /* Fill out the Port Message Header */
    SbApiMsg.ApiNumber = SbpCreateProcess;
    SbApiMsg.h.u2.ZeroInit = 0;
    SbApiMsg.h.u1.s1.DataLength = sizeof(*CreateProcess) +
        FIELD_OFFSET(SB_API_MSG, u) - sizeof(SbApiMsg.h);
    SbApiMsg.h.u1.s1.TotalLength = SbApiMsg.h.u1.s1.DataLength + sizeof(SbApiMsg.h);

    /* Send the LPC message and wait for a reply */
    Status = NtRequestWaitReplyPort(PortHandle, &SbApiMsg.h, &SbApiMsg.h);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SmpSbCreateProcess: NtRequestWaitReplyPort failed, Status: 0x%08lx\n", Status);
    }
    else
    {
        /* Return the real status */
        Status = SbApiMsg.ReturnValue;
    }

    if (NT_SUCCESS(Status)) // Only valid when Flags & SMP_DEFERRED_FLAG == TRUE
    {
        /* Return the process information */
        ProcessInformation->ProcessHandle = CreateProcess->Out.ProcessHandle;
        ProcessInformation->ThreadHandle = CreateProcess->Out.ThreadHandle;
        ProcessInformation->ClientId = CreateProcess->Out.ClientId;
        ProcessInformation->ImageInformation.SubSystemType = CreateProcess->Out.SubsystemType;
    }

    return Status;
}
