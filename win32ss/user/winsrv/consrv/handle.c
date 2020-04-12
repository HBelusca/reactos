/*
 * LICENSE:         GPL - See COPYING in the top level directory
 * PROJECT:         ReactOS Console Server DLL
 * FILE:            win32ss/user/winsrv/consrv/handle.c
 * PURPOSE:         Console I/O Handles functions
 * PROGRAMMERS:     David Welch
 *                  Jeffrey Morlan
 *                  Hermes Belusca-Maito (hermes.belusca@sfr.fr)
 */

/* INCLUDES *******************************************************************/

#include "consrv.h"

#include <win/console.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

/* Console handle */

#if 0 // See conmsg.h
typedef enum _CONSOLE_HANDLE_TYPE
{
    HANDLE_INPUT    = 0x01,
    HANDLE_OUTPUT   = 0x02
} CONSOLE_HANDLE_TYPE;
#endif
#define CONSOLE_OBJECT_MAX (HANDLE_OUTPUT + 1)

/* Object type info structure */
typedef struct _CONSOLE_OBJECT_TYPE_INFO
{
    // CONSOLE_OBJECT_TYPE Type;
    OPEN_METHOD         OpenProcedure;
    OKAYTOCLOSE_METHOD  OkayToCloseProcedure;
    CLOSE_METHOD        CloseProcedure;
    DELETE_METHOD       DeleteProcedure;
} CONSOLE_OBJECT_TYPE_INFO, *PCONSOLE_OBJECT_TYPE_INFO;

static CONSOLE_OBJECT_TYPE_INFO ObpKnownObjectTypes[CONSOLE_OBJECT_MAX] =
{
    {0},
    {OpenInputBuffer , OkayToCloseInputBuffer , CloseInputBuffer , DeleteInputBuffer },
    {OpenScreenBuffer, OkayToCloseScreenBuffer, CloseScreenBuffer, DeleteScreenBuffer}
};

typedef struct _CONSOLE_HANDLE_ENTRY
{
    /* Type of this handle */
    union
    {
        struct
        {
            ULONG HandleType  : 8;
            ULONG Inheritable : 1;
            ULONG Lock        : 1;
            ULONG Unused      : 22;
        };
        // CONSOLE_HANDLE_TYPE Type;
        ULONG_PTR Flags;
    };
    ULONG GrantedAccess;
    // ACCESS_MASK Access;
} CONSOLE_HANDLE_ENTRY, *PCONSOLE_HANDLE_ENTRY;

// The full handle. This can be understood as a "blend" of HANDLE_TABLE_ENTRY and FILE_OBJECT
typedef struct _CONSOLE_IO_HANDLE
{
    CONSOLE_HANDLE_ENTRY;
    CONSOLE_IO_OBJECT_REFERENCE ObjectRef;
    //
    // FIXME! To consider:
    // Contrary to what I was thinking, even the Duplicate console handle
    // procedure does NOT! share the ObjectRef->Context structure.
    // This is therefore different from the FILE_OBJECT case, where duplicated
    // handles can point to the same FILE_OBJECT and therefore see the same
    // read/write state.
    // Here it's really **per-handle** context!!
    //
} CONSOLE_IO_HANDLE, *PCONSOLE_IO_HANDLE;

#define IsValidHandle(Handle) \
    ((Handle) != NULL && (Handle)->ObjectRef.Object != NULL)


/* PRIVATE FUNCTIONS **********************************************************/

