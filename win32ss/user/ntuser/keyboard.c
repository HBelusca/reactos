/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS kernel
 * PURPOSE:          Keyboard functions
 * FILE:             win32ss/user/ntuser/keyboard.c
 * PROGRAMERS:       Casper S. Hornstrup (chorns@users.sourceforge.net)
 *                   Rafal Harabien (rafalh@reactos.org)
 */

#include <win32k.h>
DBG_DEFAULT_CHANNEL(UserKbd);
#define TRACE2 ERR

BYTE gafAsyncKeyState[256 * 2 / 8]; // 2 bits per key
static BYTE gafAsyncKeyStateRecentDown[256 / 8]; // 1 bit per key
static PKEYBOARD_INDICATOR_TRANSLATION gpKeyboardIndicatorTrans = NULL;
static KEYBOARD_INDICATOR_PARAMETERS gIndicators = {0, 0};
KEYBOARD_ATTRIBUTES gKeyboardInfo;
static INT gLanguageToggleKeyState = 0;
DWORD gdwLanguageToggleKey = 1;
static INT gLayoutToggleKeyState = 0;
DWORD gdwLayoutToggleKey = 2;

#include <cjkcode.h>

/* State for Alt+Numpad character entry */
static enum _ALTNUM_STATE
{
    ALTNUM_INACTIVE,
    ALTNUM_OEM,     // Alt  xxx
    ALTNUM_ACP,     // Alt 0xxx
    ALTNUM_HEX_ACP, // Alt .xxx
    ALTNUM_HEX_UTF  // Alt +xxx
} gAltNumPadState = ALTNUM_INACTIVE;

static ULONG gAltNumPadValue = 0;
BOOL gbEnableHexNumpad = FALSE;

/**/static enum _ALTNUM_STATE gPrevAltNumPadState = ALTNUM_INACTIVE;
/**/static ULONG gPrevAltNumPadValue = 0;/**/

/* FUNCTIONS *****************************************************************/

/*
 * InitKeyboardImpl
 *
 * Initialization -- Right now, just zero the key state
 */
CODE_SEG("INIT")
NTSTATUS
NTAPI
InitKeyboardImpl(VOID)
{
    RtlZeroMemory(&gafAsyncKeyState, sizeof(gafAsyncKeyState));
    RtlZeroMemory(&gafAsyncKeyStateRecentDown, sizeof(gafAsyncKeyStateRecentDown));
    // Clear and set default information.
    RtlZeroMemory(&gKeyboardInfo, sizeof(gKeyboardInfo));
    gKeyboardInfo.KeyboardIdentifier.Type = 4; /* AT-101 */
    gKeyboardInfo.NumberOfFunctionKeys = 12; /* We're doing an 101 for now, so return 12 F-keys */
    return STATUS_SUCCESS;
}

/*
 * IntKeyboardGetIndicatorTrans
 *
 * Asks the keyboard driver to send a small table that shows which
 * lights should connect with which scancodes
 */
//static
NTSTATUS APIENTRY
IntKeyboardGetIndicatorTrans(HANDLE hKeyboardDevice,
                             PKEYBOARD_INDICATOR_TRANSLATION *ppIndicatorTrans)
{
    NTSTATUS Status;
    DWORD dwSize = 0;
    IO_STATUS_BLOCK Block;
    PKEYBOARD_INDICATOR_TRANSLATION pRet;

    dwSize = sizeof(KEYBOARD_INDICATOR_TRANSLATION);

    pRet = ExAllocatePoolWithTag(PagedPool,
                                 dwSize,
                                 USERTAG_KBDTABLE);

    while (pRet)
    {
        Status = ZwDeviceIoControlFile(hKeyboardDevice,
                                       NULL,
                                       NULL,
                                       NULL,
                                       &Block,
                                       IOCTL_KEYBOARD_QUERY_INDICATOR_TRANSLATION,
                                       NULL, 0,
                                       pRet, dwSize);

        if (Status != STATUS_BUFFER_TOO_SMALL)
            break;

        ExFreePoolWithTag(pRet, USERTAG_KBDTABLE);

        dwSize += sizeof(KEYBOARD_INDICATOR_TRANSLATION);

        pRet = ExAllocatePoolWithTag(PagedPool,
                                     dwSize,
                                     USERTAG_KBDTABLE);
    }

    if (!pRet)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(pRet, USERTAG_KBDTABLE);
        return Status;
    }

    *ppIndicatorTrans = pRet;
    return Status;
}

/*
 * IntKeyboardUpdateLeds
 *
 * Sends the keyboard commands to turn on/off the lights
 */
static
NTSTATUS APIENTRY
IntKeyboardUpdateLeds(HANDLE hKeyboardDevice,
                      WORD wVk,
                      WORD wScanCode)
{
    NTSTATUS Status;
    UINT i;
    USHORT LedFlag = 0;
    IO_STATUS_BLOCK Block;

    if (!gpKeyboardIndicatorTrans)
        return STATUS_NOT_SUPPORTED;

    switch (wVk)
    {
        case VK_CAPITAL: LedFlag = KEYBOARD_CAPS_LOCK_ON; break;
        case VK_NUMLOCK: LedFlag = KEYBOARD_NUM_LOCK_ON; break;
        case VK_SCROLL: LedFlag = KEYBOARD_SCROLL_LOCK_ON; break;
        default:
            for (i = 0; i < gpKeyboardIndicatorTrans->NumberOfIndicatorKeys; i++)
            {
                if (gpKeyboardIndicatorTrans->IndicatorList[i].MakeCode == wScanCode)
                {
                    LedFlag = gpKeyboardIndicatorTrans->IndicatorList[i].IndicatorFlags;
                    break;
                }
            }
    }

    if (LedFlag)
    {
        gIndicators.LedFlags ^= LedFlag;

        /* Update the lights on the hardware */
        Status = ZwDeviceIoControlFile(hKeyboardDevice,
                                       NULL,
                                       NULL,
                                       NULL,
                                       &Block,
                                       IOCTL_KEYBOARD_SET_INDICATORS,
                                       &gIndicators, sizeof(gIndicators),
                                       NULL, 0);

        return Status;
    }

    return STATUS_SUCCESS;
}

/*
 * UserInitKeyboard
 *
 * Initializes keyboard indicators translation and their state
 */
VOID NTAPI
UserInitKeyboard(HANDLE hKeyboardDevice)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK Block;

    IntKeyboardGetIndicatorTrans(hKeyboardDevice, &gpKeyboardIndicatorTrans);

    Status = ZwDeviceIoControlFile(hKeyboardDevice,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &Block,
                                   IOCTL_KEYBOARD_QUERY_INDICATORS,
                                   NULL, 0,
                                   &gIndicators,
                                   sizeof(gIndicators));

    if (!NT_SUCCESS(Status))
    {
        WARN("NtDeviceIoControlFile() failed, ignored\n");
        gIndicators.LedFlags = 0;
        gIndicators.UnitId = 0;
    }

    SET_KEY_LOCKED(gafAsyncKeyState, VK_CAPITAL,
                   gIndicators.LedFlags & KEYBOARD_CAPS_LOCK_ON);
    SET_KEY_LOCKED(gafAsyncKeyState, VK_NUMLOCK,
                   gIndicators.LedFlags & KEYBOARD_NUM_LOCK_ON);
    SET_KEY_LOCKED(gafAsyncKeyState, VK_SCROLL,
                   gIndicators.LedFlags & KEYBOARD_SCROLL_LOCK_ON);

    // FIXME: Need device driver to work! HID support more than one!!!!
    Status = ZwDeviceIoControlFile(hKeyboardDevice,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &Block,
                                   IOCTL_KEYBOARD_QUERY_ATTRIBUTES,
                                   NULL, 0,
                                   &gKeyboardInfo, sizeof(gKeyboardInfo));

    if (!NT_SUCCESS(Status))
    {
        ERR("NtDeviceIoControlFile() failed, ignored\n");
    }
    TRACE("Keyboard type %u, subtype %u and number of func keys %u\n",
             gKeyboardInfo.KeyboardIdentifier.Type,
             gKeyboardInfo.KeyboardIdentifier.Subtype,
             gKeyboardInfo.NumberOfFunctionKeys);
}

/*
 * IntSimplifyVk
 *
 * Changes virtual keys which distinguish between left and right hand, to keys which don't distinguish
 */
