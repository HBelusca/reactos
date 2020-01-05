
#if 0

#define PM_CREATE_CONSOLE     (WM_APP + 1)
#define PM_DESTROY_CONSOLE    (WM_APP + 2)

/*
 * Functions whose implementation is frontend-dependent.
 */
BOOLEAN NTAPI
ConsoleCreate(
    IN PVOID Param1,
    IN PVOID Param2)
{
    return TRUE;
}

BOOLEAN NTAPI
ConsoleDestroy(
    IN PVOID Param1,
    IN PVOID Param2)
{
    return TRUE;
}

static ULONG NTAPI
ConsoleInputThreadTemplate(PVOID Param)
{
    LONG ConsoleCount = 0;
    MSG Msg;

    while (GetMessageW(&Msg, NULL, 0, 0))
    {
        switch (Msg.message)
        {
            case PM_CREATE_CONSOLE:
            {
                LONG OrgConsoleCount;
                DPRINT("PM_CREATE_CONSOLE -- creating console\n");

                OrgConsoleCount = InterlockedExchangeAdd(&ConsoleCount, +1);
                if (ConsoleCount == 0)
                {
                    /* The console count overflowed, restore it and fail */
                    DPRINT1("CONSRV: Console count overflowed!\n");
                    InterlockedExchange(&ConsoleCount, OrgConsoleCount);
                    continue;
                }

                if (!ConsoleCreate((PVOID)Msg.wParam, (PVOID)Msg.lParam))
                {
                    /* We failed, decrement the console count */
                    DPRINT1("Failed to create a new console\n");
                    InterlockedDecrement(&ConsoleCount);
                }

                continue;
            }

            case PM_DESTROY_CONSOLE:
            {
                DPRINT("PM_DESTROY_CONSOLE -- destroying console\n");

                if (!ConsoleDestroy((PVOID)Msg.wParam, (PVOID)Msg.lParam))
                {
                    DPRINT1("Failed to destroy a console\n");
                    continue;
                }

                if (InterlockedDecrement(&ConsoleCount) == 0)
                {
                    DPRINT("CONSRV: Going to quit the Input Thread 0x%p\n", InputThreadId);
                    goto Quit;
                }

                continue;
            }
        }

        TranslateMessage(&Msg);
        DispatchMessageW(&Msg);
    }

Quit:
    return 0;
}

#endif


typedef struct _CONSOLE_INPUT_THREAD_INFO_EX
{
    CONSOLE_INPUT_THREAD_INFO ThreadInfo;
    PCONSOLE_THREAD ConsoleThread;
    PVOID Context;
    HANDLE StartupEvent;
    BOOLEAN CreateUniqueThreadPerDesktop;
} CONSOLE_INPUT_THREAD_INFO_EX, *PCONSOLE_INPUT_THREAD_INFO_EX;

static ULONG NTAPI
ConsoleInputThread(PVOID Param)
{
    NTSTATUS Status;
    ULONG_PTR InputThreadId = HandleToUlong(NtCurrentTeb()->ClientId.UniqueThread);
    CONSOLE_INPUT_THREAD_INFO_EX ThreadInfoEx;
    DESKTOP_CONSOLE_THREAD DesktopConsoleThreadInfo;
    PCSR_THREAD pcsrt = NULL;
    HANDLE hThread = NULL;

    /*
     * Capture the thread info parameter, as its pointer will become
     * invalid after the NtSetEvent() call, see below.
     */
    ThreadInfoEx = *(PCONSOLE_INPUT_THREAD_INFO_EX)Param;

    /*
     * This thread dispatches all the console notifications to the
     * notification window. It is common for all the console windows
     * within a given desktop in a window station.
     */
    if (ThreadInfoEx.CreateUniqueThreadPerDesktop)
    {
        /* Assign this console input thread to this desktop */
        DesktopConsoleThreadInfo.DesktopHandle = ThreadInfoEx.ThreadInfo.Desktop; // Duplicated desktop handle
        DesktopConsoleThreadInfo.ThreadId = InputThreadId;
        Status = NtUserConsoleControl(ConsoleCtrlDesktopConsoleThread,
                                      &DesktopConsoleThreadInfo,
                                      sizeof(DesktopConsoleThreadInfo));
        if (!NT_SUCCESS(Status)) goto Quit;
    }

    /* Default to failure */
    Status = STATUS_UNSUCCESSFUL;

    /* Connect this CSR thread to the USER subsystem */
    pcsrt = CsrConnectToUser();
    if (pcsrt == NULL) goto Quit;
    hThread = pcsrt->ThreadHandle;

    /* Assign the desktop to this thread */
    if (!SetThreadDesktop(DesktopConsoleThreadInfo.DesktopHandle)) goto Quit;

    /* The thread has been initialized, set the event */
    NtSetEvent(ThreadInfoEx.StartupEvent, NULL);
    Status = STATUS_SUCCESS;

    /*
     * WARNING!! The Param pointer may now become invalid!!
     */

    /* Run the console input thread */
    ThreadInfoEx.ConsoleThread(ThreadInfoEx.Context);

Quit:
    DPRINT("CONSRV: Quit the Input Thread 0x%p, Status = 0x%08lx\n", InputThreadId, Status);

    if (ThreadInfoEx.CreateUniqueThreadPerDesktop)
    {
        /* Remove this console input thread from this desktop */
        // DesktopConsoleThreadInfo.DesktopHandle;
        DesktopConsoleThreadInfo.ThreadId = 0;
        NtUserConsoleControl(ConsoleCtrlDesktopConsoleThread,
                             &DesktopConsoleThreadInfo,
                             sizeof(DesktopConsoleThreadInfo));
    }

    /* Close the duplicated desktop handle */
    CloseDesktop(DesktopConsoleThreadInfo.DesktopHandle); // NtUserCloseDesktop

    /* Cleanup CSR thread */
    if (pcsrt)
    {
        if (hThread != pcsrt->ThreadHandle)
            DPRINT1("WARNING!! hThread (0x%p) != pcsrt->ThreadHandle (0x%p), you may expect crashes soon!!\n", hThread, pcsrt->ThreadHandle);

        CsrDereferenceThread(pcsrt);
    }

    /* Exit the thread */
    RtlExitUserThread(Status);
    return 0;
}