static NTSTATUS
AdjustObjectHandleCounts(
    IN PCONSOLE_IO_OBJECT_REFERENCE ObjectRef,
    IN CONSOLE_HANDLE_TYPE Type,
    // IN ACCESS_MASK Access,
    // IN BOOLEAN Inheritable, // ULONG HandleAttributes
    // IN ULONG ShareMode,
    IN LONG Change)
{
    NTSTATUS Status;
    PCONSOLE_IO_OBJECT Object = ObjectRef->Object;

    DPRINT("AdjustObjectHandleCounts(0x%p, %d), Object = 0x%p\n",
           ObjectRef, Change, Object);
    DPRINT("\tAdjustObjectHandleCounts(0x%p, %d), Object = 0x%p, Object->ReferenceCount = %d, Object->Type = %lu\n",
           ObjectRef, Change, Object, Object->ReferenceCount, Object->Type);

    if (Change > 0)
    {
        // Object->Type : INPUT_BUFFER, TEXTMODE_BUFFER, GRAPHICS_BUFFER.
        if (ObpKnownObjectTypes[Type].OpenProcedure)
        {
            Status = ObpKnownObjectTypes[Type].OpenProcedure(ObjectRef,
                                                             ObjectRef->Access
                                                             /* , ObjectRef->ShareMode */);
        }
        if (!NT_SUCCESS(Status))
            return Status;

        if (ObjectRef->Access & GENERIC_READ)           Object->AccessRead += Change;
        if (ObjectRef->Access & GENERIC_WRITE)          Object->AccessWrite += Change;
        if (!(ObjectRef->ShareMode & FILE_SHARE_READ))  Object->ExclusiveRead += Change;
        if (!(ObjectRef->ShareMode & FILE_SHARE_WRITE)) Object->ExclusiveWrite += Change;
    }
    else if (Change < 0)
    {
        if (ObjectRef->Access & GENERIC_READ)           Object->AccessRead += Change;
        if (ObjectRef->Access & GENERIC_WRITE)          Object->AccessWrite += Change;
        if (!(ObjectRef->ShareMode & FILE_SHARE_READ))  Object->ExclusiveRead += Change;
        if (!(ObjectRef->ShareMode & FILE_SHARE_WRITE)) Object->ExclusiveWrite += Change;

        if (ObpKnownObjectTypes[Type].CloseProcedure)
            ObpKnownObjectTypes[Type].CloseProcedure(Object);
    }

    return STATUS_SUCCESS;
}

static VOID
ConSrvCloseHandleEntry(
    IN PCONSOLE_IO_HANDLE HandleEntry,
    IN HANDLE Handle)
{
    PCONSOLE_IO_OBJECT_REFERENCE ObjectRef = &HandleEntry->ObjectRef;
    PCONSOLE_IO_OBJECT Object = ObjectRef->Object;
    CONSOLE_HANDLE_TYPE Type;

    /* Check validity of object */
    if (Object == NULL)
        goto Quit; // STATUS_INVALID_HANDLE;

    /* Check validity of object type */
    Type = HandleEntry->HandleType;
    if (Type <= 0 || Type >= RTL_NUMBER_OF(ObpKnownObjectTypes))
        goto Quit; // STATUS_INVALID_HANDLE;

    /* Call the OkayToClose and the Close procedures ~ IRP_MJ_CLEANUP */
    if (ObpKnownObjectTypes[Type].OkayToCloseProcedure)
        /* Status = */ ObpKnownObjectTypes[Type].OkayToCloseProcedure(ObjectRef, Handle);
    // if (!NT_SUCCESS(Status)) { ... }

    /* Decrement the object's handle counts and dereference it */
    AdjustObjectHandleCounts(ObjectRef, Type, -1);
    ConSrvDereferenceObject(Object, Type);

Quit:
    /* Invalidate (zero-out) this handle entry */
    // ObjectRef->Object = NULL;
    RtlZeroMemory(HandleEntry, sizeof(*HandleEntry)); // Be sure the whole entry is invalidated.
}


NTSTATUS
ConSrvCreateHandleTable(
    IN OUT PCONSOLE_PROCESS_DATA ProcessData)
{
    ProcessData->HandleTableSize = 0;
    ProcessData->HandleTable = NULL;

    RtlInitializeCriticalSection(&ProcessData->HandleTableLock);

    return STATUS_SUCCESS;
}

VOID
ConSrvDestroyHandleTable(
    IN PCONSOLE_PROCESS_DATA ProcessData)
{
    /* Free the handle table memory */
    if (ProcessData->HandleTable != NULL)
        ConsoleFreeHeap(ProcessData->HandleTable);

    ProcessData->HandleTable = NULL;
    ProcessData->HandleTableSize = 0;

    RtlDeleteCriticalSection(&ProcessData->HandleTableLock);
}

