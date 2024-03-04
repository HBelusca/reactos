/*
 * PROJECT:     NT-compatible ReactOS/Windows OS Loader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Presents the F8 Advanced Boot Options menu.
 * COPYRIGHT:   Copyright 1998-2003 Brian Palmer <brianp@sginet.com>
 *              Copyright 2010 Cameron Gutman <cameron.gutman@reactos.org>
 *              Copyright 2012-2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/* INCLUDES *******************************************************************/

#include <freeldr.h>

/* GLOBALS ********************************************************************/

#if 0
static UI_MENU_ITEMS OptionsMenuList[] =
{
    {"Safe Mode",                       {SAFEBOOT,          BOOT_LOGGING,   "SAFEBOOT:MINIMAL SOS NOGUIBOOT"}},
    {"Safe Mode with Networking",       {SAFEBOOT_NETWORK,  BOOT_LOGGING,   "SAFEBOOT:NETWORK SOS NOGUIBOOT"}},
    {"Safe Mode with Command Prompt",   {SAFEBOOT_ALTSHELL, BOOT_LOGGING,   "SAFEBOOT:MINIMAL(ALTERNATESHELL) SOS NOGUIBOOT"}},

    {NULL, NULL},

    {"Enable Boot Logging",             {0,                 BOOT_LOGGING,   NULL}},
    {"Enable VGA Mode",                 {0,                 BOOT_VGA_MODE,  NULL}},
    {"Last Known Good Configuration",   {LKG_CONFIG,        0,              NULL}},
    {"Directory Services Restore Mode", {SAFEBOOT_DSREPAIR, 0,              "SAFEBOOT:DSREPAIR SOS"}},
    {"Debugging Mode",                  {0,                 BOOT_DEBUGGING, NULL}},

    {NULL, NULL},

    {"Start ReactOS normally",          {NO_OPTION, 0, NULL}},
#ifdef HAS_OPTION_MENU_EDIT_CMDLINE
    {"Edit Boot Command Line (F10)", NULL},
#endif
#ifdef HAS_OPTION_MENU_REBOOT
    {"Reboot", NULL},
#endif
};
#endif

static const PCSTR OptionsMenuList[] =
{
    "Safe Mode",
    "Safe Mode with Networking",
    "Safe Mode with Command Prompt",

    NULL,

    "Enable Boot Logging",
    "Enable VGA Mode",
    "Last Known Good Configuration",
    "Directory Services Restore Mode",
    "Debugging Mode",

    NULL,

    "Start ReactOS normally",
#ifdef HAS_OPTION_MENU_EDIT_CMDLINE
    "Edit Boot Command Line (F10)",
#endif
#ifdef HAS_OPTION_MENU_REBOOT
    "Reboot",
#endif
};

/* Advanced NT boot options */
enum BootOption BootOptionChoice = NO_OPTION;
LOGICAL BootFlags = 0;

/* FUNCTIONS ******************************************************************/

static VOID
GetBootOptionsDescription(
    _Inout_z_bytecount_(BootOptsDescSize)
         PSTR BootOptsDesc,
    _In_ SIZE_T BootOptsDescSize)
{
    /* NOTE: Keep in sync with the 'enum BootOption'
     * in winldr.h and the OptionsMenuList above. */
    static const PCSTR* OptionNames[] =
    {
        /* NO_OPTION         */ NULL,
        /* SAFEBOOT          */ &OptionsMenuList[0],
        /* SAFEBOOT_NETWORK  */ &OptionsMenuList[1],
        /* SAFEBOOT_ALTSHELL */ &OptionsMenuList[2],
        /* SAFEBOOT_DSREPAIR */ &OptionsMenuList[7],
        /* LKG_CONFIG        */ &OptionsMenuList[6],
    };

    if (BootOptsDescSize < sizeof(CHAR))
        return;

    *BootOptsDesc = ANSI_NULL;

    ASSERT(BootOptionChoice < RTL_NUMBER_OF(OptionNames));
    if (BootOptionChoice != NO_OPTION) // && BootOptionChoice < RTL_NUMBER_OF(OptionNames)
        RtlStringCbCatA(BootOptsDesc, BootOptsDescSize, *OptionNames[BootOptionChoice]);

    if (BootFlags & BOOT_LOGGING)
    {
        /* Since these safe mode options come by default with boot logging,
         * don't show "Boot Logging" when one of these is selected;
         * instead just show the corresponding safe mode option name. */
        if ( (BootOptionChoice != SAFEBOOT) &&
             (BootOptionChoice != SAFEBOOT_NETWORK) &&
             (BootOptionChoice != SAFEBOOT_ALTSHELL) )
        {
            if (*BootOptsDesc)
                RtlStringCbCatA(BootOptsDesc, BootOptsDescSize, ", ");
            RtlStringCbCatA(BootOptsDesc, BootOptsDescSize, OptionsMenuList[4]);
        }
    }

    if (BootFlags & BOOT_VGA_MODE)
    {
        if (*BootOptsDesc)
            RtlStringCbCatA(BootOptsDesc, BootOptsDescSize, ", ");
        RtlStringCbCatA(BootOptsDesc, BootOptsDescSize, OptionsMenuList[5]);
    }

    if (BootFlags & BOOT_DEBUGGING)
    {
        if (*BootOptsDesc)
            RtlStringCbCatA(BootOptsDesc, BootOptsDescSize, ", ");
        RtlStringCbCatA(BootOptsDesc, BootOptsDescSize, OptionsMenuList[8]);
    }
}

