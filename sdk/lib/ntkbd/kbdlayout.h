
#ifndef __KBDLAYOUT_H__
#define __KBDLAYOUT_H__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// #include <ndk/kbd.h>

/* In kbdus.c */
extern KBDTABLES KbdTablesFallback;

typedef struct _CLIENTKEYBOARDTYPE
{
    ULONG KbdType;
    ULONG KbdSubType;
    ULONG nFnKeys;
} CLIENTKEYBOARDTYPE, *PCLIENTKEYBOARDTYPE;

/*
 * Prototypes of the exports.
 */
typedef PKBDTABLES (NTAPI *PFN_KBD_LAYER_DESCRIPTOR)(VOID);
typedef PVOID /*PKBDNLSTABLES*/ (NTAPI *PFN_KBD_NLS_LAYER_DESCRIPTOR)(VOID);

typedef BOOLEAN (NTAPI *PFN_KBD_LAYER_REALDLLFILE_NT4)(
    OUT PWSTR pFileName);

typedef PVOID (NTAPI *PFN_KBD_LAYER_REALDLLFILE)(
    IN HKL hklID, // Input locale identifier.
    OUT PWSTR pFileName,
    IN PCLIENTKEYBOARDTYPE pKbdType,
    IN PVOID Reserved);

typedef BOOLEAN (NTAPI *PFN_KBD_LAYER_MULTIDESCRIPTOR)(
    IN OUT PVOID xxxx);

/*
 * Ordinals for the exports.
 */
typedef enum _KBD_LAYER_DLL_ORDINAL
{
    ORDINAL_KbdLayerDescriptor = 1,
    ORDINAL_KbdNlsLayerDescriptor = 2,
    ORDINAL_KbdLayerRealDllFileNT4 = 3,
    ORDINAL_Reserved = 4, // Ordinal 4 is always empty.
    ORDINAL_KbdLayerRealDllFile = 5,
    ORDINAL_KbdLayerMultiDescriptor = 6,
    ORDINAL_Maximum
} KBD_LAYER_DLL_ORDINAL, *PKBD_LAYER_DLL_ORDINAL;


VOID
DumpKbdLayout(
    IN PKBDTABLES pKbdTbl);


#define GET_KS_BYTE(vk)     (((BYTE)(vk)) * 2 / 8)
#define GET_KS_DOWN_BIT(vk) (1 << ((((BYTE)(vk)) % 4) * 2))
#define GET_KS_LOCK_BIT(vk) (1 << ((((BYTE)(vk)) % 4) * 2 + 1))

#define IS_KEY_DOWN(ks, vk)     ((ks)[GET_KS_BYTE(vk)] & GET_KS_DOWN_BIT(vk))
#define IS_KEY_LOCKED(ks, vk)   ((ks)[GET_KS_BYTE(vk)] & GET_KS_LOCK_BIT(vk))

#define SET_KEY_DOWN(ks, vk, down) \
    ( (down) ? ((ks)[GET_KS_BYTE(vk)] |=  GET_KS_DOWN_BIT(vk)) \
             : ((ks)[GET_KS_BYTE(vk)] &= ~GET_KS_DOWN_BIT(vk)) )

#define SET_KEY_LOCKED(ks, vk, down) \
    ( (down) ? ((ks)[GET_KS_BYTE(vk)] |=  GET_KS_LOCK_BIT(vk)) \
             : ((ks)[GET_KS_BYTE(vk)] &= ~GET_KS_LOCK_BIT(vk)) )


/*
 * IntSimplifyVk
 *
 * Changes virtual keys which distinguish between left and right hand, to keys which don't distinguish
 */
WORD /*FASTCALL*/
IntSimplifyVk(
    IN WORD wVk);

/*
 * IntFixVk
 *
 * Changes virtual keys which don't not distinguish between left and right hand to proper keys
 */
WORD /*FASTCALL*/
IntFixVk(
    IN WORD wVk,
    IN BOOLEAN bExt);

/*
 * IntVkToVsc
 *
 * Translates virtual key to scan code
 */
WORD /*FASTCALL*/
IntVkToVsc(
    IN WORD wVk,
    IN PKBDTABLES pKbdTbl);

/*
 * IntVscToVk
 *
 * Translates prefixed scancode to virtual key
 */
WORD /*FASTCALL*/
IntVscToVk(
    IN WORD wScanCode,
    IN PKBDTABLES pKbdTbl);

/*
 * IntTranslateNumpadKey
 *
 * Translates numpad keys when numlock is enabled
 */
WORD /*FASTCALL*/
IntTranslateNumpadKey(
    IN WORD wVk);


/*
 * IntTranslateChar
 *
 * Translates virtual key to character
 */
BOOL
IntTranslateChar(
    IN WORD wVirtKey,
    IN PBYTE pKeyState,
    OUT PBOOL pbDead,
    OUT PBOOL pbLigature,
    OUT PWCHAR pwcTranslatedChar,
    IN PKBDTABLES pKbdTbl);

/*
 * IntToUnicodeEx
 *
 * Translates virtual key to characters
 */
INT
IntToUnicodeEx(
    IN UINT wVirtKey,
    IN UINT wScanCode,
    IN PBYTE pKeyState,
    OUT PWSTR pwszBuff,
    IN INT cchBuff,
    IN UINT wFlags,
    IN PKBDTABLES pKbdTbl);



/*
 * IntVkToChar
 *
 * Translates virtual key to character, ignoring shift state
 */
UINT /*FASTCALL*/
IntVkToChar(
    IN WORD wVk,
    IN PKBDTABLES pKbdTbl);

/*
 * Map a virtual key code, or virtual scan code, to a scan code, key code,
 * or unshifted unicode character.
 *
 * Code: See Below
 * Type:
 * 0 -- Code is a virtual key code that is converted into a virtual scan code
 *      that does not distinguish between left and right shift keys.
 * 1 -- Code is a virtual scan code that is converted into a virtual key code
 *      that does not distinguish between left and right shift keys.
 * 2 -- Code is a virtual key code that is converted into an unshifted unicode
 *      character.
 * 3 -- Code is a virtual scan code that is converted into a virtual key code
 *      that distinguishes left and right shift keys.
 * KeyLayout: Keyboard layout handle
 *
 * @implemented
 */
UINT /*FASTCALL*/
IntMapVirtualKeyEx(
    IN UINT uCode,
    IN UINT Type,
    IN PKBDTABLES pKbdTbl);

/*
 * Retrieve a virtual key code corresponding to the given character.
 *
 * Based on IntTranslateChar, instead of processing VirtualKey match,
 * look for wChar match.
 */
USHORT /*FASTCALL*/
IntVkKeyScanEx(
    IN WCHAR wch,
    IN PKBDTABLES pKbdTbl);


PCWSTR
IntGetKeyName(
    IN WORD wScanCode,
    IN BOOLEAN bExtKey,
    IN PKBDTABLES pKbdTbl);


VOID
// IntUpdateRawKeyState
IntUpdateKeyState(
    IN PBYTE afKeyState,
    IN WORD wVk,
    IN BOOLEAN bIsDown);

#ifdef __cplusplus
}
#endif

#endif /* __KBDLAYOUT_H__ */