// ExDupHandleTable // Duplicates the handle table given in parameter for the process given in parameter.
NTSTATUS
ConSrvInheritHandleTable(
    IN PCONSOLE_PROCESS_DATA SourceProcessData,
    IN PCONSOLE_PROCESS_DATA TargetProcessData)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PCONSOLE_IO_HANDLE HandleEntry;
    PCONSOLE_IO_OBJECT_REFERENCE ObjectRef;
    ULONG i, j;

    RtlEnterCriticalSection(&SourceProcessData->HandleTableLock);

    /* Inherit a handle table only if there is none already */
    if (TargetProcessData->HandleTable != NULL /* || TargetProcessData->HandleTableSize != 0 */)
    {
        Status = STATUS_UNSUCCESSFUL;
        goto Quit;
    }

    /* Allocate a new handle table for the child process */
    TargetProcessData->HandleTable = ConsoleAllocHeap(HEAP_ZERO_MEMORY,
                                                      SourceProcessData->HandleTableSize
                                                        * sizeof(CONSOLE_IO_HANDLE));
    if (TargetProcessData->HandleTable == NULL)
    {
        Status = STATUS_NO_MEMORY;
        goto Quit;
    }

    TargetProcessData->HandleTableSize = SourceProcessData->HandleTableSize;

    /*
     * Parse the parent process' handle table and, for each handle,
     * do a copy of it and reference it, if the handle is inheritable.
     */
    for (i = 0, j = 0; i < SourceProcessData->HandleTableSize; i++)
    {
        if (IsValidHandle(&SourceProcessData->HandleTable[i]) &&
            SourceProcessData->HandleTable[i].Inheritable)
        {
            /*
             * Copy the handle data and increment the reference and
             * handle counts of the pointed object.
             * // ConSrvCreateHandleEntry
             */

            HandleEntry = &TargetProcessData->HandleTable[j];
            *HandleEntry = SourceProcessData->HandleTable[i];

            ObjectRef = &HandleEntry->ObjectRef;

            /* Reference the object */
            Status = ConSrvReferenceObject(ObjectRef->Object, HandleEntry->HandleType);
            if (!NT_SUCCESS(Status))
            {
                /* Invalidate (zero-out) this handle entry */
                RtlZeroMemory(HandleEntry, sizeof(*HandleEntry)); // Be sure the whole entry is invalidated.
                continue;
            }

            /* Increment the object's handle counts */
            // Object->Type : INPUT_BUFFER, TEXTMODE_BUFFER, GRAPHICS_BUFFER.
            Status = AdjustObjectHandleCounts(ObjectRef, HandleEntry->HandleType, /* Access, Inheritable, ShareMode, */ +1);
            if (!NT_SUCCESS(Status))
            {
                /* Failed, dereference the object and bail out */
                ConSrvDereferenceObject(ObjectRef->Object, HandleEntry->HandleType);

                /* Invalidate (zero-out) this handle entry */
                RtlZeroMemory(HandleEntry, sizeof(*HandleEntry)); // Be sure the whole entry is invalidated.
                continue;
            }

            ++j;
        }
    }

Quit:
    RtlLeaveCriticalSection(&SourceProcessData->HandleTableLock);
    return Status;
}

VOID
ConSrvSweepHandleTable(
    IN PCONSOLE_PROCESS_DATA ProcessData)
{
    RtlEnterCriticalSection(&ProcessData->HandleTableLock);

    if (ProcessData->HandleTable != NULL)
    {
        /*
         * ProcessData->ConsoleHandle is NULL when ConSrvSweepHandleTable() is
         * called in ConSrvConnect() during the allocation of a new console.
         */
        if (ProcessData->ConsoleHandle != NULL)
        {
            ULONG i;

            /* Close all the console handles */
            for (i = 0; i < ProcessData->HandleTableSize; i++)
            {
                ConSrvCloseHandleEntry(&ProcessData->HandleTable[i], ULongToHandle((i << 2) | 0x3));
            }
        }
        /* Free the handle table memory */
        ConsoleFreeHeap(ProcessData->HandleTable);
        ProcessData->HandleTable = NULL;
    }

    ProcessData->HandleTableSize = 0;

    RtlLeaveCriticalSection(&ProcessData->HandleTableLock);
}



