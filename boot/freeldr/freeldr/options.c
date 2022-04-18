/*
 *  FreeLoader
 *  Copyright (C) 1998-2003  Brian Palmer  <brianp@sginet.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/* INCLUDES *******************************************************************/

#include <freeldr.h>
#include <debug.h>

/* GLOBALS ********************************************************************/

PCSTR OptionsMenuList[] =
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
    "FreeLdr debugging",

    NULL,

    "Start ReactOS normally",
#ifdef HAS_OPTION_MENU_EDIT_CMDLINE
    "Edit Boot Command Line (F10)",
#endif
#ifdef HAS_OPTION_MENU_CUSTOM_BOOT
    "Custom Boot",
#endif
#ifdef HAS_OPTION_MENU_REBOOT
    "Reboot",
#endif
};

const
PCSTR FrldrDbgMsg = "Enable FreeLdr debug channels\n"
                    "Acceptable syntax: [level1]#channel1[,[level2]#channel2]\n"
                    "level can be one of: trace,warn,fixme,err\n"
                    "  if the level is omitted all levels\n"
                    "  are enabled for the specified channel\n"
                    "# can be either + or -\n"
                    "channel can be one of the following:\n"
                    "  all,warning,memory,filesystem,inifile,ui,disk,cache,registry,\n"
                    "  reactos,linux,hwdetect,windows,peloader,scsiport,heap\n"
                    "Examples:\n"
                    "  trace+windows,trace+reactos\n"
                    "  +hwdetect,err-disk\n"
                    "  +peloader\n"
                    "NOTE: all letters must be lowercase, no spaces allowed.";


/* FUNCTIONS ******************************************************************/

VOID DoOptionsMenu(IN OperatingSystemItem* OperatingSystem)
{
    ULONG SelectedMenuItem;
    CHAR  DebugChannelString[100];

    if (!UiDisplayMenu("Select an option:", NULL,
                       TRUE,
                       OptionsMenuList,
                       RTL_NUMBER_OF(OptionsMenuList),
                       11, // Use "Start ReactOS normally" as default; see the switch below.
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
        case 9: // FreeLdr debugging
            DebugChannelString[0] = 0;
            if (UiEditBox(FrldrDbgMsg,
                          DebugChannelString,
                          RTL_NUMBER_OF(DebugChannelString)))
            {
                DbgParseDebugChannels(DebugChannelString);
            }
            break;
        // case 10: // Separator
        //     break;
        case 11: // Start ReactOS normally
            // Reset all the parameters to their default values.
            BootOptionChoice = NO_OPTION;
            BootFlags = 0;
            break;
#ifdef HAS_OPTION_MENU_EDIT_CMDLINE
        case 12: // Edit command line
            EditOperatingSystemEntry(OperatingSystem);
            break;
#endif
#ifdef HAS_OPTION_MENU_CUSTOM_BOOT
        case 13: // Custom Boot
            OptionMenuCustomBoot();
            break;
#endif
#ifdef HAS_OPTION_MENU_REBOOT
        case 14: // Reboot
            OptionMenuReboot();
            break;
#endif
    }
}

/*
 * Display the selected NT-specific boot options at the bottom of the screen.
 */
VOID DisplayBootTimeOptions(VOID)
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

    CHAR BootOptions[260] = "";

    ASSERT(BootOptionChoice < RTL_NUMBER_OF(OptionNames));
    if (BootOptionChoice != NO_OPTION) // && BootOptionChoice < RTL_NUMBER_OF(OptionNames)
        RtlStringCbCatA(BootOptions, sizeof(BootOptions), *OptionNames[BootOptionChoice]);

    if (BootFlags & BOOT_LOGGING)
    {
        /* Since these safe mode options come by default with boot logging,
         * don't show "Boot Logging" when one of these is selected;
         * instead just show the corresponding safe mode option name. */
        if ( (BootOptionChoice != SAFEBOOT) &&
             (BootOptionChoice != SAFEBOOT_NETWORK) &&
             (BootOptionChoice != SAFEBOOT_ALTSHELL) )
        {
            if (*BootOptions != ANSI_NULL)
                RtlStringCbCatA(BootOptions, sizeof(BootOptions), ", ");
            RtlStringCbCatA(BootOptions, sizeof(BootOptions), OptionsMenuList[4]);
        }
    }

    if (BootFlags & BOOT_VGA_MODE)
    {
        if (*BootOptions != ANSI_NULL)
            RtlStringCbCatA(BootOptions, sizeof(BootOptions), ", ");
        RtlStringCbCatA(BootOptions, sizeof(BootOptions), OptionsMenuList[5]);
    }

    if (BootFlags & BOOT_DEBUGGING)
    {
        if (*BootOptions != ANSI_NULL)
            RtlStringCbCatA(BootOptions, sizeof(BootOptions), ", ");
        RtlStringCbCatA(BootOptions, sizeof(BootOptions), OptionsMenuList[8]);
    }

    /* Display the chosen boot options */
    UiDrawText(0,
               UiScreenHeight - 2,
               BootOptions,
               ATTR(COLOR_LIGHTBLUE, UiMenuBgColor));
}