VOID
DoNTOptionsMenu(
    _Inout_ OperatingSystemItem* OperatingSystem)
{
    ULONG SelectedMenuItem;
    CHAR MenuTitle[260];

    RtlStringCbPrintfA(MenuTitle, sizeof(MenuTitle),
                       "Advanced options for: %s\n"
                       "Select an option:",
                       OperatingSystem->LoadIdentifier);

    if (!UiDisplayMenu(MenuTitle, NULL,
                       TRUE,
                       OptionsMenuList,
                       RTL_NUMBER_OF(OptionsMenuList),
                       10, // Use "Start ReactOS normally" as default; see the switch below.
                       -1,
                       &SelectedMenuItem,
                       TRUE,
                       NULL, NULL))
    {
        /* The user pressed ESC */
        return;
    }

    /* Clear the backdrop */
    UiDrawBackdrop();

    switch (SelectedMenuItem)
    {
        case 0: // Safe Mode
            BootOptionChoice = SAFEBOOT;
            BootFlags |= BOOT_LOGGING;
            break;
        case 1: // Safe Mode with Networking
            BootOptionChoice = SAFEBOOT_NETWORK;
            BootFlags |= BOOT_LOGGING;
            break;
        case 2: // Safe Mode with Command Prompt
            BootOptionChoice = SAFEBOOT_ALTSHELL;
            BootFlags |= BOOT_LOGGING;
            break;
        // case 3: // Separator
        //     break;
        case 4: // Enable Boot Logging
            BootFlags |= BOOT_LOGGING;
            break;
        case 5: // Enable VGA Mode
            BootFlags |= BOOT_VGA_MODE;
            break;
        case 6: // Last Known Good Configuration
            BootOptionChoice = LKG_CONFIG;
            break;
        case 7: // Directory Services Restore Mode
            BootOptionChoice = SAFEBOOT_DSREPAIR;
            break;
        case 8: // Debugging Mode
            BootFlags |= BOOT_DEBUGGING;
            break;
        // case 9: // Separator
        //     break;
        case 10: // Start ReactOS normally
            // Reset all the parameters to their default values.
            BootOptionChoice = NO_OPTION;
            BootFlags = 0;
            break;
#ifdef HAS_OPTION_MENU_EDIT_CMDLINE
        case 11: // Edit command line
            EditOperatingSystemEntry(OperatingSystem);
            break;
#endif
#ifdef HAS_OPTION_MENU_REBOOT
        case 12: // Reboot
            OptionMenuReboot();
            break;
#endif
    }

    /* Update the human-readable boot-option description string */
    GetBootOptionsDescription(OperatingSystem->AdvBootOptsDesc,
                              sizeof(OperatingSystem->AdvBootOptsDesc));
}

VOID
AppendBootTimeOptions(
    _Inout_z_bytecount_(BootOptionsSize)
         PSTR BootOptions,
    _In_ SIZE_T BootOptionsSize)
{
    /* NOTE: Keep in sync with the 'enum BootOption' in winldr.h */
    static const PCSTR OptionsStr[] =
    {
        /* NO_OPTION         */ NULL,
        /* SAFEBOOT          */ "SAFEBOOT:MINIMAL SOS NOGUIBOOT",
        /* SAFEBOOT_NETWORK  */ "SAFEBOOT:NETWORK SOS NOGUIBOOT",
        /* SAFEBOOT_ALTSHELL */ "SAFEBOOT:MINIMAL(ALTERNATESHELL) SOS NOGUIBOOT",
        /* SAFEBOOT_DSREPAIR */ "SAFEBOOT:DSREPAIR SOS",
        /* LKG_CONFIG        */ NULL,
    };

    if (BootOptionsSize < sizeof(CHAR))
        return;

    switch (BootOptionChoice)
    {
        case SAFEBOOT:
        case SAFEBOOT_NETWORK:
        case SAFEBOOT_ALTSHELL:
        case SAFEBOOT_DSREPAIR:
        {
            ASSERT(BootOptionChoice < RTL_NUMBER_OF(OptionsStr));
            if (*BootOptions)
                RtlStringCbCatA(BootOptions, BootOptionsSize, " ");
            RtlStringCbCatA(BootOptions, BootOptionsSize, OptionsStr[BootOptionChoice]);
            // NtLdrAddOptions(BootOptions, BootOptionsSize, TRUE, OptionsStr[BootOptionChoice]);
            break;
        }

        case LKG_CONFIG:
            DbgPrint("Last known good configuration is not supported yet!\n");
            break;

        default:
            break;
    }

    // NtLdrAddOptions(BootOptions, BootOptionsSize, TRUE, ...);
    if (BootFlags & BOOT_LOGGING)
        RtlStringCbCatA(BootOptions, BootOptionsSize, " BOOTLOG");

    if (BootFlags & BOOT_VGA_MODE)
        RtlStringCbCatA(BootOptions, BootOptionsSize, " BASEVIDEO");

    if (BootFlags & BOOT_DEBUGGING)
        RtlStringCbCatA(BootOptions, BootOptionsSize, " DEBUG");
}