// More like ~= ObInsertObject (i.e. the object is fresh new and we insert it
// in the object manager) than ObOpenObjectByPointer (for which the object
// should already exist).
//
// Also since these are used only for IO objects, this is equivalent to
// the IRP_MJ_CREATE sent when creating a new, or opening an existing file.
// We should take the opportunity then to have a callback for that; this would
// allow us to initialize a per-handle per-object(type) handle context.
//
NTSTATUS
ConSrvOpenObjectByPointer(
    IN PCONSOLE_PROCESS_DATA ProcessData,
    IN PCONSOLE_IO_OBJECT Object,
    IN CONSOLE_HANDLE_TYPE Type,
    IN ACCESS_MASK Access,
    IN BOOLEAN Inheritable, // ULONG HandleAttributes
    IN ULONG ShareMode,
    OUT PHANDLE Handle)
{
#define IO_HANDLES_INCREMENT    (2 * 3)

    NTSTATUS Status;
    ULONG i = 0;
    PCONSOLE_IO_HANDLE Block, HandleEntry;
    PCONSOLE_IO_OBJECT_REFERENCE ObjectRef;

    ASSERT(Handle);
    ASSERT(Object);

    /* Check validity of object type */
    if (Type <= 0 || Type >= RTL_NUMBER_OF(ObpKnownObjectTypes))
        return STATUS_INVALID_PARAMETER;

    /* Reference the object */
    Status = ConSrvReferenceObject(Object, Type);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Validate access */
    if (((Access & GENERIC_READ)  && Object->ExclusiveRead  != 0) ||
        ((Access & GENERIC_WRITE) && Object->ExclusiveWrite != 0) ||
        (!(ShareMode & FILE_SHARE_READ)  && Object->AccessRead     != 0) ||
        (!(ShareMode & FILE_SHARE_WRITE) && Object->AccessWrite    != 0))
    {
        DPRINT1("Sharing violation\n");
        ConSrvDereferenceObject(Object, Type);
        return STATUS_SHARING_VIOLATION;
    }

    /*
     * Always call RtlEnterCriticalSection() on the HandleTableLock.
     * This will ensure one of these two conditions:
     *
     * 1. If the handle table is already locked by the currrent thread,
     *    nothing is effectively being done; critical sections support
     *    recursive locks.
     *
     * 2. If the handle table is not locked, or locked by another thread,
     *    lock it now (we may wait), and we will release it at the end of
     *    the function.
     */
    RtlEnterCriticalSection(&ProcessData->HandleTableLock);

    ASSERT( (ProcessData->HandleTable == NULL && ProcessData->HandleTableSize == 0) ||
            (ProcessData->HandleTable != NULL && ProcessData->HandleTableSize != 0) );

    if (ProcessData->HandleTable)
    {
        /* Find an empty handle entry */
        for (i = 0; i < ProcessData->HandleTableSize; i++)
        {
            if (!IsValidHandle(&ProcessData->HandleTable[i]))
                break;
        }
    }

    if (i >= ProcessData->HandleTableSize)
    {
        /* Allocate a new handle table */
        Block = ConsoleAllocHeap(HEAP_ZERO_MEMORY,
                                 (ProcessData->HandleTableSize +
                                    IO_HANDLES_INCREMENT) * sizeof(CONSOLE_IO_HANDLE));
        if (Block == NULL)
        {
            /* Failed, bail out */
            RtlLeaveCriticalSection(&ProcessData->HandleTableLock);
            ConSrvDereferenceObject(Object, Type);
            return STATUS_UNSUCCESSFUL;
        }

        /* If we previously had a handle table, free it and use the new one */
        if (ProcessData->HandleTable)
        {
            /* Copy the handles from the old table to the new one */
            RtlCopyMemory(Block,
                          ProcessData->HandleTable,
                          ProcessData->HandleTableSize * sizeof(CONSOLE_IO_HANDLE));
            ConsoleFreeHeap(ProcessData->HandleTable);
        }
        ProcessData->HandleTable = Block;
        ProcessData->HandleTableSize += IO_HANDLES_INCREMENT;
    }

    Status = STATUS_SUCCESS;

    HandleEntry = &ProcessData->HandleTable[i];
    ObjectRef = &HandleEntry->ObjectRef;

    /* Initialize the object reference */
    ObjectRef->Object    = Object;
    ObjectRef->Context   = NULL;
    ObjectRef->Access    = Access;
    ObjectRef->ShareMode = ShareMode;

    /* Increment the object's handle counts */
    // Object->Type : INPUT_BUFFER, TEXTMODE_BUFFER, GRAPHICS_BUFFER.
    Status = AdjustObjectHandleCounts(ObjectRef, Type, /* Access, Inheritable, ShareMode, */ +1);
    if (NT_SUCCESS(Status))
    {
        /* Initialize the handle entry */
        HandleEntry->HandleType  = Type;
        HandleEntry->Inheritable = Inheritable;
        // HandleEntry->Access      = Access;
        // HandleEntry->ShareMode   = ShareMode;
        *Handle = ULongToHandle((i << 2) | 0x3);
    }
    else
    {
        /* Failed, dereference the object and bail out */
        ConSrvDereferenceObject(Object, Type);

        /* Invalidate (zero-out) this handle entry */
        RtlZeroMemory(HandleEntry, sizeof(*HandleEntry)); // Be sure the whole entry is invalidated.
    }

    RtlLeaveCriticalSection(&ProcessData->HandleTableLock);
    return Status;
}

