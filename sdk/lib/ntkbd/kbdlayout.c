
/* PSDK/NDK Headers */
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winuser.h> // For VK_xxx
// #include <winnt.h>

#define NTOS_MODE_USER
#include <ndk/rtlfuncs.h>

#include <ndk/kbd.h>
#include "kbdlayout.h"

#define NDEBUG
#include <debug.h>

#define TRACE   DPRINT
#define WARN    DPRINT1
#define ERR     DPRINT1


//
// Helper to open a kbd***.dll file, defaulting to kdbus.dll
// if none else can be found. Usage of this function is optional.
//
NTSTATUS
KbdOpenLayoutFile(
    OUT PHANDLE KbdLayoutFileHandle)
{
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
KbdLoadLayout(
    IN HANDLE Handle, // hFile (See downloads.securityfocus.com/vulnerabilities/exploits/43774.c)
    IN DWORD offTable, // Offset to KbdTables
    IN PUNICODE_STRING puszKeyboardName, // Not used?
    IN HKL hklUnload,
    IN PUNICODE_STRING pustrKLID,
    IN DWORD hkl,
    IN UINT Flags)
{
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
KbdUnloadLayout(
    IN PVOID KbdLayout)
{
    return STATUS_NOT_IMPLEMENTED;
}


//
// TODO For ALL functions: Check the validity of the pointers:
// - pCharModifiers, and pCharModifiers->pVkToBit
// - pVkToWcharTable, and pVkToWcharTable->pVkToWchars
// - pDeadKey (optional table),
// - pKeyNames, and pKeyNames->pwsz
// - pKeyNamesExt, and pKeyNamesExt->pwsz
// - pKeyNamesDead (optional table)
// - pusVSCtoVK (could be NULL if bMaxVSCtoVK == 0),
// - pVSCtoVK_E0,
// - pVSCtoVK_E1,
// - pLigature (optional table).
//


VOID
DumpKbdLayout(
    IN PKBDTABLES pKbdTbl)
{
    PVK_TO_BIT pVkToBit;
    PVK_TO_WCHAR_TABLE pVkToWchTbl;
    PVSC_VK pVscVk;
    ULONG i;

    DbgPrint("Kbd layout: fLocaleFlags %x bMaxVSCtoVK %x\n",
             pKbdTbl->fLocaleFlags, pKbdTbl->bMaxVSCtoVK);
    DbgPrint("wMaxModBits %x\n",
             pKbdTbl->pCharModifiers ? pKbdTbl->pCharModifiers->wMaxModBits
                                     : 0);

    if (pKbdTbl->pCharModifiers)
    {
        pVkToBit = pKbdTbl->pCharModifiers->pVkToBit;
        if (pVkToBit)
        {
            for (; pVkToBit->Vk; ++pVkToBit)
            {
                DbgPrint("VkToBit %x -> %x\n", pVkToBit->Vk, pVkToBit->ModBits);
            }
        }

        for (i = 0; i <= pKbdTbl->pCharModifiers->wMaxModBits; ++i)
        {
            DbgPrint("ModNumber %x -> %x\n", i, pKbdTbl->pCharModifiers->ModNumber[i]);
        }
    }

    pVkToWchTbl = pKbdTbl->pVkToWcharTable;
    if (pVkToWchTbl)
    {
        for (; pVkToWchTbl->pVkToWchars; ++pVkToWchTbl)
        {
            PVK_TO_WCHARS1 pVkToWch = pVkToWchTbl->pVkToWchars;

            DbgPrint("pVkToWchTbl nModifications %x cbSize %x\n",
                     pVkToWchTbl->nModifications, pVkToWchTbl->cbSize);
            if (pVkToWch)
            {
                while (pVkToWch->VirtualKey)
                {
                    DbgPrint("pVkToWch VirtualKey %x Attributes %x wc { ",
                             pVkToWch->VirtualKey, pVkToWch->Attributes);
                    for (i = 0; i < pVkToWchTbl->nModifications; ++i)
                    {
                        DbgPrint("%x ", pVkToWch->wch[i]);
                    }
                    DbgPrint("}\n");
                    pVkToWch = (PVK_TO_WCHARS1)(((PBYTE)pVkToWch) + pVkToWchTbl->cbSize);
                }
            }
        }
    }

// TODO: DeadKeys, KeyNames, KeyNamesExt, KeyNamesDead

    DbgPrint("pusVSCtoVK: { ");
    if (pKbdTbl->pusVSCtoVK)
    {
        for (i = 0; i < pKbdTbl->bMaxVSCtoVK; ++i)
        {
            DbgPrint("%x -> %x, ", i, pKbdTbl->pusVSCtoVK[i]);
        }
    }
    DbgPrint("}\n");

    DbgPrint("pVSCtoVK_E0: { ");
    pVscVk = pKbdTbl->pVSCtoVK_E0;
    if (pVscVk)
    {
        for (; pVscVk->Vsc; ++pVscVk)
        {
            DbgPrint("%x -> %x, ", pVscVk->Vsc, pVscVk->Vk);
        }
    }
    DbgPrint("}\n");

    DbgPrint("pVSCtoVK_E1: { ");
    pVscVk = pKbdTbl->pVSCtoVK_E1;
    if (pVscVk)
    {
        for (; pVscVk->Vsc; ++pVscVk)
        {
            DbgPrint("%x -> %x, ", pVscVk->Vsc, pVscVk->Vk);
        }
    }
    DbgPrint("}\n");

// TODO: Ligatures
}



//
// Translation functions.
//


/*
 * IntSimplifyVk
 *
 * Changes virtual keys which distinguish between left and right hand, to keys which don't distinguish
 */
WORD /*FASTCALL*/
IntSimplifyVk(
    IN WORD wVk)
{
    switch (wVk)
    {
        case VK_LSHIFT: case VK_RSHIFT:
            return VK_SHIFT;

        case VK_LCONTROL: case VK_RCONTROL:
            return VK_CONTROL;

        case VK_LMENU: case VK_RMENU:
            return VK_MENU;

        default:
            return wVk;
    }
}

/*
 * IntFixVk
 *
 * Changes virtual keys which don't not distinguish between left and right hand to proper keys
 */
WORD /*FASTCALL*/
IntFixVk(
    IN WORD wVk,
    IN BOOLEAN bExt)
{
    switch (wVk)
    {
        case VK_SHIFT:
            return bExt ? VK_RSHIFT : VK_LSHIFT;

        case VK_CONTROL:
            return bExt ? VK_RCONTROL : VK_LCONTROL;

        case VK_MENU:
            return bExt ? VK_RMENU : VK_LMENU;

        default:
            return wVk;
    }
}

/*
 * IntVkToVsc
 *
 * Translates virtual key to scan code
 */
WORD /*FASTCALL*/
IntVkToVsc(
    IN WORD wVk,
    IN PKBDTABLES pKbdTbl)
{
    ULONG i;

    /* Check standard keys first */
    if (pKbdTbl->bMaxVSCtoVK > 0 /* && pKbdTbl->pusVSCtoVK */)
    {
        for (i = 0; i < pKbdTbl->bMaxVSCtoVK; ++i)
        {
            if ((pKbdTbl->pusVSCtoVK[i] & 0xFF) == wVk)
                return i; // INVESTIGATE: Here Windows returns (i & 0xFF) instead.
        }
    }

    /* Check extended keys now */
    if (pKbdTbl->pVSCtoVK_E0)
    {
        for (i = 0; pKbdTbl->pVSCtoVK_E0[i].Vsc; ++i)
        {
            if ((pKbdTbl->pVSCtoVK_E0[i].Vk & 0xFF) == wVk)
                return 0xE000 | pKbdTbl->pVSCtoVK_E0[i].Vsc;
        }
    }
    if (pKbdTbl->pVSCtoVK_E1)
    {
        for (i = 0; pKbdTbl->pVSCtoVK_E1[i].Vsc; ++i)
        {
            if ((pKbdTbl->pVSCtoVK_E1[i].Vk & 0xFF) == wVk)
                return 0xE100 | pKbdTbl->pVSCtoVK_E1[i].Vsc;
        }
    }

// TODO: Scan numpad keys !!!! (à la IntTranslateNumpadKey,
// and not by looking in pKbdTbl tables).

    /* Virtual key has not been found */
    return 0;
}

/*
 * IntVscToVk
 *
 * Translates prefixed scancode to virtual key
 */
WORD /*FASTCALL*/
IntVscToVk(
    IN WORD wScanCode,
    IN PKBDTABLES pKbdTbl)
{
    ULONG i;
    WORD wVk = 0;

    if (wScanCode < pKbdTbl->bMaxVSCtoVK)
    {
        wVk = pKbdTbl->pusVSCtoVK[wScanCode];
    }
    else if ((wScanCode & 0xFF00) == 0xE000)
    {
        /* Ignore any fake left/right-shift that may have been emitted */
        if (((wScanCode & 0xFF) == SCANCODE_LSHIFT) ||
            ((wScanCode & 0xFF) == SCANCODE_RSHIFT))
        {
            return 0;
        }
        if (pKbdTbl->pVSCtoVK_E0)
        {
            for (i = 0; pKbdTbl->pVSCtoVK_E0[i].Vsc; ++i)
            {
                if (pKbdTbl->pVSCtoVK_E0[i].Vsc == (wScanCode & 0xFF))
                {
                    wVk = pKbdTbl->pVSCtoVK_E0[i].Vk;
                }
            }
        }
    }
    else if (((wScanCode & 0xFF00) == 0xE100) && pKbdTbl->pVSCtoVK_E1)
    {
        for (i = 0; pKbdTbl->pVSCtoVK_E1[i].Vsc; ++i)
        {
            if (pKbdTbl->pVSCtoVK_E1[i].Vsc == (wScanCode & 0xFF))
            {
                wVk = pKbdTbl->pVSCtoVK_E1[i].Vk;
            }
        }
    }

    /* 0xFF and 0x00 are invalid VKs */
    return (wVk != 0xFF ? wVk : 0);
}

/*
 * IntTranslateNumpadKey
 *
 * Translates numpad keys when numlock is enabled
 */
WORD /*FASTCALL*/
IntTranslateNumpadKey(
    IN WORD wVk)
{
    // aVkNumpad table.
    switch (wVk)
    {
        case VK_INSERT: return VK_NUMPAD0;
        case VK_END:    return VK_NUMPAD1;
        case VK_DOWN:   return VK_NUMPAD2;
        case VK_NEXT:   return VK_NUMPAD3;
        case VK_LEFT:   return VK_NUMPAD4;
        case VK_CLEAR:  return VK_NUMPAD5;
        case VK_RIGHT:  return VK_NUMPAD6;
        case VK_HOME:   return VK_NUMPAD7;
        case VK_UP:     return VK_NUMPAD8;
        case VK_PRIOR:  return VK_NUMPAD9;
        case VK_DELETE: return VK_DECIMAL;
        default:        return wVk;
    }
}


/*
 * IntGetModBits
 *
 * Gets layout specific modification bits, for example KBDSHIFT, KBDCTRL, KBDALT
 */
static DWORD
IntGetModBits(
    IN PBYTE pKeyState,
    IN PKBDTABLES pKbdTbl)
{
    DWORD i, dwModBits = 0;

    for (i = 0; pKbdTbl->pCharModifiers->pVkToBit[i].Vk; ++i)
    {
        if (IS_KEY_DOWN(pKeyState, pKbdTbl->pCharModifiers->pVkToBit[i].Vk))
            dwModBits |= pKbdTbl->pCharModifiers->pVkToBit[i].ModBits;
    }

    TRACE("Current Mod Bits: %lx\n", dwModBits);

    return dwModBits;
}

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
    IN PKBDTABLES pKbdTbl)
{
    PVK_TO_WCHAR_TABLE pVkToVchTbl;
    PVK_TO_WCHARS10 pVkToVch;
    DWORD i, dwModBits, dwVkModBits, dwModNumber = 0;
    WCHAR wch;
    BOOL bAltGr;
    WORD wCaplokAttr;

    dwModBits = pKeyState ? IntGetModBits(pKeyState, pKbdTbl) : 0;
    bAltGr = pKeyState && (pKbdTbl->fLocaleFlags & KLLF_ALTGR) && IS_KEY_DOWN(pKeyState, VK_RMENU);
    wCaplokAttr = bAltGr ? CAPLOKALTGR : CAPLOK;

    TRACE("TryToTranslate: %04x %x\n", wVirtKey, dwModBits);

    /* If ALT without CTRL has been used, remove ALT flag */
    if ((dwModBits & (KBDALT|KBDCTRL)) == KBDALT)
        dwModBits &= ~KBDALT;

    if (dwModBits > pKbdTbl->pCharModifiers->wMaxModBits)
    {
        TRACE("dwModBits %x > wMaxModBits %x\n",
              dwModBits, pKbdTbl->pCharModifiers->wMaxModBits);
        return FALSE;
    }

    for (i = 0; pKbdTbl->pVkToWcharTable[i].pVkToWchars; ++i)
    {
        pVkToVchTbl = &pKbdTbl->pVkToWcharTable[i];
        pVkToVch = (PVK_TO_WCHARS10)(pVkToVchTbl->pVkToWchars);
        while (pVkToVch->VirtualKey)
        {
            if (wVirtKey == (pVkToVch->VirtualKey & 0xFF))
            {
                dwVkModBits = dwModBits;

                /* If CapsLock is enabled for this key and locked, add SHIFT bit */
                if ((pVkToVch->Attributes & wCaplokAttr) &&
                    pKeyState && IS_KEY_LOCKED(pKeyState, VK_CAPITAL))
                {
                    /* Note: we use special value here instead of getting VK_SHIFT mod bit - it's verified */
                    dwVkModBits ^= KBDSHIFT;
                }

                if (dwVkModBits > pKbdTbl->pCharModifiers->wMaxModBits)
                    break;

                /* Get modification number */
                dwModNumber = pKbdTbl->pCharModifiers->ModNumber[dwVkModBits];
                if (dwModNumber >= pVkToVchTbl->nModifications)
                {
                    TRACE("dwModNumber %u >= nModifications %u\n",
                          dwModNumber, pVkToVchTbl->nModifications);
                    break;
                }

                /* Read character */
                wch = pVkToVch->wch[dwModNumber];
                if (wch == WCH_NONE)
                    break;

                *pbDead = (wch == WCH_DEAD);
                *pbLigature = (wch == WCH_LGTR);
                *pwcTranslatedChar = wch;

                TRACE("%lu %04x: dwModNumber %08x Char %04x\n",
                      i, wVirtKey, dwModNumber, wch);

                if (*pbDead)
                {
                    /* After WCH_DEAD, real character is located */
                    pVkToVch = (PVK_TO_WCHARS10)(((BYTE *)pVkToVch) + pVkToVchTbl->cbSize);
                    if (pVkToVch->VirtualKey != 0xFF)
                    {
                        WARN("Found dead key with no trailer in the table.\n"
                             "VK: %04x, ADDR: %p\n", wVirtKey, pVkToVch);
                        break;
                    }
                    *pwcTranslatedChar = pVkToVch->wch[dwModNumber];
                }
                return TRUE;
            }
            pVkToVch = (PVK_TO_WCHARS10)(((BYTE *)pVkToVch) + pVkToVchTbl->cbSize);
        }
    }

    /* If nothing has been found in layout, check if this is ASCII control character.
       Note: we could add it to layout table, but windows does not have it there */
    if (wVirtKey >= 'A' && wVirtKey <= 'Z' &&
        pKeyState && IS_KEY_DOWN(pKeyState, VK_CONTROL) &&
        !IS_KEY_DOWN(pKeyState, VK_MENU))
    {
        *pwcTranslatedChar = (wVirtKey - 'A') + 1; /* ASCII control character */
        *pbDead = FALSE;
        *pbLigature = FALSE;
        return TRUE;
    }

    return FALSE;
}

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
    IN PKBDTABLES pKbdTbl)
{
    WCHAR wchTranslatedChar;
    BOOL bDead, bLigature;
    static WCHAR wchDead = 0; // FIXME: Make global or param of function!!
    INT iRet = 0;

// FIXME: Handle wScanCode (the high bit)

    if (!IntTranslateChar(wVirtKey,
                          pKeyState,
                          &bDead,
                          &bLigature,
                          &wchTranslatedChar,
                          pKbdTbl))
    {
        return 0;
    }

    if (bLigature)
    {
        WARN("Not handling ligature (yet)\n");
        return 0;
    }

    /* If we got dead char in previous call check dead keys in keyboard layout */
    if (wchDead)
    {
        UINT i;
        WCHAR wchFirst, wchSecond;
        TRACE("Previous dead char: %lc (%x)\n", wchDead, wchDead);

        if (pKbdTbl->pDeadKey)
        {
            for (i = 0; pKbdTbl->pDeadKey[i].dwBoth; ++i)
            {
                wchFirst = HIWORD(pKbdTbl->pDeadKey[i].dwBoth);
                wchSecond = LOWORD(pKbdTbl->pDeadKey[i].dwBoth);
                if (wchFirst == wchDead && wchSecond == wchTranslatedChar)
                {
                    wchTranslatedChar = pKbdTbl->pDeadKey[i].wchComposed;
                    wchDead = 0;
                    bDead = FALSE;
                    break;
                }
            }
        }
        else
        {
#if defined(__GNUC__) // FIXME: CORE-14948
            if (wchDead == 0x8000)
            {
                ERR("GCC is inventing bits, ignoring fake dead key\n");
                wchDead = 0;
            }
#endif
        }

        TRACE("Final char: %lc (%x)\n", wchTranslatedChar, wchTranslatedChar);
    }

    /* Dead char has not been not found */
    if (wchDead)
    {
        /* Treat both characters normally */
        if (cchBuff > iRet)
            pwszBuff[iRet++] = wchDead;
        bDead = FALSE;
    }

    /* Add character to the buffer */
    if (cchBuff > iRet)
        pwszBuff[iRet++] = wchTranslatedChar;

    /* Save dead character */
    wchDead = bDead ? wchTranslatedChar : 0;

    return bDead ? -iRet : iRet;
}