static
WORD
IntSimplifyVk(WORD wVk)
{
    switch (wVk)
    {
        case VK_LSHIFT:
        case VK_RSHIFT:
            return VK_SHIFT;

        case VK_LCONTROL:
        case VK_RCONTROL:
            return VK_CONTROL;

        case VK_LMENU:
        case VK_RMENU:
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
static
WORD
IntFixVk(WORD wVk, BOOL bExt)
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
 * IntTranslateNumpadKey
 *
 * Translates numpad keys when numlock is enabled
 */
static
WORD
IntTranslateNumpadKey(WORD wVk)
{
    switch (wVk)
    {
        case VK_INSERT: return VK_NUMPAD0;
        case VK_END: return VK_NUMPAD1;
        case VK_DOWN: return VK_NUMPAD2;
        case VK_NEXT: return VK_NUMPAD3;
        case VK_LEFT: return VK_NUMPAD4;
        case VK_CLEAR: return VK_NUMPAD5;
        case VK_RIGHT: return VK_NUMPAD6;
        case VK_HOME: return VK_NUMPAD7;
        case VK_UP: return VK_NUMPAD8;
        case VK_PRIOR: return VK_NUMPAD9;
        case VK_DELETE: return VK_DECIMAL;
        default: return wVk;
    }
}

/*
 * IntGetModBits
 *
 * Gets layout specific modification bits, for example KBDSHIFT, KBDCTRL, KBDALT
 */
static
DWORD
IntGetModBits(PKBDTABLES pKbdTbl, PBYTE pKeyState)
{
    DWORD i, dwModBits = 0;

    /* DumpKeyState( KeyState ); */

    for (i = 0; pKbdTbl->pCharModifiers->pVkToBit[i].Vk; i++)
        if (IS_KEY_DOWN(pKeyState, pKbdTbl->pCharModifiers->pVkToBit[i].Vk))
            dwModBits |= pKbdTbl->pCharModifiers->pVkToBit[i].ModBits;

    TRACE("Current Mod Bits: %lx\n", dwModBits);

    return dwModBits;
}

static BOOL
IntIsAltNumpadKey(_In_ WORD wVk)
{
    /* Check if we have a "Numpad" key while in Alt+Numpad mode */
    switch (gAltNumPadState)
    {
        case ALTNUM_OEM: case ALTNUM_ACP:
        {
            /* Check if it is a numpad digit */
            if (wVk >= VK_NUMPAD0 && wVk <= VK_NUMPAD9)
                return TRUE;
            // else if (wVk >= '0' && wVk <= '9')
            //     return TRUE;
            break;
        }
        case ALTNUM_HEX_ACP: case ALTNUM_HEX_UTF:
        {
            /* Check if it represents a valid hexadecimal digit */
            if (wVk >= VK_NUMPAD0 && wVk <= VK_NUMPAD9)
                return TRUE;
            else if (wVk >= '0' && wVk <= '9')
                return TRUE;
            else if (wVk >= 'A' && wVk <= 'F')
                return TRUE;
            else if (/*gbEnableHexNumpad &&*/ (wVk == VK_DECIMAL)) // || (wVk == VK_OEM_PERIOD)
                return TRUE; // ALTNUM_HEX_ACP;
            else if (/*gbEnableHexNumpad &&*/ (wVk == VK_ADD)) // || (wVk == VK_OEM_PLUS)
                return TRUE; // ALTNUM_HEX_UTF;
            break;
        }
        default:
            break;
    }
    return FALSE;
}

// FIXME
static BOOL
IntTranslateAltNumpad(
    _Out_ PWCHAR pwcTranslatedChar,
    _Out_ LPARAM* plParamExtraFlags,
    _In_ WORD wVk,
    _In_ BOOL bIsDown);

/**
 * @brief   Translates a virtual key to a UTF-16 character.
 *
 * @param[in]   wVirtKey
 * The virtual key to translate.
 *
 * @param[in]   wScanCode
 * If provided, the hardware scan code of the key to be translated.
 * The high-order bit of this value is set if the key is up.
 * It is only used for deducing extra state for the key.
 *
 * @param[in]   pKeyState
 * Optional keyboard state bytes array.
 *
 * @param[out]  pbDead
 * On output, set to TRUE if the virtual key specifies a dead key, or FALSE if not.
 *
 * @param[out]  pbLigature
 * On output, set to TRUE if the virtual key specifies a ligature, or FALSE if not.
 *
 * @param[out]  pwcTranslatedChar
 * Pointer to a WCHAR that receives the translated UTF-16 character.
 *
 * @param[out]  plParamExtraFlags
 * Pointer to an LPARAM that receives any deduced extra flags, to be used
 * by IntTranslateKbdMessage(), indirectly passed through IntToUnicodeEx().
 *
 * @param[in]   wFlags
 * Flags modifying the function behaviour. It corresponds to the wFlags parameter
 * of the TranslateMessageEx(), ToUnicode(), and ToUnicodeEx() functions.
 * See the TM_* flags in undocuser.h.
 *
 * @param[in]   pKbdTbl
 * Pointer to keyboard layout tables.
 **/
static
BOOL
IntTranslateChar(
    _In_ WORD wVirtKey,
    _In_opt_ WORD wScanCode,
    _In_opt_ PBYTE pKeyState,
    _Out_ PBOOL pbDead,
    _Out_ PBOOL pbLigature,
    _Out_ PWCHAR pwcTranslatedChar,
    _Out_ LPARAM* plParamExtraFlags,
    _In_ UINT wFlags,
    _In_ PKBDTABLES pKbdTbl)
{
    PVK_TO_WCHAR_TABLE pVkToVchTbl;
    PVK_TO_WCHARS10 pVkToVch;
    DWORD i, dwModBits, dwVkModBits, dwModNumber = 0;
    WCHAR wch;
    BOOL bAltGr;
    WORD wCaplokAttr;

    dwModBits = pKeyState ? IntGetModBits(pKbdTbl, pKeyState) : 0;
    bAltGr = pKeyState && (pKbdTbl->fLocaleFlags & KLLF_ALTGR) && IS_KEY_DOWN(pKeyState, VK_RMENU);
    wCaplokAttr = bAltGr ? CAPLOKALTGR : CAPLOK;

    TRACE2("IntTranslateChar: %04x %x\n", wVirtKey, dwModBits);

    TRACE2("In IntTranslateChar, gAltNumPadState: %lu, gAltNumPadValue: %lu\n",
          gAltNumPadState, gAltNumPadValue);

    *plParamExtraFlags = (wScanCode & KBDBREAK) << 16; // NOTE: KBDBREAK == KF_UP

// TODO:
// - If we are handling Alt+Numpad (whatever mode) and we have a numpad press,
//   don't translate the key.
// - If we are handling Alt+Numpad in hexmode and we have a 0-9 or A-F press,
//   don't translate the key.
    if (IntIsAltNumpadKey(wVirtKey))
        return FALSE;

    /* Check whether we translate an Alt+Numpad key on ALT key release
     * (we do this whatever the TM_POSTCHARBREAKS bit is set or not) */
    if ((wScanCode & KBDBREAK) && (wVirtKey == VK_MENU))
    {
ERR("IntTranslateChar: Calling IntTranslateAltNumpad\n");
        if (IntTranslateAltNumpad(pwcTranslatedChar, plParamExtraFlags,
                                  wVirtKey, !(wScanCode & KBDBREAK)))
        {
            *pbDead = FALSE;
            *pbLigature = FALSE;
            return (*pwcTranslatedChar ? TRUE : FALSE);
        }
        // else, regular ALT break.
    }

    /* Don't translate key breaks (key up), unless TM_POSTCHARBREAKS is set */
    if ((wScanCode & KBDBREAK) && !(wFlags & TM_POSTCHARBREAKS))
        return FALSE;

    /* If set, Alt+Numpad combinations are not handled */
    if (!(wScanCode & KBDBREAK) && (wFlags & TM_MENUMODE))
    {
ERR("IntTranslateChar: resetting AltNumpad state\n");
        /* Reset the Alt+Numpad state */ // FIXME ??
        gAltNumPadState = ALTNUM_INACTIVE;
        gAltNumPadValue = 0;
    }

    // NOTE: If the actual wScanCode is required below in the future,
    // filter out any flags in it:
    // wScanCode &= 0xFF;

    /* If ALT without CTRL has been used, remove ALT flag */
    if ((dwModBits & (KBDALT|KBDCTRL)) == KBDALT)
        dwModBits &= ~KBDALT;

    if (dwModBits > pKbdTbl->pCharModifiers->wMaxModBits)
    {
        TRACE("dwModBits %x > wMaxModBits %x\n",
              dwModBits, pKbdTbl->pCharModifiers->wMaxModBits);
        return FALSE;
    }

    for (i = 0; pKbdTbl->pVkToWcharTable[i].pVkToWchars; i++)
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
                    pKeyState &&
                    IS_KEY_LOCKED(pKeyState, VK_CAPITAL))
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
                    TRACE("dwModNumber %u >= nModifications %u\n", dwModNumber, pVkToVchTbl->nModifications);
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
                        WARN("Found dead key with no trailer in the table.\n");
                        WARN("VK: %04x, ADDR: %p\n", wVirtKey, pVkToVch);
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
static
int APIENTRY
IntToUnicodeEx(
    _In_ UINT wVirtKey,
    _In_ UINT wScanCode,
    _In_ PBYTE pKeyState,
    _Out_ LPARAM* plParamExtraFlags,
    _Out_writes_(cchBuff) LPWSTR pwszBuff,
    _In_ int cchBuff,
    _In_ UINT wFlags,
    _In_ PKBDTABLES pKbdTbl)
{
    WCHAR wchTranslatedChar;
    BOOL bDead, bLigature;
    static WCHAR wchDead = 0;
    int iRet = 0;

    ASSERT(pKbdTbl);

    if (!IntTranslateChar(wVirtKey,
                          wScanCode,
                          pKeyState,
                          &bDead,
                          &bLigature,
                          &wchTranslatedChar,
                          plParamExtraFlags,
                          wFlags,
                          pKbdTbl))
    {
        return 0;
    }

    if (bLigature)
    {
        WARN("Not handling ligature (yet)\n" );
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
            for (i = 0; pKbdTbl->pDeadKey[i].dwBoth; i++)
            {
                wchFirst = pKbdTbl->pDeadKey[i].dwBoth >> 16;
                wchSecond = pKbdTbl->pDeadKey[i].dwBoth & 0xFFFF;
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
#if defined(__GNUC__)
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
 * IntVkToVsc
 *
 * Translates virtual key to scan code
 */
static
WORD FASTCALL
IntVkToVsc(WORD wVk, PKBDTABLES pKbdTbl)
{
    unsigned i;

    ASSERT(pKbdTbl);

    /* Check standard keys first */
    for (i = 0; i < pKbdTbl->bMaxVSCtoVK; i++)
    {
        if ((pKbdTbl->pusVSCtoVK[i] & 0xFF) == wVk)
            return i;
    }

    /* Check extended keys now */
    for (i = 0; pKbdTbl->pVSCtoVK_E0[i].Vsc; i++)
    {
        if ((pKbdTbl->pVSCtoVK_E0[i].Vk & 0xFF) == wVk)
            return 0xE000 | pKbdTbl->pVSCtoVK_E0[i].Vsc;
    }

    for (i = 0; pKbdTbl->pVSCtoVK_E1[i].Vsc; i++)
    {
        if ((pKbdTbl->pVSCtoVK_E1[i].Vk & 0xFF) == wVk)
            return 0xE100 | pKbdTbl->pVSCtoVK_E1[i].Vsc;
    }

    /* Virtual key has not been found */
    return 0;
}

/*
 * IntVscToVk
 *
 * Translates prefixed scancode to virtual key
 */
static
WORD FASTCALL
IntVscToVk(WORD wScanCode, PKBDTABLES pKbdTbl)
{
    unsigned i;
    WORD wVk = 0;

    ASSERT(pKbdTbl);

    if ((wScanCode & 0xFF00) == 0xE000)
    {
        for (i = 0; pKbdTbl->pVSCtoVK_E0[i].Vsc; i++)
        {
            if (pKbdTbl->pVSCtoVK_E0[i].Vsc == (wScanCode & 0xFF))
            {
                wVk = pKbdTbl->pVSCtoVK_E0[i].Vk;
            }
        }
    }
    else if ((wScanCode & 0xFF00) == 0xE100)
    {
        for (i = 0; pKbdTbl->pVSCtoVK_E1[i].Vsc; i++)
        {
            if (pKbdTbl->pVSCtoVK_E1[i].Vsc == (wScanCode & 0xFF))
            {
                wVk = pKbdTbl->pVSCtoVK_E1[i].Vk;
            }
        }
    }
    else if (wScanCode < pKbdTbl->bMaxVSCtoVK)
    {
        wVk = pKbdTbl->pusVSCtoVK[wScanCode];
    }

    /* 0xFF nad 0x00 are invalid VKs */
    return wVk != 0xFF ? wVk : 0;
}

/*
 * IntVkToChar
 *
 * Translates virtual key to character, ignoring shift state
 */
static
WCHAR FASTCALL
IntVkToChar(WORD wVk, PKBDTABLES pKbdTbl)
{
    WCHAR wch;
    BOOL bDead, bLigature;
    LPARAM lfDummy;

    ASSERT(pKbdTbl);

    if (IntTranslateChar(wVk,
                         0,
                         NULL,
                         &bDead,
                         &bLigature,
                         &wch,
                         &lfDummy,
                         0,
                         pKbdTbl))
    {
        return wch;
    }

    return 0;
}

/*
 * NtUserGetAsyncKeyState
 *
 * Gets key state from global bitmap
 */
SHORT
APIENTRY
NtUserGetAsyncKeyState(INT Key)
{
    WORD wRet = 0;

    TRACE("Enter NtUserGetAsyncKeyState\n");

    if (Key >= 0x100 || Key < 0)
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
        ERR("Invalid parameter Key\n");
        return 0;
    }

    UserEnterExclusive();

    if (IS_KEY_DOWN(gafAsyncKeyState, Key))
        wRet |= 0x8000; // If down, windows returns 0x8000.
    if (gafAsyncKeyStateRecentDown[Key / 8] & (1 << (Key % 8)))
        wRet |= 0x1;
    gafAsyncKeyStateRecentDown[Key / 8] &= ~(1 << (Key % 8));

    UserLeave();

    TRACE("Leave NtUserGetAsyncKeyState, ret=%u\n", wRet);
    return wRet;
}

/*
 * UpdateAsyncKeyState
 *
 * Updates gafAsyncKeyState array
 */
static
VOID NTAPI
UpdateAsyncKeyState(WORD wVk, BOOL bIsDown)
{
    if (bIsDown)
    {
        /* If it's first key down event, xor lock bit */
        if (!IS_KEY_DOWN(gafAsyncKeyState, wVk))
            SET_KEY_LOCKED(gafAsyncKeyState, wVk, !IS_KEY_LOCKED(gafAsyncKeyState, wVk));

        SET_KEY_DOWN(gafAsyncKeyState, wVk, TRUE);
        gafAsyncKeyStateRecentDown[wVk / 8] |= (1 << (wVk % 8));
    }
    else
        SET_KEY_DOWN(gafAsyncKeyState, wVk, FALSE);
}

/*
 * co_CallLowLevelKeyboardHook
 *
 * Calls WH_KEYBOARD_LL hook
 */
static LRESULT
co_CallLowLevelKeyboardHook(WORD wVk, WORD wScanCode, DWORD dwFlags, BOOL bInjected, DWORD dwTime, DWORD dwExtraInfo)
{
    KBDLLHOOKSTRUCT KbdHookData;
    UINT uMsg;

    KbdHookData.vkCode = wVk;
    KbdHookData.scanCode = wScanCode;
    KbdHookData.flags = 0;
    if (dwFlags & KEYEVENTF_EXTENDEDKEY)
        KbdHookData.flags |= LLKHF_EXTENDED;
    if (IS_KEY_DOWN(gafAsyncKeyState, VK_MENU))
        KbdHookData.flags |= LLKHF_ALTDOWN;
    if (dwFlags & KEYEVENTF_KEYUP)
        KbdHookData.flags |= LLKHF_UP;
    if (bInjected)
        KbdHookData.flags |= LLKHF_INJECTED;
    KbdHookData.time = dwTime;
    KbdHookData.dwExtraInfo = dwExtraInfo;

    /* Note: it doesn't support WM_SYSKEYUP */
    if (dwFlags & KEYEVENTF_KEYUP)
        uMsg = WM_KEYUP;
    else if (IS_KEY_DOWN(gafAsyncKeyState, VK_MENU) && !IS_KEY_DOWN(gafAsyncKeyState, VK_CONTROL))
        uMsg = WM_SYSKEYDOWN;
    else
        uMsg = WM_KEYDOWN;

    return co_HOOK_CallHooks(WH_KEYBOARD_LL, HC_ACTION, uMsg, (LPARAM)&KbdHookData);
}

/*
 * SnapWindow
 *
 * Saves snapshot of specified window or whole screen in the clipboard
 */
static VOID
SnapWindow(HWND hWnd)
{
    HBITMAP hbm = NULL, hbmOld;
    HDC hdc = NULL, hdcMem;
    SETCLIPBDATA scd;
    INT cx, cy;
    PWND pWnd = NULL;

    TRACE("SnapWindow(%p)\n", hWnd);

    /* If no windows is given, make snapshot of desktop window */
    if (!hWnd)
        hWnd = IntGetDesktopWindow();

    pWnd = UserGetWindowObject(hWnd);
    if (!pWnd)
    {
        ERR("Invalid window\n");
        goto cleanup;
    }

    hdc = UserGetDCEx(pWnd, NULL, DCX_USESTYLE | DCX_WINDOW);
    if (!hdc)
    {
        ERR("UserGetDCEx failed!\n");
        goto cleanup;
    }

    cx = pWnd->rcWindow.right - pWnd->rcWindow.left;
    cy = pWnd->rcWindow.bottom - pWnd->rcWindow.top;

    hbm = NtGdiCreateCompatibleBitmap(hdc, cx, cy);
    if (!hbm)
    {
        ERR("NtGdiCreateCompatibleBitmap failed!\n");
        goto cleanup;
    }

    hdcMem = NtGdiCreateCompatibleDC(hdc);
    if (!hdcMem)
    {
        ERR("NtGdiCreateCompatibleDC failed!\n");
        goto cleanup;
    }

    hbmOld = NtGdiSelectBitmap(hdcMem, hbm);
    NtGdiBitBlt(hdcMem, 0, 0, cx, cy, hdc, 0, 0, SRCCOPY, CLR_INVALID, 0);
    NtGdiSelectBitmap(hdcMem, hbmOld);
    IntGdiDeleteDC(hdcMem, FALSE);

    /* Save snapshot in clipboard */
    if (UserOpenClipboard(NULL))
    {
        UserEmptyClipboard();
        scd.fIncSerialNumber = TRUE;
        scd.fGlobalHandle = FALSE;
        if (UserSetClipboardData(CF_BITMAP, hbm, &scd))
        {
            /* Bitmap is managed by system now */
            hbm = NULL;
        }
        UserCloseClipboard();
    }

cleanup:
    if (hbm)
        GreDeleteObject(hbm);
    if (hdc)
        UserReleaseDC(pWnd, hdc, FALSE);
}

/* Find the next/previous keyboard layout of the same/different language */
static PKL FASTCALL
IntGetNextKL(
    _In_ PKL pKL,
    _In_ BOOL bNext,
    _In_ BOOL bSameLang)
{
    PKL pFirstKL = pKL;
    LANGID LangID = LOWORD(pKL->hkl);

    do
    {
        pKL = (bNext ? pKL->pklNext : pKL->pklPrev);

        if (!(pKL->dwKL_Flags & KL_UNLOAD) && bSameLang == (LangID == LOWORD(pKL->hkl)))
            return pKL;
    } while (pKL != pFirstKL);

    return pFirstKL;
}

/* Perform layout toggle by [Left Alt]+Shift or Ctrl+Shift */
static VOID
IntLanguageToggle(
    _In_ PUSER_MESSAGE_QUEUE pFocusQueue,
    _In_ BOOL bSameLang,
    _In_ INT nKeyState)
{
    PWND pWnd;
    PTHREADINFO pti;
    PKL pkl;
    WPARAM wParam = 0;

    if (!pFocusQueue)
    {
        ERR("IntLanguageToggle(): NULL pFocusQueue\n");
        return;
    }
    pWnd = pFocusQueue->spwndFocus;
    if (!pWnd)
        pWnd = pFocusQueue->spwndActive;
    if (!pWnd)
        return;

    pti = pWnd->head.pti;
    pkl = pti->KeyboardLayout;

    if (nKeyState == INPUTLANGCHANGE_FORWARD)
        pkl = IntGetNextKL(pkl, TRUE, bSameLang);
    else if (nKeyState == INPUTLANGCHANGE_BACKWARD)
        pkl = IntGetNextKL(pkl, FALSE, bSameLang);

    if (gSystemFS & pkl->dwFontSigs)
        wParam |= INPUTLANGCHANGE_SYSCHARSET;

    UserPostMessage(UserHMGetHandle(pWnd), WM_INPUTLANGCHANGEREQUEST, wParam, (LPARAM)pkl->hkl);
}

/* Check Language Toggle by [Left Alt]+Shift or Ctrl+Shift */
static BOOL
IntCheckLanguageToggle(
    _In_ PUSER_MESSAGE_QUEUE pFocusQueue,
    _In_ BOOL bIsDown,
    _In_ WORD wVk,
    _Inout_ PINT pKeyState)
{
    if (bIsDown) /* Toggle key combination is pressed? */
    {
        if (wVk == VK_LSHIFT)
            *pKeyState = INPUTLANGCHANGE_FORWARD;
        else if (wVk == VK_RSHIFT)
            *pKeyState = INPUTLANGCHANGE_BACKWARD;
        else if (!wVk && IS_KEY_DOWN(gafAsyncKeyState, VK_LSHIFT))
            *pKeyState = INPUTLANGCHANGE_FORWARD;
        else if (!wVk && IS_KEY_DOWN(gafAsyncKeyState, VK_RSHIFT))
            *pKeyState = INPUTLANGCHANGE_BACKWARD;
        else
            return FALSE;
    }
    else
    {
        if (*pKeyState == 0)
            return FALSE;

        IntLanguageToggle(pFocusQueue, (pKeyState == &gLayoutToggleKeyState), *pKeyState);
        *pKeyState = 0;
    }
    return TRUE;
}

/**
 * @brief   Handles Alt+Numpad character composition.
 *
 * @param[in]   wVk
 * The virtual key code of the key being input, in its "simplified"
 * shift-less version, as given by IntSimplifyVk().
 *
 * @param[in]   wScanCode
 * Keyboard scan code corresponding to the virtual key.
 * (HACK for the WM_CHAR message sent by the function.)
 *
 * @param[in]   bIsDown
 * TRUE if the current key is being pressed; FALSE if it is released.
 *
 * @return
 * TRUE if no further key processing needs to be done;
 * FALSE if regular key processing has to be done by the caller.
 **/
static BOOL
IntHandleAltNumpad(
    _In_ WORD wVk,
    _In_ WORD wScanCode,
    _In_ BOOL bIsDown,
    _In_ DWORD dwTime /*,
    _In_ PUSER_MESSAGE_QUEUE pFocusQueue */)
{
    if (bIsDown &&
        IS_KEY_DOWN(gafAsyncKeyState, VK_MENU) &&
        !IS_KEY_DOWN(gafAsyncKeyState, VK_CONTROL))
    {
        TRACE2("VK_MENU && !VK_CONTROL - wVk: 0x%04x\n", wVk);

        /* Initialize the Alt+Numpad state if necessary */
        if (gAltNumPadState == ALTNUM_INACTIVE)
        {
            if (gbEnableHexNumpad && (wVk == VK_DECIMAL)) // || (wVk == VK_OEM_PERIOD)
                gAltNumPadState = ALTNUM_HEX_ACP, wVk = VK_NUMPAD0; // Replace '.' by '0'
            else if (gbEnableHexNumpad && (wVk == VK_ADD)) // || (wVk == VK_OEM_PLUS)
                gAltNumPadState = ALTNUM_HEX_UTF, wVk = VK_NUMPAD0; // Replace '+' by '0'
            else if (wVk >= VK_NUMPAD0 && wVk <= VK_NUMPAD9) // || (wVk >= '0' && wVk <= '9')
                gAltNumPadState = (wVk == VK_NUMPAD0) ? ALTNUM_ACP : ALTNUM_OEM;
            else
                return FALSE; /* Unhandled, do regular key processing */

            /* Reset the stashed Alt+Numpad value */
            gAltNumPadValue = 0;
        }

        /* Check the incoming key */
        switch (gAltNumPadState)
        {
            case ALTNUM_OEM: case ALTNUM_ACP:
            {
                UINT uDigit;

                /* Check if it is a numpad digit */
                if (wVk >= VK_NUMPAD0 && wVk <= VK_NUMPAD9)
                    uDigit = wVk - VK_NUMPAD0;
                // else if (wVk >= '0' && wVk <= '9')
                //     uDigit = wVk - '0';
                else
                    break;

                /* Build the decimal value; the value can overflow
                 * and be truncated (same behaviour as on Windows) */
                gAltNumPadValue = (gAltNumPadValue * 10) + uDigit;
                return TRUE; /* No key processing needs to be done */
            }
            case ALTNUM_HEX_ACP: case ALTNUM_HEX_UTF:
            {
                UINT uDigit;

                /* Check if it represents a valid hexadecimal digit */
                if (wVk >= VK_NUMPAD0 && wVk <= VK_NUMPAD9)
                    uDigit = wVk - VK_NUMPAD0;
                else if (wVk >= '0' && wVk <= '9')
                    uDigit = wVk - '0';
                else if (wVk >= 'A' && wVk <= 'F')
                    uDigit = wVk - 'A' + 10;
                else
                    break;

                /* Build the hexadecimal value; the value can overflow
                 * and be truncated (same behaviour as on Windows) */
                gAltNumPadValue = (gAltNumPadValue * 16) + uDigit;
                return TRUE; /* No key processing needs to be done */
            }
            DEFAULT_UNREACHABLE;
        }

        /* If the incoming key is not the ALT key, reset the Alt+Numpad state */
        if (wVk != VK_MENU)
        {
            gAltNumPadState = ALTNUM_INACTIVE;
            gAltNumPadValue = 0;
        }
    }
    TRACE2("gAltNumPadState: %lu, gAltNumPadValue: %lu\n",
          gAltNumPadState, gAltNumPadValue);

    /* Check for the end of an Alt+Numpad sequence, triggered by the ALT key release */
    if ((gAltNumPadState != ALTNUM_INACTIVE) && !bIsDown && (wVk == VK_MENU))
    {
        // PUSER_MESSAGE_QUEUE pFocusQueue = IntGetFocusMessageQueue();
        // PTHREADINFO pti;

        TRACE2("End of Alt+Numpad\n");
#if 0
        if (gAltNumPadValue != 0 && pFocusQueue /*&& pFocusQueue->ptiKeyboard*/)
        {
            PWND pWnd;
            NTSTATUS Status;
            WCHAR wchUnicodeChar;
            LPARAM lCharFlags = 0;

            pWnd = pFocusQueue->spwndFocus;
            if (!pWnd /*&& pFocusQueue->spwndActive*/)
                pWnd = pFocusQueue->spwndActive;
            //if (!pWnd)
            //    return;

            pti = pFocusQueue->ptiKeyboard;
            if (pWnd)
                pti = pWnd->head.pti;

            /* Convert the input codepoint value to UTF-16 */
            if (gAltNumPadState == ALTNUM_HEX_UTF)
            {
                /* Convert the Unicode codepoint to UTF-16 */
                // FIXME: We currently support only the Basic Multilingual Plane
                // (equivalent to UCS-2) and don't exclude the surrogate range
                // (U+D800 to U+DFFF). In the future, we should handle all the
                // valid values, generating two UTF-16 code units if necessary
                // so as to support the other Unicode planes.
                // See https://en.wikipedia.org/wiki/UTF-16#Description
                wchUnicodeChar = (WCHAR)(gAltNumPadValue & 0xFFFF);
                Status = STATUS_SUCCESS;
            }
            else if ((gAltNumPadState == ALTNUM_OEM) && pWnd &&
                     ((pWnd->head.pti->TIF_flags & TIF_CSRSSTHREAD) &&
                      (pWnd->pcls->atomClassName == gaGuiConsoleWndClass)))
            {
                /*
                 * Directly forward the raw OEM character value with the
                 * ALTNUMPAD_BIT set to the console for Unicode conversion,
                 * because we do not know its active OEM code page.
                 * For more details, see:
                 * https://github.com/microsoft/terminal/blob/e20e1f7bf92b61580baea69483a74a7f4578a586/src/host/stream.cpp#L170
                 */
ERR("Send OEM key to Console\n");
                lCharFlags |= ALTNUMPAD_BIT;
                wchUnicodeChar = (WCHAR)(gAltNumPadValue & 0xFFFF);
                Status = STATUS_SUCCESS;
            }
            else
            {
                NTSTATUS (NTAPI* pRtlCPToUnicodeN)(
                    _Out_writes_bytes_to_(MaxBytesInUnicodeString, *BytesInUnicodeString) PWCH UnicodeString,
                    _In_ ULONG MaxBytesInUnicodeString,
                    _Out_opt_ PULONG BytesInUnicodeString,
                    _In_reads_bytes_(BytesInString) PCCH String,
                    _In_ ULONG BytesInString);

                USHORT AnsiCP, OemCP, wCodePage, mbChar;

                /* Check the current codepage and select a conversion function.
                 * NOTE: Windows WIN32K invokes the ClientCharToWchar
                 * USER32 callback to perform the conversion. */
                RtlGetDefaultCodePage(&AnsiCP, &OemCP);
                if (gAltNumPadState == ALTNUM_OEM)
                {
                    /* Use the current OEM codepage -> Unicode function */
                    wCodePage = OemCP;
                    pRtlCPToUnicodeN = RtlOemToUnicodeN;
                }
                else // if ((gAltNumPadState == ALTNUM_ACP) ||
                     //     (gAltNumPadState == ALTNUM_HEX_ACP))
                {
                    /* Use the current ANSI codepage (ACP) MultiByte -> Unicode function */
                    wCodePage = AnsiCP;
                    pRtlCPToUnicodeN = RtlMultiByteToUnicodeN;
                }

                /* For CJK codepages, keep the input value on 2 bytes,
                 * otherwise truncate it to 1 byte for legacy behaviour. */
                if (IsCJKCodePage(wCodePage))
                {
                    mbChar = (USHORT)(gAltNumPadValue & 0xFFFF);
                }
                else
                {
                    /*
                     * NOTE: the input value is considered modulo 256 as it is
                     * stored in a 1-byte CHAR. Other input systems that hook and
                     * re-implement the Alt+Numpad system (e.g. RichEdit, ...)
                     * store instead the value in a 2-byte WORD, i.e. consider
                     * the value modulo 65536.
                     * See: https://devblogs.microsoft.com/oldnewthing/20240702-00/?p=109951
                     */
                    mbChar = (USHORT)(gAltNumPadValue & 0xFF);
                }

                Status = pRtlCPToUnicodeN(&wchUnicodeChar,
                                          sizeof(wchUnicodeChar),
                                          NULL,
                                          (PCCH)&mbChar,
                                          sizeof(mbChar));
            }

            /* Post the Unicode character to the focused message queue if conversion succeeded */
            if (NT_SUCCESS(Status))
            {
                // FIXME: This should be done by IntTranslateKbdMessage/IntTranslateChar,
                // as part of TranslateMessage.
                MSG msgChar;
                msgChar.hwnd = pWnd ? UserHMGetHandle(pWnd) : NULL;
                msgChar.message = WM_CHAR;
                msgChar.wParam = wchUnicodeChar;

                // FIXME: See also the 'if (!bPacket)' case in ProcessKeyEvent.
                msgChar.lParam = MAKELPARAM(1, wScanCode) | lCharFlags;
                //if (bExt)
                //    msgChar.lParam |= KF_EXTENDED << 16;
                if (IS_KEY_DOWN(gafAsyncKeyState, VK_MENU))
                    msgChar.lParam |= KF_ALTDOWN << 16;
                //if (bWasSimpleDown)
                //    msgChar.lParam |= KF_REPEAT << 16;
                if (!bIsDown)
                    msgChar.lParam |= KF_UP << 16;
                /* FIXME: Set KF_DLGMODE and KF_MENUMODE when needed */
                if (pFocusQueue->QF_flags & QF_DIALOGACTIVE)
                    msgChar.lParam |= KF_DLGMODE << 16;
                if (pFocusQueue->MenuOwner) // (pti->pMenuState && pti->pMenuState->fMenuStarted)
                    msgChar.lParam |= KF_MENUMODE << 16;

                msgChar.time = dwTime;
                msgChar.pt = gpsi->ptCursor;

                MsqPostMessage(pti, &msgChar, FALSE, QS_KEY, 0, 0);
            }
        }
#else
        /* Stash the current Alt+Numpad state and value for character translation */
        /* Keep the stashed Alt+Numpad value for character translation */
        gPrevAltNumPadState = gAltNumPadState;
        gPrevAltNumPadValue = gAltNumPadValue;
#endif

        /* Reset the Alt+Numpad state */
        gAltNumPadState = ALTNUM_INACTIVE;
        gAltNumPadValue = 0;
    }

    return FALSE; /* More key processing has to be done */
}

/**
 * @brief
 * Translate the Alt+Numpad character being composed, on ALT key release.
 * Invoked by IntTranslateChar().
 *
 * @param[in]   wVk
 * The virtual key code of the key being input, in its "simplified"
 * shift-less version, as given by IntSimplifyVk().
 *
 * @param[in]   bIsDown
 * TRUE if the current key is being pressed; FALSE if it is released.
 *
 * @return
 * TRUE if no further key processing needs to be done;
 * FALSE if regular key processing has to be done by the caller.
 **/
static BOOL
IntTranslateAltNumpad(
    _Out_ PWCHAR pwcTranslatedChar,
    _Out_ LPARAM* plParamExtraFlags,
    _In_ WORD wVk,
    _In_ BOOL bIsDown)
{
    BOOL bSuccess = FALSE;

    *pwcTranslatedChar = UNICODE_NULL;
    *plParamExtraFlags = 0;

    /* Check for the end of an Alt+Numpad sequence, triggered by the ALT key release */
    if (/*(gPrevAltNumPadState != ALTNUM_INACTIVE)*/ (gPrevAltNumPadValue != 0) &&
        !bIsDown && (wVk == VK_MENU))
    {
        PUSER_MESSAGE_QUEUE pFocusQueue = IntGetFocusMessageQueue();

        TRACE2("Translate Alt+Numpad:\n"
               "gAltNumPadState: %lu, gAltNumPadValue: %lu\n"
               "gPrevAltNumPadState: %lu, gPrevAltNumPadValue: %lu\n",
               gAltNumPadState, gAltNumPadValue,
               gPrevAltNumPadState, gPrevAltNumPadValue);

        if (gPrevAltNumPadValue != 0 && pFocusQueue)
        {
            PWND pWnd;
            NTSTATUS Status;
            WCHAR wchUnicodeChar;
            LPARAM lCharFlags = 0;

            pWnd = pFocusQueue->spwndFocus;
            if (!pWnd /*&& pFocusQueue->spwndActive*/)
                pWnd = pFocusQueue->spwndActive;
            //if (!pWnd)
            //    return FALSE;

            /* Convert the input codepoint value to UTF-16 */
            if (gPrevAltNumPadState == ALTNUM_HEX_UTF)
            {
                /* Convert the Unicode codepoint to UTF-16 */
                // FIXME: We currently support only the Basic Multilingual Plane
                // (equivalent to UCS-2) and don't exclude the surrogate range
                // (U+D800 to U+DFFF). In the future, we should handle all the
                // valid values, generating two UTF-16 code units if necessary
                // so as to support the other Unicode planes.
                // See https://en.wikipedia.org/wiki/UTF-16#Description
                wchUnicodeChar = (WCHAR)(gPrevAltNumPadValue & 0xFFFF);
                Status = STATUS_SUCCESS;
            }
            else if ((gPrevAltNumPadState == ALTNUM_OEM) && pWnd &&
                     ((pWnd->head.pti->TIF_flags & TIF_CSRSSTHREAD) &&
                      (pWnd->pcls->atomClassName == gaGuiConsoleWndClass)))
            {
                /*
                 * Directly forward the raw OEM character value with the
                 * ALTNUMPAD_BIT set to the console for Unicode conversion,
                 * because we do not know its active OEM code page.
                 * For more details, see:
                 * https://github.com/microsoft/terminal/blob/e20e1f7bf92b61580baea69483a74a7f4578a586/src/host/stream.cpp#L170
                 */
ERR("Send OEM key to Console\n");
                lCharFlags |= ALTNUMPAD_BIT;
                wchUnicodeChar = (WCHAR)(gPrevAltNumPadValue & 0xFFFF);
                Status = STATUS_SUCCESS;
            }
            else
            {
                NTSTATUS (NTAPI* pRtlCPToUnicodeN)(
                    _Out_writes_bytes_to_(MaxBytesInUnicodeString, *BytesInUnicodeString) PWCH UnicodeString,
                    _In_ ULONG MaxBytesInUnicodeString,
                    _Out_opt_ PULONG BytesInUnicodeString,
                    _In_reads_bytes_(BytesInString) PCCH String,
                    _In_ ULONG BytesInString);

                USHORT AnsiCP, OemCP, wCodePage, mbChar;

                /* Check the current codepage and select a conversion function.
                 * NOTE: Windows WIN32K invokes the ClientCharToWchar
                 * USER32 callback to perform the conversion. */
                RtlGetDefaultCodePage(&AnsiCP, &OemCP);
                if (gPrevAltNumPadState == ALTNUM_OEM)
                {
                    /* Use the current OEM codepage -> Unicode function */
                    wCodePage = OemCP;
                    pRtlCPToUnicodeN = RtlOemToUnicodeN;
                }
                else // if ((gPrevAltNumPadState == ALTNUM_ACP) ||
                     //     (gPrevAltNumPadState == ALTNUM_HEX_ACP))
                {
                    /* Use the current ANSI codepage (ACP) MultiByte -> Unicode function */
                    wCodePage = AnsiCP;
                    pRtlCPToUnicodeN = RtlMultiByteToUnicodeN;
                }

                /* For CJK codepages, keep the input value on 2 bytes,
                 * otherwise truncate it to 1 byte for legacy behaviour. */
                if (IsCJKCodePage(wCodePage))
                {
                    mbChar = (USHORT)(gPrevAltNumPadValue & 0xFFFF);
                }
                else
                {
                    /*
                     * NOTE: the input value is considered modulo 256 as it is
                     * stored in a 1-byte CHAR. Other input systems that hook and
                     * re-implement the Alt+Numpad system (e.g. RichEdit, ...)
                     * store instead the value in a 2-byte WORD, i.e. consider
                     * the value modulo 65536.
                     * See: https://devblogs.microsoft.com/oldnewthing/20240702-00/?p=109951
                     */
                    mbChar = (USHORT)(gPrevAltNumPadValue & 0xFF);
                }

                Status = pRtlCPToUnicodeN(&wchUnicodeChar,
                                          sizeof(wchUnicodeChar),
                                          NULL,
                                          (PCCH)&mbChar,
                                          sizeof(mbChar));
            }

            /* Return the translated Unicode character if conversion succeeded */
            if (NT_SUCCESS(Status))
            {
                *pwcTranslatedChar = wchUnicodeChar;
                *plParamExtraFlags = lCharFlags;
                bSuccess = TRUE;
            }
        }

        /* Reset the Alt+Numpad stashed state */
        gPrevAltNumPadState = ALTNUM_INACTIVE;
        gPrevAltNumPadValue = 0;
    }

    return bSuccess;
}

/*
 * UserSendKeyboardInput
 *
 * Process keyboard input from input devices and SendInput API
 */
BOOL NTAPI
ProcessKeyEvent(WORD wVk, WORD wScanCode, DWORD dwFlags, BOOL bInjected, DWORD dwTime, DWORD dwExtraInfo)
{
    WORD wSimpleVk, wFixedVk, wVk2;
    PUSER_MESSAGE_QUEUE pFocusQueue;
    PTHREADINFO pti;
    BOOL bExt = (dwFlags & KEYEVENTF_EXTENDEDKEY) ? TRUE : FALSE;
    BOOL bIsDown = (dwFlags & KEYEVENTF_KEYUP) ? FALSE : TRUE;
    BOOL bPacket = (dwFlags & KEYEVENTF_UNICODE) ? TRUE : FALSE;
    BOOL bWasSimpleDown = FALSE, bPostMsg = TRUE, bIsSimpleDown;
    MSG Msg;
    static BOOL bMenuDownRecently = FALSE;
    BOOL bLangToggled = FALSE;

    /* Get virtual key without shifts (VK_(L|R)* -> VK_*) */
    wSimpleVk = IntSimplifyVk(wVk);

    if (PRIMARYLANGID(gusLanguageID) == LANG_JAPANESE)
    {
        /* Japanese special! */
        if (IS_KEY_DOWN(gafAsyncKeyState, VK_SHIFT))
        {
            if (wSimpleVk == VK_OEM_ATTN)
                wSimpleVk = VK_CAPITAL;
            else if (wSimpleVk == VK_OEM_COPY)
                wSimpleVk = VK_OEM_FINISH;
        }
    }

    /* Handle Alt+Numpad character composition */
    if (IntHandleAltNumpad(wSimpleVk, wScanCode, bIsDown, dwTime))
        NOTHING; // return TRUE;

    bWasSimpleDown = IS_KEY_DOWN(gafAsyncKeyState, wSimpleVk);

    /* Update key without shifts */
    wVk2 = IntFixVk(wSimpleVk, !bExt);
    bIsSimpleDown = bIsDown || IS_KEY_DOWN(gafAsyncKeyState, wVk2);
    UpdateAsyncKeyState(wSimpleVk, bIsSimpleDown);

    if (bIsDown)
    {
        /* Update keyboard LEDs */
        IntKeyboardUpdateLeds(ghKeyboardDevice,
                              wSimpleVk,
                              wScanCode);
    }

    /* Call WH_KEYBOARD_LL hook */
    if (co_CallLowLevelKeyboardHook(wVk, wScanCode, dwFlags, bInjected, dwTime, dwExtraInfo))
    {
        ERR("Kbd msg dropped by WH_KEYBOARD_LL hook\n");
        bPostMsg = FALSE;
    }

    /* Check if this is a hotkey */
    if (co_UserProcessHotKeys(wSimpleVk, bIsDown)) //// Check if this is correct, refer to hotkey sequence message tests.
    {
        TRACE("HotKey Processed\n");
        bPostMsg = FALSE;
    }

    wFixedVk = IntFixVk(wSimpleVk, bExt); /* LSHIFT + EXT = RSHIFT */
    if (wSimpleVk == VK_SHIFT) /* shift can't be extended */
        bExt = FALSE;

    /* If we have a focus queue, post a keyboard message */
    pFocusQueue = IntGetFocusMessageQueue();
    TRACE("ProcessKeyEvent Q 0x%p Active pWnd 0x%p Focus pWnd 0x%p\n",
          pFocusQueue,
          (pFocusQueue ? pFocusQueue->spwndActive : NULL),
          (pFocusQueue ? pFocusQueue->spwndFocus : NULL));

    /* If it is F10 or ALT is down and CTRL is up, it's a system key */
    if ( wVk == VK_F10 ||
        (wSimpleVk == VK_MENU && bMenuDownRecently) ||
        (IS_KEY_DOWN(gafAsyncKeyState, VK_MENU) &&
        !IS_KEY_DOWN(gafAsyncKeyState, VK_CONTROL)) ||
         // See MSDN WM_SYSKEYDOWN/UP fixes last wine Win test_keyboard_input.
        (pFocusQueue && !pFocusQueue->spwndFocus) )
    {
        bMenuDownRecently = FALSE; // reset
        if (bIsDown)
        {
            Msg.message = WM_SYSKEYDOWN;
            if (wSimpleVk == VK_MENU)
            {
                // Note: If only LALT is pressed WM_SYSKEYUP is generated instead of WM_KEYUP
                bMenuDownRecently = TRUE;
            }
        }
        else
            Msg.message = WM_SYSKEYUP;
    }
    else
    {
        if (bIsDown)
            Msg.message = WM_KEYDOWN;
        else
            Msg.message = WM_KEYUP;
    }

    /* Update async state of not simplified vk here.
       See user32_apitest:GetKeyState */
    UpdateAsyncKeyState(wFixedVk, bIsDown);

    /* Alt-Tab/Esc Check. Use FocusQueue or RIT Queue */
    if (bIsSimpleDown && !bWasSimpleDown &&
        IS_KEY_DOWN(gafAsyncKeyState, VK_MENU) &&
        !IS_KEY_DOWN(gafAsyncKeyState, VK_CONTROL) &&
        (wVk == VK_ESCAPE || wVk == VK_TAB))
    {
       TRACE("Alt-Tab/Esc Pressed wParam %x\n",wVk);
    }

    /*
     * Check Language/Layout Toggle by [Left Alt]+Shift or Ctrl+Shift.
     * @see https://learn.microsoft.com/en-us/previous-versions/windows/it-pro/windows-2000-server/cc976564%28v=technet.10%29
     */
    if (gdwLanguageToggleKey == 1 || gdwLanguageToggleKey == 2)
    {
        if (wSimpleVk == VK_SHIFT) /* Shift key is pressed or released */
        {
            UINT targetKey = ((gdwLanguageToggleKey == 1) ? VK_LMENU : VK_CONTROL);
            if (IS_KEY_DOWN(gafAsyncKeyState, targetKey))
                bLangToggled = IntCheckLanguageToggle(pFocusQueue, bIsDown, wVk, &gLanguageToggleKeyState);
        }
        else if ((wSimpleVk == VK_MENU && gdwLanguageToggleKey == 1) ||
                 (wSimpleVk == VK_CONTROL && gdwLanguageToggleKey == 2))
        {
            if (IS_KEY_DOWN(gafAsyncKeyState, VK_SHIFT))
                bLangToggled = IntCheckLanguageToggle(pFocusQueue, bIsDown, 0, &gLanguageToggleKeyState);
        }
    }
    if (!bLangToggled && (gdwLayoutToggleKey == 1 || gdwLayoutToggleKey == 2))
    {
        if (wSimpleVk == VK_SHIFT) /* Shift key is pressed or released */
        {
            UINT targetKey = ((gdwLayoutToggleKey == 1) ? VK_LMENU : VK_CONTROL);
            if (IS_KEY_DOWN(gafAsyncKeyState, targetKey))
                IntCheckLanguageToggle(pFocusQueue, bIsDown, wVk, &gLayoutToggleKeyState);
        }
        else if ((wSimpleVk == VK_MENU && gdwLayoutToggleKey == 1) ||
                 (wSimpleVk == VK_CONTROL && gdwLayoutToggleKey == 2))
        {
            if (IS_KEY_DOWN(gafAsyncKeyState, VK_SHIFT))
                IntCheckLanguageToggle(pFocusQueue, bIsDown, 0, &gLayoutToggleKeyState);
        }
    }

    if (bIsDown && wVk == VK_SNAPSHOT)
    {
        if (pFocusQueue &&
            IS_KEY_DOWN(gafAsyncKeyState, VK_MENU) &&
            !IS_KEY_DOWN(gafAsyncKeyState, VK_CONTROL))
        {
            // Snap from Active Window, Focus can be null.
            SnapWindow(pFocusQueue->spwndActive ? UserHMGetHandle(pFocusQueue->spwndActive) : NULL);
        }
        else
            SnapWindow(NULL); // Snap Desktop.
    }
    else if (pFocusQueue && bPostMsg)
    {
        PWND Wnd = pFocusQueue->spwndFocus; // SysInit.....

        pti = pFocusQueue->ptiKeyboard;

        if (!Wnd && pFocusQueue->spwndActive) // SysInit.....
        {
           // Going with Active. WM_SYSKEYXXX last wine Win test_keyboard_input.
           Wnd = pFocusQueue->spwndActive;
        }
        if (Wnd) pti = Wnd->head.pti;

        /* Init message */
        Msg.hwnd = Wnd ? UserHMGetHandle(Wnd) : NULL;
        Msg.wParam = wFixedVk & 0xFF; /* Note: It's simplified by msg queue */
        Msg.lParam = MAKELPARAM(1, wScanCode);
        Msg.time = dwTime;
        Msg.pt = gpsi->ptCursor;

        if (Msg.message == WM_KEYDOWN || Msg.message == WM_SYSKEYDOWN)
        {
           if ((Msg.wParam == VK_SHIFT ||
                Msg.wParam == VK_CONTROL ||
                Msg.wParam == VK_MENU) &&
               !IS_KEY_DOWN(gafAsyncKeyState, Msg.wParam))
           {
              ERR("Set last input\n");
              //ptiLastInput = pti;
           }
        }

        /* If it is VK_PACKET, high word of wParam is used for wchar */
        if (!bPacket)
        {
            if (bExt)
                Msg.lParam |= KF_EXTENDED << 16;
            if (IS_KEY_DOWN(gafAsyncKeyState, VK_MENU))
                Msg.lParam |= KF_ALTDOWN << 16;
            if (bWasSimpleDown)
                Msg.lParam |= KF_REPEAT << 16;
            if (!bIsDown)
                Msg.lParam |= KF_UP << 16;
            /* FIXME: Set KF_DLGMODE and KF_MENUMODE when needed */
            if (pFocusQueue->QF_flags & QF_DIALOGACTIVE)
                Msg.lParam |= KF_DLGMODE << 16;
            if (pFocusQueue->MenuOwner) // (pti->pMenuState && pti->pMenuState->fMenuStarted)
                Msg.lParam |= KF_MENUMODE << 16;
        }

        // Post mouse move before posting key buttons, to keep it synced.
        if (pFocusQueue->QF_flags & QF_MOUSEMOVED)
            IntCoalesceMouseMove(pti);

        /* Post a keyboard message */
        TRACE("Posting keyboard msg %u wParam 0x%x lParam 0x%x\n", Msg.message, Msg.wParam, Msg.lParam);
        if (!Wnd) ERR("Window is NULL\n");
        MsqPostMessage(pti, &Msg, TRUE, QS_KEY, 0, dwExtraInfo);
    }
    return TRUE;
}

BOOL NTAPI
UserSendKeyboardInput(KEYBDINPUT *pKbdInput, BOOL bInjected)
{
    WORD wScanCode, wVk;
    PKL pKl = NULL;
    PKBDTABLES pKbdTbl;
    PUSER_MESSAGE_QUEUE pFocusQueue;
    DWORD dwTime;
    BOOL bExt = (pKbdInput->dwFlags & KEYEVENTF_EXTENDEDKEY) ? TRUE : FALSE;

    gppiInputProvider = ((PTHREADINFO)PsGetCurrentThreadWin32Thread())->ppi;

    /* Find the target thread whose locale is in effect */
    pFocusQueue = IntGetFocusMessageQueue();

    if (pFocusQueue && pFocusQueue->ptiKeyboard)
    {
        pKl = pFocusQueue->ptiKeyboard->KeyboardLayout;
    }

    if (!pKl)
        pKl = W32kGetDefaultKeyLayout();
    if (!pKl)
    {
        ERR("No keyboard layout!\n");
        return FALSE;
    }

    pKbdTbl = pKl->spkf->pKbdTbl;

    /* Note: wScan field is always used */
    wScanCode = pKbdInput->wScan;

    if (pKbdInput->dwFlags & KEYEVENTF_UNICODE)
    {
        /* Generate WM_KEYDOWN msg with wParam == VK_PACKET and
           high order word of lParam == pKbdInput->wScan */
        wVk = VK_PACKET;
    }
    else
    {
        wScanCode &= 0x7F;
        if (pKbdInput->dwFlags & KEYEVENTF_SCANCODE)
        {
            /* Don't ignore invalid scan codes */
            wVk = IntVscToVk(wScanCode | (bExt ? 0xE000 : 0), pKbdTbl);
            if (!wVk) /* use 0xFF if vsc is invalid */
                wVk = 0xFF;
        }
        else
        {
            wVk = pKbdInput->wVk;
        }

        /* Remove all virtual key flags (KBDEXT, KBDMULTIVK, KBDSPECIAL, KBDNUMPAD) */
        wVk &= 0xFF;
    }

    /* If time is given, use it */
    if (pKbdInput->time)
        dwTime = pKbdInput->time;
    else
        dwTime = EngGetTickCount32();

    if (wVk == VK_RMENU && (pKbdTbl->fLocaleFlags & KLLF_ALTGR))
    {
        /* For AltGr keyboards RALT generates CTRL events */
        ProcessKeyEvent(VK_LCONTROL, 0, pKbdInput->dwFlags & KEYEVENTF_KEYUP, bInjected, dwTime, 0);
    }

    /* Finally process this key */
    return ProcessKeyEvent(wVk, wScanCode, pKbdInput->dwFlags, bInjected, dwTime, pKbdInput->dwExtraInfo);
}

/*
 * UserProcessKeyboardInput
 *
 * Process raw keyboard input data
 */
VOID NTAPI
UserProcessKeyboardInput(
    PKEYBOARD_INPUT_DATA pKbdInputData)
{
    WORD wScanCode, wVk;
    PKL pKl = NULL;
    PKBDTABLES pKbdTbl;
    PUSER_MESSAGE_QUEUE pFocusQueue;

    /* Calculate scan code with prefix */
    wScanCode = pKbdInputData->MakeCode & 0x7F;
    if (pKbdInputData->Flags & KEY_E0)
        wScanCode |= 0xE000;
    if (pKbdInputData->Flags & KEY_E1)
        wScanCode |= 0xE100;

    /* Find the target thread whose locale is in effect */
    pFocusQueue = IntGetFocusMessageQueue();

    if (pFocusQueue && pFocusQueue->ptiKeyboard)
    {
        pKl = pFocusQueue->ptiKeyboard->KeyboardLayout;
    }

    if (!pKl)
        pKl = W32kGetDefaultKeyLayout();
    if (!pKl)
        return;

    pKbdTbl = pKl->spkf->pKbdTbl;

    /* Convert scan code to virtual key.
       Note: We could call UserSendKeyboardInput using scan code,
             but it wouldn't interpret E1 key(s) properly */
    wVk = IntVscToVk(wScanCode, pKbdTbl);
    TRACE("UserProcessKeyboardInput: %x (break: %u) -> %x\n",
          wScanCode, (pKbdInputData->Flags & KEY_BREAK) ? 1u : 0, wVk);

    if (wVk)
    {
        KEYBDINPUT KbdInput;

        /* Support numlock */
        if ((wVk & KBDNUMPAD) && IS_KEY_LOCKED(gafAsyncKeyState, VK_NUMLOCK))
        {
            wVk = IntTranslateNumpadKey(wVk & 0xFF);
        }

        /* Send keyboard input */
        KbdInput.wVk = wVk & 0xFF;
        KbdInput.wScan = wScanCode & 0x7F;
        KbdInput.dwFlags = 0;
        if (pKbdInputData->Flags & KEY_BREAK)
            KbdInput.dwFlags |= KEYEVENTF_KEYUP;

        if (wVk & KBDEXT)
            KbdInput.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        //
        // Based on wine input:test_Input_blackbox this is okay. It seems the
        // bit did not get set and more research is needed. Now the right
        // shift works.
        //
        if (wVk == VK_RSHIFT)
            KbdInput.dwFlags |= KEYEVENTF_EXTENDEDKEY;

        KbdInput.time = 0;
        KbdInput.dwExtraInfo = pKbdInputData->ExtraInformation;
        UserSendKeyboardInput(&KbdInput, FALSE);

        /* E1 keys don't have break code */
        if (pKbdInputData->Flags & KEY_E1)
        {
            /* Send key up event */
            KbdInput.dwFlags |= KEYEVENTF_KEYUP;
            UserSendKeyboardInput(&KbdInput, FALSE);
        }
    }
}

/**
 * @brief
 * Generates WM_(SYS)CHAR messages in the message queue if the
 * key-up/down message describes a key which produce character(s).
 **/
BOOL FASTCALL
IntTranslateKbdMessage(
    LPMSG lpMsg,
    UINT flags)
{
    PTHREADINFO pti;
    INT cch = 0, i;
    LPARAM msgLParam;
    WCHAR wch[3] = { 0 }; // FIXME: May have to be enlarged for high plane Unicode characters.
    MSG NewMsg = { 0 };
    PKBDTABLES pKbdTbl;
    BOOL bSysKey = FALSE;
    BOOL bResult = FALSE;

    switch (lpMsg->message)
    {
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
            bSysKey = TRUE;
            __fallthrough;
        case WM_KEYDOWN:
        case WM_KEYUP:
            break;
        default:
            return FALSE;
    }

    pti = PsGetCurrentThreadWin32Thread();

    if (!pti->KeyboardLayout)
    {
        PKL pDefKL = W32kGetDefaultKeyLayout();
        UserAssignmentLock((PVOID*)&(pti->KeyboardLayout), pDefKL);
        if (pDefKL)
        {
            pti->pClientInfo->hKL = pDefKL->hkl;
            pKbdTbl = pDefKL->spkf->pKbdTbl;
        }
        else
        {
            pti->pClientInfo->hKL = NULL;
            pKbdTbl = NULL;
        }
    }
    else
    {
        pKbdTbl = pti->KeyboardLayout->spkf->pKbdTbl;
    }
    if (!pKbdTbl)
        return FALSE;

    // FIXME: Disabled because wrong (i.e. we _should_ support the UP messages).
    // See:
    // https://github.com/reactos/reactos/commit/3f52e9d7281867283a71a88451c30885dfde3f9b
    // Wine has this as well, but it's wrong, Windows does translate WM_*KEYUP messages.
    //if (lpMsg->message != WM_KEYDOWN && lpMsg->message != WM_SYSKEYDOWN)
    //    return FALSE;

    /* Init pt, hwnd and time msg fields */
    NewMsg.pt = gpsi->ptCursor;
    NewMsg.hwnd = lpMsg->hwnd;
    NewMsg.time = EngGetTickCount32();

    TRACE("Enter IntTranslateKbdMessage msg 0x%x, vk 0x%x\n", lpMsg->message, lpMsg->wParam);

    if (lpMsg->wParam == VK_PACKET)
    {
__debugbreak();
        NewMsg.message = (bSysKey ? WM_SYSCHAR : WM_CHAR);
        NewMsg.wParam = HIWORD(lpMsg->lParam);
        NewMsg.lParam = LOWORD(lpMsg->lParam);
        MsqPostMessage(pti, &NewMsg, FALSE, QS_KEY, 0, 0);
        return TRUE;
    }

    if (pti->MessageQueue->MenuOwner) // (pti->pMenuState && pti->pMenuState->fMenuStarted)
        flags |= TM_MENUMODE;
    // else, should we remove it in case the caller set it? flags &= ~TM_MENUMODE;

ERR("IntTranslateKbdMessage msg 0x%x, vk 0x%x scancode 0x%x\n", lpMsg->message, lpMsg->wParam, HIWORD(lpMsg->lParam));
    cch = IntToUnicodeEx(lpMsg->wParam,
                         HIWORD(lpMsg->lParam),
                         pti->MessageQueue->afKeyState,
                         &msgLParam,
                         wch,
                         sizeof(wch) / sizeof(wch[0]),
                         flags,
                         pKbdTbl);

    // TODO: Consistently handle TM_POSTCHARBREAKS, see:
    // https://github.com/microsoft/terminal/blob/e05a5fb6d8ff492cf9c49761bad70363e8515c5e/src/interactivity/win32/ConsoleControl.hpp#L21
    if (cch == 0)
    {
        /*
         * No message translated and WM_*CHAR posted. Return FALSE if
         * bit 1 (TM_POSTCHARBREAKS flag) is set, or TRUE otherwise, see:
         * https://learn.microsoft.com/en-us/windows/win32/winmsg/translatemessageex#flags-in
         */
        bResult = !(flags & TM_POSTCHARBREAKS);
        goto Quit;
    }

    if (cch > 0) /* Normal characters */
    {
        NewMsg.message = (bSysKey ? WM_SYSCHAR : WM_CHAR);
    }
    else /* Dead character */
    {
        cch = -cch;
        NewMsg.message = (bSysKey ? WM_SYSDEADCHAR : WM_DEADCHAR);
    }

    /* Prepare the message lParam */
    msgLParam |= lpMsg->lParam;
    /* Ensure the up/down flag is correctly set */
    if (!(msgLParam & (KF_UP << 16)))
        msgLParam &= ~(KF_UP << 16);

    /* Send all characters */
    for (i = 0; i < cch; ++i)
    {
        TRACE("Msg: 0x%x '%lc' (%04x) %08x\n", NewMsg.message, wch[i], wch[i], lpMsg->lParam);
        NewMsg.wParam = wch[i];
        /* In case multi character messages are generated, all except the
         * last one are tagged as "fake" keystrokes (DONTCARE_BIT set). */
        NewMsg.lParam = msgLParam | ((i < cch-1) ? FAKE_KEYSTROKE : 0);
        MsqPostMessage(pti, &NewMsg, FALSE, QS_KEY, 0, 0);
    }
    bResult = TRUE;

Quit:
    TRACE("Leave IntTranslateKbdMessage ret %u, msg 0x%x, cch %u, wch 0x%x\n",
          bResult, NewMsg.message, cch, NewMsg.wParam);
    return bResult;
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
static UINT
IntMapVirtualKeyEx(UINT uCode, UINT Type, PKBDTABLES pKbdTbl)
{
    UINT uRet = 0;

    switch (Type)
    {
        case MAPVK_VK_TO_VSC:
            uCode = IntFixVk(uCode, FALSE);
            uRet = IntVkToVsc(uCode, pKbdTbl);
            if (uRet > 0xFF) // Fail for scancodes with prefix (e0, e1)
                uRet = 0;
            break;

        case MAPVK_VSC_TO_VK:
            uRet = IntVscToVk(uCode, pKbdTbl) & 0xFF;
            uRet = IntSimplifyVk(uRet);
            break;

        case MAPVK_VK_TO_CHAR:
            uRet = (UINT)IntVkToChar(uCode, pKbdTbl);
        break;

        case MAPVK_VSC_TO_VK_EX:
            uRet = IntVscToVk(uCode, pKbdTbl) & 0xFF;
            break;

        case MAPVK_VK_TO_VSC_EX:
            uRet = IntVkToVsc(uCode, pKbdTbl);
            break;

        default:
            EngSetLastError(ERROR_INVALID_PARAMETER);
            ERR("Wrong type value: %u\n", Type);
    }

    return uRet;
}

/*
 * NtUserMapVirtualKeyEx
 *
 * Map a virtual key code, or virtual scan code, to a scan code, key code,
 * or unshifted unicode character. See IntMapVirtualKeyEx.
 */
UINT
APIENTRY
NtUserMapVirtualKeyEx(UINT uCode, UINT uType, DWORD keyboardId, HKL dwhkl)
{
    PKBDTABLES pKbdTbl = NULL;
    UINT ret = 0;

    TRACE("Enter NtUserMapVirtualKeyEx\n");
    UserEnterShared();

    if (!dwhkl)
    {
        PTHREADINFO pti;

        pti = PsGetCurrentThreadWin32Thread();
        if (pti && pti->KeyboardLayout)
            pKbdTbl = pti->KeyboardLayout->spkf->pKbdTbl;
    }
    else
    {
        PKL pKl;

        pKl = UserHklToKbl(dwhkl);
        if (pKl)
            pKbdTbl = pKl->spkf->pKbdTbl;
    }

    if (pKbdTbl)
        ret = IntMapVirtualKeyEx(uCode, uType, pKbdTbl);

    UserLeave();
    TRACE("Leave NtUserMapVirtualKeyEx, ret=%u\n", ret);
    return ret;
}

/*
 * NtUserToUnicodeEx
 *
 * Translates virtual key to characters
 */
int
APIENTRY
NtUserToUnicodeEx(
    UINT wVirtKey,
    UINT wScanCode,
    PBYTE pKeyStateUnsafe,
    LPWSTR pwszBuffUnsafe,
    INT cchBuff,
    UINT wFlags,
    HKL dwhkl)
{
    PTHREADINFO pti;
    BYTE afKeyState[256 * 2 / 8] = {0};
    PWCHAR pwszBuff = NULL;
    INT i, iRet = 0;
    PKL pKl = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    TRACE("Enter NtUserToUnicodeEx\n");

    // TODO: Remove this code; this is implicitly handled by IntToUnicodeEx.
    // See https://github.com/reactos/reactos/commit/428dff1a79ed6a592bd486c77e4110c0949d3188
    // and https://github.com/reactos/reactos/commit/1dbbf02a7e30d9bcb797a30157e93483951f4840
    // Also, remove SC_KEY_UP from input.h
    // /* Return 0 if SC_KEY_UP bit is set */
    // if (wScanCode & SC_KEY_UP || wVirtKey >= 0x100)
    // {
    //     ERR("Invalid parameter\n");
    //     return 0;
    // }

    _SEH2_TRY
    {
        /* Probe and copy key state to smaller bitmap */
        ProbeForRead(pKeyStateUnsafe, 256 * sizeof(BYTE), 1);
        for (i = 0; i < 256; ++i)
        {
            if (pKeyStateUnsafe[i] & KS_DOWN_BIT)
                SET_KEY_DOWN(afKeyState, i, TRUE);
            if (pKeyStateUnsafe[i] & KS_LOCK_BIT)
                SET_KEY_LOCKED(afKeyState, i, TRUE);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ERR("Cannot copy key state\n");
        SetLastNtError(_SEH2_GetExceptionCode());
        _SEH2_YIELD(return 0);
    }
    _SEH2_END;

    pwszBuff = ExAllocatePoolWithTag(NonPagedPool, sizeof(WCHAR) * cchBuff, TAG_STRING);
    if (!pwszBuff)
    {
        ERR("ExAllocatePoolWithTag(%u) failed\n", sizeof(WCHAR) * cchBuff);
        return 0;
    }
    RtlZeroMemory(pwszBuff, sizeof(WCHAR) * cchBuff);

    UserEnterExclusive(); // Note: We modify wchDead static variable

    if (dwhkl)
        pKl = UserHklToKbl(dwhkl);

    if (!pKl)
    {
        pti = PsGetCurrentThreadWin32Thread();
        pKl = pti->KeyboardLayout;
    }

    if (pKl)
    {
        LPARAM lfDummy;
        iRet = IntToUnicodeEx(wVirtKey,
                              wScanCode,
                              afKeyState,
                              &lfDummy,
                              pwszBuff,
                              cchBuff,
                              wFlags,
                              pKl->spkf->pKbdTbl);
        if (iRet)
        {
            Status = MmCopyToCaller(pwszBuffUnsafe, pwszBuff, cchBuff * sizeof(WCHAR));
        }
    }
    else
    {
        ERR("No keyboard layout ?!\n");
        Status = STATUS_INVALID_HANDLE;
    }

    ExFreePoolWithTag(pwszBuff, TAG_STRING);

    if (!NT_SUCCESS(Status))
    {
        iRet = 0;
        SetLastNtError(Status);
    }

    UserLeave();
    TRACE("Leave NtUserToUnicodeEx, ret=%i\n", iRet);
    return iRet;
}

/*
 * NtUserGetKeyNameText
 *
 * Gets key name from keyboard layout
 */
DWORD
APIENTRY
NtUserGetKeyNameText(LONG lParam, LPWSTR lpString, int cchSize)
{
    PTHREADINFO pti;
    DWORD i, dwRet = 0;
    SIZE_T cchKeyName;
    WORD wScanCode = (lParam >> 16) & 0xFF;
    BOOL bExtKey = (HIWORD(lParam) & KF_EXTENDED) ? TRUE : FALSE;
    PKBDTABLES pKbdTbl;
    VSC_LPWSTR *pKeyNames = NULL;
    CONST WCHAR *pKeyName = NULL;
    WCHAR KeyNameBuf[2];

    TRACE("Enter NtUserGetKeyNameText\n");

    UserEnterShared();

    /* Get current keyboard layout */
    pti = PsGetCurrentThreadWin32Thread();
    pKbdTbl = pti ? pti->KeyboardLayout->spkf->pKbdTbl : NULL;

    if (!pKbdTbl || cchSize < 1)
    {
        ERR("Invalid parameter\n");
        goto cleanup;
    }

    /* "Do not care" flag */
    if(lParam & LP_DO_NOT_CARE_BIT)
    {
        /* Note: We could do vsc -> vk -> vsc conversion, instead of using
                 hardcoded scan codes, but it's not what Windows does */
        if (wScanCode == SCANCODE_RSHIFT && !bExtKey)
            wScanCode = SCANCODE_LSHIFT;
        else if (wScanCode == SCANCODE_CTRL || wScanCode == SCANCODE_ALT)
            bExtKey = FALSE;
    }

    if (bExtKey)
        pKeyNames = pKbdTbl->pKeyNamesExt;
    else
        pKeyNames = pKbdTbl->pKeyNames;

    for (i = 0; pKeyNames[i].pwsz; i++)
    {
        if (pKeyNames[i].vsc == wScanCode)
        {
            pKeyName = pKeyNames[i].pwsz;
            break;
        }
    }

    if (!pKeyName)
    {
        WORD wVk = IntVscToVk(wScanCode, pKbdTbl);

        if (wVk)
        {
            KeyNameBuf[0] = IntVkToChar(wVk, pKbdTbl);
            KeyNameBuf[1] = 0;
            if (KeyNameBuf[0])
                pKeyName = KeyNameBuf;
        }
    }

    if (pKeyName)
    {
        cchKeyName = wcslen(pKeyName);
        if (cchKeyName > (cchSize - 1UL))
            cchKeyName = cchSize - 1UL; // Don't count '\0'

        _SEH2_TRY
        {
            ProbeForWrite(lpString, (cchKeyName + 1) * sizeof(WCHAR), 1);
            RtlCopyMemory(lpString, pKeyName, cchKeyName * sizeof(WCHAR));
            lpString[cchKeyName] = UNICODE_NULL;
            dwRet = cchKeyName;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            SetLastNtError(_SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
    }

cleanup:
    UserLeave();
    TRACE("Leave NtUserGetKeyNameText, ret=%lu\n", dwRet);
    return dwRet;
}

/*
 * UserGetKeyboardType
 *
 * Returns some keyboard specific information
 */
DWORD FASTCALL
UserGetKeyboardType(
    DWORD dwTypeFlag)
{
    switch (dwTypeFlag)
    {
        case 0:        /* Keyboard type */
            return (DWORD)gKeyboardInfo.KeyboardIdentifier.Type;
        case 1:        /* Keyboard Subtype */
            return (DWORD)gKeyboardInfo.KeyboardIdentifier.Subtype;
        case 2:        /* Number of F-keys */
            return (DWORD)gKeyboardInfo.NumberOfFunctionKeys;
        default:
            ERR("Unknown type!\n");
            return 0;    /* Note: we don't have to set last error here */
    }
}

/*
 * NtUserVkKeyScanEx
 *
 * Based on IntTranslateChar, instead of processing VirtualKey match,
 * look for wChar match.
 */
DWORD
APIENTRY
NtUserVkKeyScanEx(
    WCHAR wch,
    HKL dwhkl,
    BOOL bUsehKL)
{
    PKBDTABLES pKbdTbl;
    PVK_TO_WCHAR_TABLE pVkToWchTbl;
    PVK_TO_WCHARS10 pVkToWch;
    PKL pKl = NULL;
    DWORD i, dwModBits = 0, dwModNumber = 0, Ret = (DWORD)-1;

    TRACE("NtUserVkKeyScanEx() wch %u, KbdLayout 0x%p\n", wch, dwhkl);
    UserEnterShared();

    if (bUsehKL)
    {
        // Use given keyboard layout
        if (dwhkl)
            pKl = UserHklToKbl(dwhkl);
    }
    else
    {
        // Use thread keyboard layout
        pKl = ((PTHREADINFO)PsGetCurrentThreadWin32Thread())->KeyboardLayout;
    }

    if (!pKl)
        goto Exit;

    pKbdTbl = pKl->spkf->pKbdTbl;

    // Interate through all VkToWchar tables while pVkToWchars is not NULL
    for (i = 0; pKbdTbl->pVkToWcharTable[i].pVkToWchars; i++)
    {
        pVkToWchTbl = &pKbdTbl->pVkToWcharTable[i];
        pVkToWch = (PVK_TO_WCHARS10)(pVkToWchTbl->pVkToWchars);

        // Interate through all virtual keys
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
                    goto Exit;
                }
            }
            pVkToWch = (PVK_TO_WCHARS10)(((BYTE *)pVkToWch) + pVkToWchTbl->cbSize);
        }
    }
Exit:
    UserLeave();
    return Ret;
}

/* EOF */