NTSTATUS
ConSrvOpenObjectByType(
    IN PCONSOLE_PROCESS_DATA ProcessData,
    IN PCONSRV_CONSOLE Console,
    IN CONSOLE_HANDLE_TYPE Type,
    IN CONSOLE_IO_OBJECT_TYPE IoType OPTIONAL,
    IN ACCESS_MASK Access, // DesiredAccess
    IN BOOLEAN Inheritable, // ULONG HandleAttributes
    IN ULONG ShareMode,
    OUT PHANDLE Handle)
{
    PCONSOLE_IO_OBJECT Object;

    ASSERT(Handle);

    /*
     * Open a handle to either the active screen buffer or the input buffer.
     */
    switch (Type)
    {
    case HANDLE_INPUT:
        Object = &Console->InputBuffer.Header;
        break;

    case HANDLE_OUTPUT:
        Object = &Console->ActiveBuffer->Header;
        break;

    default:
        DPRINT1("Invalid Type %lu\n", Type);
        return STATUS_INVALID_PARAMETER;
    }

    if ( /*(IoType != 0 && Object->Type != IoType)*/
         (IoType != 0 && (Object->Type & IoType) == 0) )
    {
        DPRINT("ConSrvOpenObjectByType -- Invalid object 0x%x of type %lu ; expected type %lu\n",
               Object, IoType, Object->Type);
        return STATUS_OBJECT_TYPE_MISMATCH;
    }

    return ConSrvOpenObjectByPointer(ProcessData,
                                     Object,
                                     Type,
                                     Access,
                                     Inheritable,
                                     ShareMode,
                                     Handle);
}

NTSTATUS
ConSrvDuplicateObject(
    IN PCONSOLE_PROCESS_DATA SourceProcessData,
    IN HANDLE SourceHandle,
    IN PCONSOLE_PROCESS_DATA TargetProcessData,
    OUT PHANDLE TargetHandle OPTIONAL,
    IN ACCESS_MASK DesiredAccess,
    IN BOOLEAN Inheritable, // ULONG HandleAttributes
    IN ULONG Options)
{
    NTSTATUS Status;
    PCONSOLE_PROCESS_DATA ProcessData = SourceProcessData;
    ULONG Index = HandleToULong(SourceHandle) >> 2;
    PCONSOLE_IO_HANDLE Entry;

    /*
     * We can duplicate console handles only if both the source
     * and the target processes are in fact the current process.
     */
    if (SourceProcessData != TargetProcessData)
        return STATUS_INVALID_PARAMETER; // STATUS_ACCESS_DENIED;

    RtlEnterCriticalSection(&ProcessData->HandleTableLock);

    // ASSERT( (ProcessData->HandleTable == NULL && ProcessData->HandleTableSize == 0) ||
    //         (ProcessData->HandleTable != NULL && ProcessData->HandleTableSize != 0) );

    if ( /** !IsConsoleHandle(SourceHandle)   || **/
        Index >= ProcessData->HandleTableSize ||
        (Entry = &ProcessData->HandleTable[Index], !IsValidHandle(Entry)) )
    {
        DPRINT1("Couldn't duplicate invalid handle 0x%p\n", SourceHandle);
        Status = STATUS_INVALID_HANDLE;
        goto Quit;
    }

    if (Options & DUPLICATE_SAME_ACCESS)
    {
        DesiredAccess = Entry->ObjectRef.Access;
    }
    else
    {
        /* Make sure the source handle has all the desired flags */
        if ((Entry->ObjectRef.Access & DesiredAccess) == 0)
        {
            DPRINT1("Handle 0x%p has only access %X; requested %X\n",
                    SourceHandle, Entry->ObjectRef.Access, DesiredAccess);
            Status = STATUS_INVALID_PARAMETER; // STATUS_ACCESS_DENIED;
            goto Quit;
        }
    }

#if 0
    if (Options & DUPLICATE_SAME_ATTRIBUTES)
    {
        Inheritable = Entry->Inheritable;
    }
#endif

    /* Insert the new handle inside the process handle table */
    Status = ConSrvOpenObjectByPointer(ProcessData,
                                       Entry->ObjectRef.Object,
                                       Entry->HandleType,
                                       DesiredAccess,
                                       Inheritable,
                                       Entry->ObjectRef.ShareMode,
                                       TargetHandle);

    /* Always close the source handle if requested */
    if (Options & DUPLICATE_CLOSE_SOURCE)
        ConSrvCloseHandleEntry(Entry, SourceHandle);

Quit:
    RtlLeaveCriticalSection(&ProcessData->HandleTableLock);

    return Status;
}