/*
 * IntVkToChar
 *
 * Translates virtual key to character, ignoring shift state
 */
UINT /*FASTCALL*/
IntVkToChar(
    IN WORD wVk,
    IN PKBDTABLES pKbdTbl)
{
    WCHAR wch;
    BOOL bDead, bLigature;

    if (IntTranslateChar(wVk,
                         NULL,
                         &bDead,
                         &bLigature,
                         &wch,
                         pKbdTbl))
    {
        /* If this is a dead char, set the high bit (see MSDN
         * description for MapVirtualKeyEx(), MAPVK_VK_TO_CHAR). */
        return (bDead ? (0x80000000 | (UINT)wch) : (UINT)wch);
    }

    return 0;
}

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
    IN PKBDTABLES pKbdTbl)
{
    UINT uRet = 0;

    switch (Type)
    {
        case MAPVK_VK_TO_VSC:
            uCode = IntFixVk(uCode, FALSE);
            uRet = IntVkToVsc(uCode, pKbdTbl);
            if (uRet > 0xFF) // Fail for scancodes with prefix (e0, e1)
                uRet = 0;    // INVESTIGATE: Windows: Not really.
            break;

        case MAPVK_VSC_TO_VK:
            uRet = IntVscToVk(uCode, pKbdTbl) & 0xFF;
            uRet = IntSimplifyVk(uRet);
            // if (uRet == 0xFF) uRet = 0; // Done by IntVscToVk.
            break;

        case MAPVK_VK_TO_CHAR:
            uRet = IntVkToChar(uCode, pKbdTbl);
            break;

        case MAPVK_VSC_TO_VK_EX:
            uRet = IntVscToVk(uCode, pKbdTbl) & 0xFF;
            // if (uRet == 0xFF) uRet = 0; // Done by IntVscToVk.
            break;

        case MAPVK_VK_TO_VSC_EX:
            uRet = IntVkToVsc(uCode, pKbdTbl);
            break;

        default:
            // Status = STATUS_INVALID_PARAMETER;
            ERR("Wrong type value: %u\n", Type);
    }

    return uRet;
}

