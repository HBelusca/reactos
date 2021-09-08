#pragma once

typedef struct _USER_REFERENCE_ENTRY
{
   SINGLE_LIST_ENTRY Entry;
   PVOID obj;
} USER_REFERENCE_ENTRY, *PUSER_REFERENCE_ENTRY;

typedef struct _HANDLEENTRY USER_HANDLE_ENTRY, *PUSER_HANDLE_ENTRY;

typedef struct _USER_HANDLE_TABLE
{
    /* Pointers to shared data */
    PUSER_HANDLE_ENTRY* handles;  /* Pointer to gSharedInfo.aheList, the handle table */
    PULONG_PTR allocated_handles; /* Pointer to SERVERINFO::cHandleEntries, the total
                                   * number of allocated handle entries in the table;
                                   * also related to SERVERINFO::cbHandleTable */

    /* Private information */
    PUSER_HANDLE_ENTRY freelist;  /* Linked list of free handle entries in the region below nb_handles */
    ULONG_PTR nb_handles;         /* Start index of the continuous free handle entries region
                                   * [nb_handles; *allocated_handles] */
    ULONG_PTR nb_used_handles;    /* (STATISTICS ONLY) Number of used (non-free) handles */
} USER_HANDLE_TABLE, *PUSER_HANDLE_TABLE;

extern USER_HANDLE_TABLE gHandleTable;

VOID FASTCALL UserReferenceObject(PVOID obj);
PVOID FASTCALL UserReferenceObjectByHandle(HANDLE handle, HANDLE_TYPE type);
BOOL FASTCALL UserDereferenceObject(PVOID obj);
PVOID FASTCALL UserCreateObject(PUSER_HANDLE_TABLE ht, struct _DESKTOP* pDesktop, PTHREADINFO pti, HANDLE* h,HANDLE_TYPE type , ULONG size);
BOOL FASTCALL UserDeleteObject(HANDLE h, HANDLE_TYPE type );
PVOID UserGetObject(PUSER_HANDLE_TABLE ht, HANDLE handle, HANDLE_TYPE type );
PVOID UserGetObjectNoErr(PUSER_HANDLE_TABLE, HANDLE, HANDLE_TYPE);
BOOL FASTCALL UserCreateHandleTable(VOID);
BOOL FASTCALL UserObjectInDestroy(HANDLE);
void DbgUserDumpHandleTable();
PVOID FASTCALL ValidateHandle(HANDLE handle, HANDLE_TYPE type);
BOOLEAN UserDestroyObjectsForOwner(PUSER_HANDLE_TABLE Table, PVOID Owner);
BOOL FASTCALL UserMarkObjectDestroy(PVOID);
PVOID FASTCALL UserAssignmentLock(PVOID *ppvObj, PVOID pvNew);
PVOID FASTCALL UserAssignmentUnlock(PVOID *ppvObj);

static __inline VOID
UserRefObjectCo(PVOID obj, PUSER_REFERENCE_ENTRY UserReferenceEntry)
{
    PTHREADINFO W32Thread;

    W32Thread = PsGetCurrentThreadWin32Thread();
    ASSERT(W32Thread != NULL);
    ASSERT(UserReferenceEntry != NULL);
    UserReferenceEntry->obj = obj;
    UserReferenceObject(obj);
    PushEntryList(&W32Thread->ReferencesList, &UserReferenceEntry->Entry);
}

static __inline VOID
UserDerefObjectCo(PVOID obj)
{
    PTHREADINFO W32Thread;
    PSINGLE_LIST_ENTRY ReferenceEntry;
    PUSER_REFERENCE_ENTRY UserReferenceEntry;

    ASSERT(obj != NULL);
    W32Thread = PsGetCurrentThreadWin32Thread();
    ASSERT(W32Thread != NULL);
    ReferenceEntry = PopEntryList(&W32Thread->ReferencesList);
    ASSERT(ReferenceEntry != NULL);
    UserReferenceEntry = CONTAINING_RECORD(ReferenceEntry, USER_REFERENCE_ENTRY, Entry);
    ASSERT(UserReferenceEntry != NULL);

    ASSERT(obj == UserReferenceEntry->obj);
    UserDereferenceObject(obj);
}

void FreeProcMarkObject(_In_ PVOID Object);

/* EOF */