// ~= ObCloseHandle
NTSTATUS
ConSrvCloseHandle(
    IN PCONSOLE_PROCESS_DATA ProcessData,
    IN HANDLE Handle)
{
    ULONG Index = HandleToULong(Handle) >> 2;

    RtlEnterCriticalSection(&ProcessData->HandleTableLock);

    ASSERT(ProcessData->HandleTable);
    // ASSERT( (ProcessData->HandleTable == NULL && ProcessData->HandleTableSize == 0) ||
    //         (ProcessData->HandleTable != NULL && ProcessData->HandleTableSize != 0) );

    if (Index >= ProcessData->HandleTableSize ||
        !IsValidHandle(&ProcessData->HandleTable[Index]))
    {
        RtlLeaveCriticalSection(&ProcessData->HandleTableLock);
        return STATUS_INVALID_HANDLE;
    }

    ASSERT(ProcessData->ConsoleHandle);
    ConSrvCloseHandleEntry(&ProcessData->HandleTable[Index], Handle);

    RtlLeaveCriticalSection(&ProcessData->HandleTableLock);
    return STATUS_SUCCESS;
}

NTSTATUS
ConSrvReferenceObject(
    IN PCONSOLE_IO_OBJECT Object,
    /****/IN CONSOLE_HANDLE_TYPE Type/****/)
{
    PCONSRV_CONSOLE ObjectConsole;

    ASSERT(Object);

    /* Reference its console as well */
    ObjectConsole = (PCONSRV_CONSOLE)Object->Console;
    if (ConDrvValidateConsoleUnsafe((PCONSOLE)ObjectConsole, CONSOLE_RUNNING, FALSE))
    {
        _InterlockedIncrement(&ObjectConsole->ReferenceCount);
    }
    else
    {
        return STATUS_INVALID_PARAMETER; // STATUS_INVALID_ADDRESS;
    }

    /* Reference the object */
    _InterlockedIncrement(&Object->ReferenceCount);
    return STATUS_SUCCESS;
}

NTSTATUS
ConSrvReferenceObjectByPointer(
    IN PCONSOLE_IO_OBJECT Object,
    IN ACCESS_MASK Access,
    /****/IN CONSOLE_HANDLE_TYPE Type,/****/
    IN CONSOLE_IO_OBJECT_TYPE IoType OPTIONAL)
{
    ASSERT(Object);

    /* NOTE: Access check is ignored since we don't use a handle. */

    /* Check object type */
    if ( /*(IoType != 0 && Object->Type != IoType)*/
         (IoType != 0 && (Object->Type & IoType) == 0) )
    {
        DPRINT("ConSrvReferenceObjectByPointer -- Invalid object 0x%x of type %lu ; expected type %lu\n",
               Object, IoType, Object->Type);
        return STATUS_OBJECT_TYPE_MISMATCH;
    }

    return ConSrvReferenceObject(Object, Type);
}