NTSTATUS
StartConsoleInputThread(
    IN HANDLE ConsoleLeaderProcessHandle,
    IN PUNICODE_STRING Desktop,
    IN BOOLEAN CreateUniqueThreadPerDesktop,
    IN PCONSOLE_THREAD ConsoleThread,
    IN PVOID Context,
    OUT PCONSOLE_INPUT_THREAD_INFO ThreadInfo)
{
    NTSTATUS Status;
    UNICODE_STRING DesktopPath;
    DESKTOP_CONSOLE_THREAD DesktopConsoleThreadInfo;
    CONSOLE_INPUT_THREAD_INFO_EX ThreadInfoEx;
    HWINSTA hWinSta;
    HDESK hDesk;
    HANDLE hInputThread;
    CLIENT_ID ClientId;

    /*
     * Set-up the console input thread. We have
     * one console input thread per desktop.
     */

    if (!CsrImpersonateClient(NULL))
        return STATUS_BAD_IMPERSONATION_LEVEL;

    if (Desktop && Desktop->Buffer && Desktop->MaximumLength)
    {
        DesktopPath.MaximumLength = Desktop->MaximumLength;
        DesktopPath.Length = DesktopPath.MaximumLength - sizeof(UNICODE_NULL);
        DesktopPath.Buffer = Desktop->Buffer;
    }
    else
    {
        RtlInitUnicodeString(&DesktopPath, L"Default");
    }

    hDesk = NtUserResolveDesktop(ConsoleLeaderProcessHandle,
                                 &DesktopPath,
                                 FALSE,
                                 &hWinSta);
    DPRINT("NtUserResolveDesktop(DesktopPath = '%wZ') returned hDesk = 0x%p; hWinSta = 0x%p\n",
           &DesktopPath, hDesk, hWinSta);

    CsrRevertToSelf();

    if (hDesk == NULL)
        return STATUS_UNSUCCESSFUL;

    if (CreateUniqueThreadPerDesktop)
    {
        /*
         * We need to see whether we need to create a
         * new console input thread for this desktop.
         */
        DesktopConsoleThreadInfo.DesktopHandle = hDesk;
        // Set the special value to say we just want to retrieve the thread ID.
        DesktopConsoleThreadInfo.ThreadId = (ULONG_PTR)INVALID_HANDLE_VALUE;
        Status = NtUserConsoleControl(ConsoleCtrlDesktopConsoleThread,
                                      &DesktopConsoleThreadInfo,
                                      sizeof(DesktopConsoleThreadInfo));
        DPRINT("NtUserConsoleControl returned ThreadId = 0x%p, Status = 0x%08lx\n",
               DesktopConsoleThreadInfo.ThreadId, Status);
        if (!NT_SUCCESS(Status))
            goto Quit;
    }

    ThreadInfoEx.CreateUniqueThreadPerDesktop = CreateUniqueThreadPerDesktop;
    ThreadInfoEx.ConsoleThread = ConsoleThread;
    ThreadInfoEx.Context = Context;

    /*
     * Save the opened window station and desktop handles in the initialization
     * structure. They will be used later on, and released by the caller.
     */
    ThreadInfoEx.ThreadInfo.WinSta  = hWinSta;
    ThreadInfoEx.ThreadInfo.Desktop = hDesk;

    /* Here ThreadInfo contains original handles */

    if (CreateUniqueThreadPerDesktop)
    {
        /* If we already have a console input thread on this desktop... */
        if (DesktopConsoleThreadInfo.ThreadId != 0)
        {
            /* ... just use it... */
            DPRINT("Using input thread ThreadId = 0x%p\n", DesktopConsoleThreadInfo.ThreadId);
            ThreadInfo->ThreadId = DesktopConsoleThreadInfo.ThreadId;
            ThreadInfo->WinSta   = hWinSta;
            ThreadInfo->Desktop  = hDesk;
            Status = STATUS_SUCCESS;
            goto Quit;
        }
    }

    /* ... otherwise create a new one. */

    /* Initialize a startup event for the thread to signal it */
    Status = NtCreateEvent(&ThreadInfoEx.StartupEvent, EVENT_ALL_ACCESS,
                           NULL, SynchronizationEvent, FALSE);
    if (!NT_SUCCESS(Status))
        goto Quit;

    /*
     * Duplicate the desktop handle for the console input thread internal needs.
     * If it happens to need also a window station handle in the future, then
     * it is there that you also need to duplicate the window station handle!
     *
     * Note also that we are going to temporarily overwrite the stored handles
     * in ThreadInfo because it happens that we use also this structure to give
     * the duplicated handles to the input thread that is going to initialize.
     * After the input thread finishes its initialization, we restore the handles
     * in ThreadInfo to their old values.
     */
    Status = NtDuplicateObject(NtCurrentProcess(),
                               hDesk,
                               NtCurrentProcess(),
                               (PHANDLE)&ThreadInfoEx.ThreadInfo.Desktop,
                               0, 0, DUPLICATE_SAME_ACCESS);
    if (!NT_SUCCESS(Status))
    {
        /* Close the startup event and bail out */
        NtClose(ThreadInfoEx.StartupEvent);
        goto Quit;
    }

    /* Here ThreadInfo contains duplicated handles */

    hInputThread = NULL;
    Status = RtlCreateUserThread(NtCurrentProcess(),
                                 NULL,
                                 TRUE, // Start the thread in suspended state.
                                 0,
                                 0,
                                 0,
                                 ConsoleInputThread,
                                 &ThreadInfoEx,
                                 &hInputThread,
                                 &ClientId);
    if (NT_SUCCESS(Status))
    {
        /* Add it as a static server thread and resume it */
        ASSERT(hInputThread);
        CsrAddStaticServerThread(hInputThread, &ClientId, 0);
        Status = NtResumeThread(hInputThread, NULL);
    }
    DPRINT("Thread creation hInputThread = 0x%p, ThreadId = 0x%p, Status = 0x%08lx\n",
           hInputThread, ClientId.UniqueThread, Status);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("CONSRV: Failed to create the console input thread.\n");

        /* Close the thread's handle */
        if (hInputThread) NtClose(hInputThread);

        /* We need to close here the duplicated desktop handle */
        CloseDesktop(ThreadInfoEx.ThreadInfo.Desktop); // NtUserCloseDesktop

        /* Close the startup event and bail out */
        NtClose(ThreadInfoEx.StartupEvent);
        goto Quit;
    }

    /* No need to close hInputThread, this is done by CSR automatically */

    /* Wait for the thread to finish its initialization, and close the startup event */
    NtWaitForSingleObject(ThreadInfoEx.StartupEvent, FALSE, NULL);
    NtClose(ThreadInfoEx.StartupEvent);

    /*
     * Save the input thread ID for later use, and restore the original handles.
     * The copies are held by the console input thread.
     */
    ThreadInfo->ThreadId = (ULONG_PTR)ClientId.UniqueThread;
    ThreadInfo->WinSta   = hWinSta;
    ThreadInfo->Desktop  = hDesk;

    /* Here ThreadInfo contains again original handles */

    Status = STATUS_SUCCESS;

Quit:
    if (!NT_SUCCESS(Status))
    {
        /*
         * Close the original handles. Do not use the copies in ThreadInfo
         * because we may have failed in the middle of the duplicate operation
         * and the handles stored in ThreadInfo may have changed.
         */
        CloseDesktop(hDesk); // NtUserCloseDesktop
        CloseWindowStation(hWinSta); // NtUserCloseWindowStation
    }

    return Status;
}