/*
 * Retrieve a virtual key code corresponding to the given character.
 *
 * Based on IntTranslateChar, instead of processing VirtualKey match,
 * look for wChar match.
 */
USHORT /*FASTCALL*/
IntVkKeyScanEx(
    IN WCHAR wch,
    IN PKBDTABLES pKbdTbl)
{
    PVK_TO_WCHAR_TABLE pVkToWchTbl;
    PVK_TO_WCHARS10 pVkToWch;
    DWORD i, dwModBits = 0, dwModNumber = 0;
    USHORT Ret = (USHORT)-1;

    /* Iterate through all VkToWchar tables while pVkToWchars is not NULL */
    for (i = 0; pKbdTbl->pVkToWcharTable[i].pVkToWchars; ++i)
    {
        pVkToWchTbl = &pKbdTbl->pVkToWcharTable[i];
        pVkToWch = (PVK_TO_WCHARS10)(pVkToWchTbl->pVkToWchars);

        /* Iterate through all virtual keys */
        while (pVkToWch->VirtualKey)
        {
            for (dwModNumber = 0; dwModNumber < pVkToWchTbl->nModifications; dwModNumber++)
            {
                if (pVkToWch->wch[dwModNumber] == wch)
                {
                    dwModBits = pKbdTbl->pCharModifiers->ModNumber[dwModNumber];
                    TRACE("i %lu wC %04x: dwModBits %08x dwModNumber %08x MaxModBits %08x\n",
                          i, wch, dwModBits, dwModNumber, pKbdTbl->pCharModifiers->wMaxModBits);
                    Ret = (dwModBits << 8) | (pVkToWch->VirtualKey & 0xFF);
                    return Ret;
                }
            }
            pVkToWch = (PVK_TO_WCHARS10)(((BYTE *)pVkToWch) + pVkToWchTbl->cbSize);
        }
    }

    return Ret;
}