NTSTATUS
ConSrvReferenceObjectByHandle(
    IN PCONSOLE_PROCESS_DATA ProcessData,
    IN HANDLE Handle,
    IN ACCESS_MASK Access,
    IN CONSOLE_HANDLE_TYPE Type OPTIONAL,
    IN CONSOLE_IO_OBJECT_TYPE IoType OPTIONAL,
    OUT PCONSOLE_IO_OBJECT* Object,
    OUT PCONSOLE_IO_OBJECT_REFERENCE* Entry OPTIONAL    // BOF BOF !!!! Not PCONSOLE_IO_HANDLE*
    )
{
    NTSTATUS Status;
    ULONG Index = HandleToULong(Handle) >> 2;
    PCONSOLE_IO_HANDLE HandleEntry = NULL;
    PCONSOLE_IO_OBJECT_REFERENCE ObjectRef = NULL;
    PCONSOLE_IO_OBJECT ObjectEntry = NULL;

    ASSERT(Object);
    if (Entry) *Entry = NULL;

    RtlEnterCriticalSection(&ProcessData->HandleTableLock);

    if ( IsConsoleHandle(Handle) &&
         Index < ProcessData->HandleTableSize )
    {
        HandleEntry = &ProcessData->HandleTable[Index];
        ObjectRef   = &HandleEntry->ObjectRef;
        ObjectEntry = ObjectRef->Object;
    }

    /* Check handle validity */
    if (HandleEntry == NULL || ObjectEntry == NULL)
    {
        DPRINT("ConSrvReferenceObjectByHandle -- Invalid handle 0x%x of type %lu ; retrieved object 0x%x (handle 0x%x)\n",
               Handle, Type, ObjectEntry, HandleEntry);
        RtlLeaveCriticalSection(&ProcessData->HandleTableLock);
        return STATUS_INVALID_HANDLE;
    }

    /* Check handle type */
    if (Type != 0 && HandleEntry->HandleType != Type)
    {
        DPRINT("ConSrvReferenceObjectByHandle -- Invalid handle 0x%x of type %lu with access %lu ; retrieved object 0x%x (handle 0x%x) of type %lu with access %lu\n",
               Handle, Type, Access, ObjectEntry, HandleEntry, (ObjectEntry ? ObjectEntry->Type : 0), (HandleEntry ? ObjectRef->Access : 0));
        RtlLeaveCriticalSection(&ProcessData->HandleTableLock);
        return STATUS_INVALID_HANDLE; // STATUS_OBJECT_TYPE_MISMATCH;
    }

    /* Check handle access */
    if ((ObjectRef->Access & Access) == 0)
    {
        DPRINT("ConSrvReferenceObjectByHandle -- Invalid handle 0x%x of type %lu with access %lu ; retrieved object 0x%x (handle 0x%x) of type %lu with access %lu\n",
               Handle, Type, Access, ObjectEntry, HandleEntry, ObjectEntry->Type, ObjectRef->Access);
        RtlLeaveCriticalSection(&ProcessData->HandleTableLock);
        return STATUS_ACCESS_DENIED;
    }

    RtlLeaveCriticalSection(&ProcessData->HandleTableLock);

    Status = ConSrvReferenceObjectByPointer(ObjectEntry, Access, HandleEntry->HandleType, IoType);
    if (Status == STATUS_SUCCESS)
    {
        /* Return the objects to the caller */
        *Object = ObjectEntry;
        if (Entry) *Entry = ObjectRef;
    }
    else if (Status != STATUS_OBJECT_TYPE_MISMATCH)
    {
        Status = STATUS_INVALID_HANDLE;
    }
    return Status;
}

VOID
ConSrvDereferenceObject(
    IN PCONSOLE_IO_OBJECT Object,
    /****/IN CONSOLE_HANDLE_TYPE Type/****/)
{
    PCONSRV_CONSOLE ObjectConsole;

    ASSERT(Object);

    ObjectConsole = (PCONSRV_CONSOLE)Object->Console;

    /* Dereference the object and delete it if this is its last reference */
    if (_InterlockedDecrement(&Object->ReferenceCount) <= 0)
    {
        /* Call the Delete procedure ~ IRP_MJ_CLOSE */
        if (ObpKnownObjectTypes[Type].DeleteProcedure)
            ObpKnownObjectTypes[Type].DeleteProcedure(Object);
    }

    /* Dereference the console as well */
    ConSrvReleaseConsole(ObjectConsole, FALSE);
}


/* PUBLIC SERVER APIS *********************************************************/

/* API_NUMBER: ConsolepOpenConsole */
CON_API(SrvOpenConsole,
        CONSOLE_OPENCONSOLE, OpenConsoleRequest)
{
    /*
     * This API opens a handle to either the input buffer or to
     * a screen-buffer of the console of the current process.
     */

    OpenConsoleRequest->Handle = INVALID_HANDLE_VALUE;

    /* Open a handle to either the input buffer or the active screen buffer */
    return ConSrvOpenObjectByType(ProcessData,
                                  Console,
                                  OpenConsoleRequest->HandleType,
                                  0,
                                  OpenConsoleRequest->DesiredAccess,
                                  OpenConsoleRequest->InheritHandle,
                                  OpenConsoleRequest->ShareMode,
                                  &OpenConsoleRequest->Handle);
}

/* API_NUMBER: ConsolepDuplicateHandle */
CON_API(SrvDuplicateHandle,
        CONSOLE_DUPLICATEHANDLE, DuplicateHandleRequest)
{
    DuplicateHandleRequest->TargetHandle = INVALID_HANDLE_VALUE;

    return ConSrvDuplicateObject(ProcessData,
                                 DuplicateHandleRequest->SourceHandle,
                                 ProcessData,
                                 &DuplicateHandleRequest->TargetHandle,
                                 DuplicateHandleRequest->DesiredAccess,
                                 DuplicateHandleRequest->InheritHandle,
                                 DuplicateHandleRequest->Options);
}

