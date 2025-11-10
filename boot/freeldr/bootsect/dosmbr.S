/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Bootsector for ISO file system
 * PURPOSE:         Normal DOS boot sector
 *                  Ported to NASM from FreeDOS fdisk 1.2.0 by:
 * PROGRAMMERS:     Casper Hornstrup (chorns@users.sourceforge.net)
 */

#include <asm.inc>

.code16

//-----------------------------------------------------------------------
//   ENTRY (copied from freedos bootsector)
//
// CS:IP = 0:7C00
// IN : DL = boot drive
// OUT: DL = boot drive
//
//-----------------------------------------------------------------------

// ORG HEX(7c00)
// ORG HEX(0600)

start:
    cli
    /* Setup segment registers */
ASSUME CS:.text, DS:.text, ES:.text, SS:.text
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax           // Initialize stack at SS:SP = 0:7BE0,
    mov sp, HEX(7c00)-32 // right below original boot code with 0x20 bytes of safety margin
/*
    If bp contains the original start IP (determined at runtime),
    then we could define the stack pointer with:
    // mov bp, HEX(7c00)
    lea sp, [bp-32]
*/
    sti

    /* Relocate 512 bytes of MBR down to 0:0600 */
    cld
    mov si, HEX(7c00)
    mov di, offset start
    mov cx, 512 / 2
    rep movsw

    /* Jump into relocated code */
    ljmp16 0, cont

ASSUME CS:.text, DS:.text, ES:.text, SS:.text
cont:

    /* Search for active partition */
    mov di, offset parttbl // start of partition table
test_next_for_active:
    test byte ptr ds:[di], HEX(80)
    jne active_partition_found
    add di, 16                   // next table entry
    cmp di, offset BootSignature // scanned beyond end of table?
    jb test_next_for_active

/*****************************************************************/
    call print
    .asciz "No active partition found"

WAIT_FOR_REBOOT:
    jmp WAIT_FOR_REBOOT


/*****************************************************************/
trouble_reading_drive:
    call print
    .asciz "Read error while reading drive"
    jmp WAIT_FOR_REBOOT

/*****************************************************************/

invalid_partition_code:
    call print
    .asciz "Partition signature != 55AA"
    jmp WAIT_FOR_REBOOT

/*****************************************************************/

active_partition_found:
    call read_boot_sector
    jc trouble_reading_drive

    cmp word ptr ds:[HEX(7c00) + HEX(1fe)], HEX(0aa55)
    jne invalid_partition_code

    ljmp16 0, HEX(7c00)            // and jump to boot sector code

/*****************************
 * read_boot_sector
 *
 * IN: DI--> partition info
 * OUT:CARRY
 *****************************/
read_boot_sector:
    /* check for LBA support */
    mov bx, HEX(55aa)
    mov ah, HEX(41)
    int HEX(13)

    jc  StandardBios    //  if (regs.b.x != 0xaa55 || (regs.flags & 0x01))
    cmp bx, HEX(0aa55)  //    goto StandardBios;
    jne StandardBios

    /* if DAP cannot be used, don't use LBA
       if ((regs.c.x & 1) == 0)
           goto StandardBios; */
    test cl, 1
    jz StandardBios

    jmp short LBABios


_bios_LBA_address_packet:
    .byte 16
    .byte 0
    .byte 4         // read four sectors - why not
    .byte 0
    .word HEX(7c00) // fixed boot address for DOS sector
    .word HEX(0000)

_bios_LBA_low:
    .word 0
_bios_LBA_high:
    .word 0
    .word 0,0


LBABios:
    // copy start address of partition to DAP
    mov ax, [di + 8]
    mov word ptr ds:[_bios_LBA_low], ax
    mov ax, [di + 8 + 2]
    mov word ptr ds:[_bios_LBA_high], ax

    mov ax, HEX(4200)    //  regs.a.x = LBA_READ;
    mov si, offset _bios_LBA_address_packet // regs.si = FP_OFF(&dap);

    int HEX(13)
    ret

/*****************************************************************
 * read disk, using standard BIOS
 */
StandardBios:
    mov ax, HEX(0204)               //  regs.a.x = 0x0201;
    mov bx, HEX(7c00)               //  regs.b.x = FP_OFF(buffer);

    /* regs.c.x =
       ((chs.Cylinder & 0xff) << 8) + ((chs.Cylinder & 0x300) >> 2) +
        ; chs.Sector;
         that was easy ;-) */
    mov cx, word ptr ds:[di + 2]
    mov dh, byte ptr ds:[di + 1]    //  regs.d.b.h = chs.Head;
                                    //  regs.es = FP_SEG(buffer);
    int HEX(13)
    ret

/****** PRINT
 * prints text after call to this function.
 */
print_1char:
    xor bx, bx                   // video page 0
    mov ah, HEX(0E)              // else print it
    int HEX(10)                  // via TTY mode
print:
    pop si                       // this is the first character
print1:
    lodsb                        // get token
    push si                      // stack up potential return address
    cmp al, 0                    // end of string?
    jne print_1char              // until done
    ret                          // and jump to it

.org 446 // HEX(1be)
parttbl:
    // 4 primary partition table entries here...

.org 510 // HEX(1fe)
BootSignature:
    .byte HEX(55), HEX(0aa)

.endcode16

END
