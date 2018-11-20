#ifndef _PNPIO_H
#define _PNPIO_H

/* Dump resources flags */
#define PIP_DUMP_FL_ALL_NODES        1
#define PIP_DUMP_FL_RES_ALLOCATED    2
#define PIP_DUMP_FL_RES_REQUIREMENTS 4
#define PIP_DUMP_FL_RES_TRANSLATED   8

/* Request types for PIP_ENUM_REQUEST */
typedef enum _PIP_ENUM_TYPE
{
    PipEnumAddBootDevices,
    PipEnumAssignResources,
    PipEnumGetSetDeviceStatus,
    PipEnumClearProblem,
    PipEnumInvalidateRelationsInList,
    PipEnumHaltDevice,
    PipEnumBootDevices,
    PipEnumDeviceOnly,
    PipEnumDeviceTree,
    PipEnumRootDevices,
    PipEnumInvalidateDeviceState,
    PipEnumResetDevice,
    PipEnumIoResourceChanged,
    PipEnumSystemHiveLimitChange,
    PipEnumSetProblem,
    PipEnumShutdownPnpDevices,
    PipEnumStartDevice,
    PipEnumStartSystemDevices
} PIP_ENUM_TYPE;

typedef struct _PIP_ENUM_REQUEST
{
    LIST_ENTRY RequestLink;
    PDEVICE_OBJECT DeviceObject;
    PIP_ENUM_TYPE RequestType;
    UCHAR ReorderingBarrier;
    UCHAR Padded[3];
    ULONG_PTR RequestArgument;
    PKEVENT CompletionEvent;
    NTSTATUS * CompletionStatus;
} PIP_ENUM_REQUEST, *PPIP_ENUM_REQUEST;

#if defined(_M_X64)
C_ASSERT(sizeof(PIP_ENUM_REQUEST) == 0x38);
#else
C_ASSERT(sizeof(PIP_ENUM_REQUEST) == 0x20);
#endif

/* debug.c */

VOID
NTAPI
PipDumpCmResourceList(
    _In_ PCM_RESOURCE_LIST CmResource,
    _In_ ULONG DebugLevel
);

PCM_PARTIAL_RESOURCE_DESCRIPTOR
NTAPI
PipGetNextCmPartialDescriptor(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor
);

VOID
NTAPI
PipDumpCmResourceDescriptor(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor,
    _In_ ULONG DebugLevel
);

VOID
NTAPI
PipDumpResourceRequirementsList(
    _In_ PIO_RESOURCE_REQUIREMENTS_LIST IoResource,
    _In_ ULONG DebugLevel
);

VOID
NTAPI
PipDumpIoResourceDescriptor(
    _In_ PIO_RESOURCE_DESCRIPTOR Descriptor,
    _In_ ULONG DebugLevel
);

VOID
NTAPI
PipDumpDeviceNodes(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ ULONG Flags,
    _In_ ULONG DebugLevel
);

/* pnpenum.c */
NTSTATUS
NTAPI
PipRequestDeviceAction(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIP_ENUM_TYPE RequestType,
    _In_ UCHAR ReorderingBarrier,
    _In_ ULONG_PTR RequestArgument,
    _In_ PKEVENT CompletionEvent,
    _Inout_ NTSTATUS * CompletionStatus
);

/* pnpnode.c */
VOID
NTAPI
PpDevNodeLockTree(
    _In_ ULONG LockLevel
);

VOID
NTAPI
PpDevNodeUnlockTree(
    _In_ ULONG LockLevel
);

#endif  /* _PNPIO_H */