/* API_NUMBER: ConsolepGetHandleInformation */
CON_API(SrvGetHandleInformation,
        CONSOLE_GETHANDLEINFO, GetHandleInfoRequest)
{
    NTSTATUS Status;
    HANDLE Handle = GetHandleInfoRequest->Handle;
    ULONG Index = HandleToULong(Handle) >> 2;
    PCONSOLE_IO_HANDLE Entry;

    RtlEnterCriticalSection(&ProcessData->HandleTableLock);

    ASSERT(ProcessData->HandleTable);
    // ASSERT( (ProcessData->HandleTable == NULL && ProcessData->HandleTableSize == 0) ||
    //         (ProcessData->HandleTable != NULL && ProcessData->HandleTableSize != 0) );

    if ( !IsConsoleHandle(Handle)              ||
         Index >= ProcessData->HandleTableSize ||
         (Entry = &ProcessData->HandleTable[Index], !IsValidHandle(Entry)) )
    {
        Status = STATUS_INVALID_HANDLE;
        goto Quit;
    }

    /*
     * Retrieve the handle information flags. The console server
     * doesn't support HANDLE_FLAG_PROTECT_FROM_CLOSE.
     */
    GetHandleInfoRequest->Flags = 0;
    if (Entry->Inheritable) GetHandleInfoRequest->Flags |= HANDLE_FLAG_INHERIT;

    Status = STATUS_SUCCESS;

Quit:
    RtlLeaveCriticalSection(&ProcessData->HandleTableLock);

    return Status;
}

/* API_NUMBER: ConsolepSetHandleInformation */
CON_API(SrvSetHandleInformation,
        CONSOLE_SETHANDLEINFO, SetHandleInfoRequest)
{
    NTSTATUS Status;
    HANDLE Handle = SetHandleInfoRequest->Handle;
    ULONG Index = HandleToULong(Handle) >> 2;
    PCONSOLE_IO_HANDLE Entry;

    RtlEnterCriticalSection(&ProcessData->HandleTableLock);

    ASSERT(ProcessData->HandleTable);
    // ASSERT( (ProcessData->HandleTable == NULL && ProcessData->HandleTableSize == 0) ||
    //         (ProcessData->HandleTable != NULL && ProcessData->HandleTableSize != 0) );

    if ( !IsConsoleHandle(Handle)              ||
         Index >= ProcessData->HandleTableSize ||
         (Entry = &ProcessData->HandleTable[Index], !IsValidHandle(Entry)) )
    {
        Status = STATUS_INVALID_HANDLE;
        goto Quit;
    }

    /*
     * Modify the handle information flags. The console server
     * doesn't support HANDLE_FLAG_PROTECT_FROM_CLOSE.
     */
    if (SetHandleInfoRequest->Mask & HANDLE_FLAG_INHERIT)
    {
        Entry->Inheritable = ((SetHandleInfoRequest->Flags & HANDLE_FLAG_INHERIT) != 0);
    }

    Status = STATUS_SUCCESS;

Quit:
    RtlLeaveCriticalSection(&ProcessData->HandleTableLock);

    return Status;
}

/* API_NUMBER: ConsolepCloseHandle */
CON_API(SrvCloseHandle,
        CONSOLE_CLOSEHANDLE, CloseHandleRequest)
{
    return ConSrvCloseHandle(ProcessData, CloseHandleRequest->Handle);
}

/* API_NUMBER: ConsolepVerifyIoHandle */
CON_API(SrvVerifyConsoleIoHandle,
        CONSOLE_VERIFYHANDLE, VerifyHandleRequest)
{
    HANDLE IoHandle = VerifyHandleRequest->Handle;
    ULONG Index = HandleToULong(IoHandle) >> 2;

    VerifyHandleRequest->IsValid = FALSE;

    RtlEnterCriticalSection(&ProcessData->HandleTableLock);

    // ASSERT( (ProcessData->HandleTable == NULL && ProcessData->HandleTableSize == 0) ||
    //         (ProcessData->HandleTable != NULL && ProcessData->HandleTableSize != 0) );

    if ( !IsConsoleHandle(IoHandle)            ||
         Index >= ProcessData->HandleTableSize ||
         !IsValidHandle(&ProcessData->HandleTable[Index]) )
    {
        DPRINT("SrvVerifyConsoleIoHandle failed\n");
    }
    else
    {
        VerifyHandleRequest->IsValid = TRUE;
    }

    RtlLeaveCriticalSection(&ProcessData->HandleTableLock);

    return STATUS_SUCCESS;
}

/* EOF */