PCWSTR
IntGetKeyName(
    IN WORD wScanCode,
    IN BOOLEAN bExtKey,
    IN PKBDTABLES pKbdTbl)
{
    PVSC_LPWSTR pKeyNames;

    if (bExtKey)
        pKeyNames = pKbdTbl->pKeyNamesExt;
    else
        pKeyNames = pKbdTbl->pKeyNames;

    if (!pKeyNames)
        return NULL;

    for (; pKeyNames->pwsz; ++pKeyNames)
    {
        if (pKeyNames->vsc == wScanCode)
            return pKeyNames->pwsz;
    }
    return NULL;
}


/* Change the input key state for a given key, via its Vk (low byte) */
VOID
IntUpdateKeyState(
    IN PBYTE afKeyState,
    IN WORD wVk,
    IN BOOLEAN bIsDown)
{
    TRACE("IntUpdateKeyState wVk: %u, bIsDown: %d\n", wVk, bIsDown);

    if (bIsDown)
    {
        /* If it's first key down event, xor lock bit */
        if (!IS_KEY_DOWN(afKeyState, wVk))
            SET_KEY_LOCKED(afKeyState, wVk, !IS_KEY_LOCKED(afKeyState, wVk));

        SET_KEY_DOWN(afKeyState, wVk, TRUE);
    }
    else
    {
        SET_KEY_DOWN(afKeyState, wVk, FALSE);
    }
}
