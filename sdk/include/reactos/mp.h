
#pragma once

typedef struct _APINFO
{
    KIPCR Pcr;
    KTHREAD Thread;
    KGDTENTRY Gdt[128];
    KIDTENTRY Idt[128];
    KTSS Tss;
    KTSS TssDoubleFault;
    KTSS TssNMI;
    UCHAR DECLSPEC_ALIGN(PAGE_SIZE) NMIStackData[KERNEL_STACK_SIZE]; // See DOUBLE_FAULT_STACK_SIZE
} APINFO, *PAPINFO;

/* GDT stuff */
#define TYPE_CODE   (0x10 | DESCRIPTOR_CODE | DESCRIPTOR_EXECUTE_READ)
#define TYPE_DATA   (0x10 | DESCRIPTOR_READ_WRITE)
#define DESCRIPTOR_ACCESSED     0x1
#define DESCRIPTOR_READ_WRITE   0x2
#define DESCRIPTOR_EXECUTE_READ 0x2
#define DESCRIPTOR_EXPAND_DOWN  0x4
#define DESCRIPTOR_CONFORMING   0x4
#define DESCRIPTOR_CODE         0x8

VOID
NTAPI
KxInitAPProcessorState(
    _Out_ PKPROCESSOR_STATE ProcessorState);

FORCEINLINE
VOID
KiSetGdtDescriptorBase(
    IN OUT PKGDTENTRY Entry,
    IN ULONG32 Base);

FORCEINLINE
PKGDTENTRY
KiGetGdtEntry(
    IN PVOID pGdt,
    IN USHORT Selector);

FORCEINLINE
VOID
KiSetGdtDescriptorLimit(
    IN OUT PKGDTENTRY Entry,
    IN ULONG Limit);

VOID
KiSetGdtEntryEx(
    IN OUT PKGDTENTRY Entry,
    IN ULONG32 Base,
    IN ULONG Limit,
    IN UCHAR Type,
    IN UCHAR Dpl,
    IN BOOLEAN Granularity,
    IN UCHAR SegMode);

FORCEINLINE
VOID
KiSetGdtEntry(
    IN OUT PKGDTENTRY Entry,
    IN ULONG32 Base,
    IN ULONG Limit,
    IN UCHAR Type,
    IN UCHAR Dpl,
    IN UCHAR SegMode);

/* paging stuff */
#define MM_PAGE_SIZE    4096