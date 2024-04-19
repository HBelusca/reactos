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

/* GLOBALS ********************************************************************/

#if defined(_M_IX86) || defined(_M_AMD64)
static const PCSTR BootSectorFilePrompt =
    "Enter the boot sector file path.\n"
    "Leave blank for booting a disk or partition.\n"
    "\n"
    "Examples:\n"
    "\\BOOTSECT.DOS\n"
    "/boot/bootsect.dos";
static const PCSTR LinuxKernelPrompt =
    "Enter the Linux kernel image path.\n"
    "\n"
    "Examples:\n"
    "/vmlinuz\n"
    "/boot/vmlinuz-2.4.18";
static const PCSTR LinuxInitrdPrompt =
    "Enter the initrd image path.\n"
    "Leave blank for no initial ramdisk.\n"
    "\n"
    "Examples:\n"
    "/initrd.gz\n"
    "/boot/root.img.gz";
static const PCSTR LinuxCommandLinePrompt =
    "Enter the Linux kernel command line.\n"
    "\n"
    "Examples:\n"
    "root=/dev/hda1\n"
    "root=/dev/fd0 read-only\n"
    "root=/dev/sdb1 init=/sbin/init";
#endif /* _M_IX86 || _M_AMD64 */

static const PCSTR BootPathPromptFmt =
    "Enter the boot path.\n"
    "The device/partition part can be specified with\n"
    "either a full ARC path, or using the format:\n"
    "  dev#%s,part#%s\n"
    "where dev# is a zero-based drive identifier:\n"
    "  fd# for floppy disks,   hd# for hard disks,\n"
    "  cd# for CD-ROMs,        rd# for ramdisks;\n"
    "and part# is%s partition.\n"
//  "or 0 for the active (bootable) partition.\n"
    /* NOTE: "Active"/bootable partition is a per-platform concept,
     * and may not really exist. In addition, partition(0) in ARC
     * means the whole disk (in non-partitioned access).
     * Commit f2854a864 (r17736) and CORE-156 are thus inaccurate
     * in this regard. */
    "\n"
    "Examples:\n"
    "multi(0)disk(0)fdisk(0)\n"
    "multi(0)disk(0)rdisk(0)partition(1)\n"
    "\n"
    "fd0 - 1st floppy drive;   hd0 - 1st hard drive;\n"
    "hd1,1 - 2nd hard drive, 1st partition."
#if (defined(_M_IX86) || defined(_M_AMD64)) && !defined(SARCH_XBOX) && !defined(UEFIBOOT)
/* Extra prompt piece for BIOS-based PC builds *ONLY* */
    "\n\n"
    "BIOS drive numbers may also be used:\n"
    "0 - 1st floppy drive;\n"
    "0x80 - 1st hard drive;   0x81 - 2nd hard drive."
#endif /* (_M_IX86 || _M_AMD64) && !SARCH_XBOX && !UEFIBOOT */
    ;

static const PCSTR ARCPathPrompt =
    "Enter the boot ARC path.\n"
    "\n"
    "Examples:\n"
    "multi(0)disk(0)rdisk(0)partition(1)\n"
    "multi(0)disk(0)fdisk(0)";

static const PCSTR ReactOSSystemPathPrompt =
    "Enter the path to your ReactOS system directory.\n"
    "\n"
    "Examples:\n"
    "\\REACTOS\n"
    "\\ROS";
static const PCSTR ReactOSOptionsPrompt =
    "Enter the load options you want passed to the kernel.\n"
    "\n"
    "Examples:\n"
    "/DEBUG /DEBUGPORT=COM1 /BAUDRATE=115200\n"
    "/FASTDETECT /SOS /NOGUIBOOT\n"
    "/BASEVIDEO /MAXMEM=64\n"
    "/KERNEL=NTKRNLMP.EXE /HAL=HALMPS.DLL";
static const PCSTR ReactOSSetupOptionsPrompt =
    "Enter additional load options you want passed to the ReactOS Setup.\n"
    "These options will supplement those obtained from the TXTSETUP.SIF\n"
    "file, unless you also specify the /SIFOPTIONSOVERRIDE option switch.\n"
    "\n"
    "Example:\n"
    "/DEBUG /DEBUGPORT=COM1 /BAUDRATE=115200 /NOGUIBOOT";

static const PCSTR CustomBootPrompt =
    "Press ENTER to boot your custom boot setup.";

/* FUNCTIONS ******************************************************************/

#ifdef HAS_OPTION_MENU_CUSTOM_BOOT

VOID OptionMenuCustomBoot(VOID)
{
    PCSTR CustomBootMenuList[] = {
#if defined(_M_IX86) || defined(_M_AMD64)
        "Boot Sector (Disk/Partition/File)",
        "Linux",
#endif
        "ReactOS",
        "ReactOS Setup"
        };
    ULONG SelectedMenuItem;
    OperatingSystemItem OperatingSystem;

    if (!UiDisplayMenu("Please choose a boot method:", NULL,
                       FALSE,
                       CustomBootMenuList,
                       RTL_NUMBER_OF(CustomBootMenuList),
                       0, -1,
                       &SelectedMenuItem,
                       TRUE,
                       NULL, NULL))
    {
        /* The user pressed ESC */
        return;
    }

    /* Initialize a new custom OS entry */
    OperatingSystem.SectionId = 0;
    switch (SelectedMenuItem)
    {
#if defined(_M_IX86) || defined(_M_AMD64)
        case 0: // Boot Sector (Disk/Partition/File)
            EditCustomBootSector(&OperatingSystem);
            break;
        case 1: // Linux
            EditCustomBootLinux(&OperatingSystem);
            break;
        case 2: // ReactOS
            EditCustomBootReactOS(&OperatingSystem, FALSE);
            break;
        case 3: // ReactOS Setup
            EditCustomBootReactOS(&OperatingSystem, TRUE);
            break;
#else
        case 0: // ReactOS
            EditCustomBootReactOS(&OperatingSystem, FALSE);
            break;
        case 1: // ReactOS Setup
            EditCustomBootReactOS(&OperatingSystem, TRUE);
            break;
#endif /* _M_IX86 || _M_AMD64 */
    }

    /* And boot it */
    if (OperatingSystem.SectionId != 0)
    {
        UiMessageBox(CustomBootPrompt);
        LoadOperatingSystem(&OperatingSystem);
    }
}

#endif // HAS_OPTION_MENU_CUSTOM_BOOT

static BOOLEAN
RetrieveBootPath(
    _Inout_ PCHAR ArcPath,
    _In_ ULONG ArcPathLength,
    _In_ BOOLEAN OptionalPartition,
    _Out_ PBOOLEAN HasPartition)
{
    CHAR BootPathPrompt[580];
    if (OptionalPartition)
    {
        RtlStringCbPrintfA(BootPathPrompt, sizeof(BootPathPrompt),
                           BootPathPromptFmt, "[", "]", " an optional");
    }
    else
    {
        RtlStringCbPrintfA(BootPathPrompt, sizeof(BootPathPrompt),
                           BootPathPromptFmt, "", "", " the");
    }

retry_boot_drive_part:
    if (!UiEditBox(BootPathPrompt, ArcPath, ArcPathLength))
        return FALSE;

    if (!*ArcPath)
    {
        *HasPartition = FALSE;
        return TRUE;
    }

    /* Do the ARC mapping now */
    if (!ExpandPath(ArcPath, ArcPathLength))
    {
        UiMessageBox("Invalid boot drive/partition value: %s", ArcPath);
        goto retry_boot_drive_part;
    }

    // if (!UiEditBox(ARCPathPrompt, ArcPath, ArcPathLength))
    //     return FALSE;
    /* Check for a non-zero partition specifier */
    *HasPartition = (!strstr(ArcPath, ")partition()") &&
                     !strstr(ArcPath, ")partition(0)"));

    return TRUE;
}


#if defined(_M_IX86) || defined(_M_AMD64)

#ifdef HAS_DEPRECATED_OPTIONS
BOOLEAN
ConvertDeprecatedBootDrivePart(
    _In_ ULONG_PTR SectionId,
    _Out_ PSTR PathBuffer,
    _In_ ULONG BufferSize);
#endif // HAS_DEPRECATED_OPTIONS

VOID
EditCustomBootSector(
    _Inout_ OperatingSystemItem* OperatingSystem)
{
    TIMEINFO* TimeInfo;
    ULONG_PTR SectionId = OperatingSystem->SectionId;
    CHAR SectionName[100];
    CHAR BootArcPath[200];
    CHAR BootSectorFile[200];
    BOOLEAN HasPartition = FALSE;

    RtlZeroMemory(SectionName, sizeof(SectionName));
    RtlZeroMemory(BootArcPath, sizeof(BootArcPath));
    RtlZeroMemory(BootSectorFile, sizeof(BootSectorFile));

    if (SectionId != 0)
    {
        /* Load the settings */
        IniReadSettingByName(SectionId, "BootPath", BootArcPath, sizeof(BootArcPath));
////
#ifdef HAS_DEPRECATED_OPTIONS
        /* If we don't have a "BootPath" value, check for the DEPRECATED values */
        if (!*BootArcPath)
            ConvertDeprecatedBootDrivePart(SectionId, BootArcPath, sizeof(BootArcPath));
#endif // HAS_DEPRECATED_OPTIONS
////

        /* Always load the file name; it will only be handled later if a partition has been specified */
        IniReadSettingByName(SectionId, "BootSectorFile", BootSectorFile, sizeof(BootSectorFile));
    }

    /* Retrieve the corresponding boot path */
    if (!RetrieveBootPath(BootArcPath, sizeof(BootArcPath),
                          TRUE /* optionalpartition */,
                          &HasPartition))
    {
        return;
    }

    /* Edit the file name only if a partition has been specified */
    if (HasPartition &&
        !UiEditBox(BootSectorFilePrompt, BootSectorFile, sizeof(BootSectorFile)))
    {
        return;
    }

    /* Modify the settings values and return if we were in edit mode */
    if (SectionId != 0)
    {
        /* Modify or reset the BootPath. If reset, BootSectorFile
         * will be relative to the default system partition. */
        IniModifySettingValue(SectionId, "BootPath", BootArcPath);

#ifdef HAS_DEPRECATED_OPTIONS
        /* Deprecate the other values */
        IniModifySettingValue(SectionId, "BootDrive", "");
        IniModifySettingValue(SectionId, "BootPartition", "");
#endif

        /* Always write back the file name */
        IniModifySettingValue(SectionId, "BootSectorFile", BootSectorFile);
        return;
    }

    /* Generate a unique section name */
    TimeInfo = ArcGetTime();
    RtlStringCbPrintfA(SectionName, sizeof(SectionName),
                       "CustomBootSector%u%u%u%u%u%u",
                       TimeInfo->Year, TimeInfo->Day, TimeInfo->Month,
                       TimeInfo->Hour, TimeInfo->Minutes, TimeInfo->Seconds);

    /* Add the section */
    if (!IniAddSection(SectionName, &SectionId))
        return;

    /* Add the BootType */
    if (!IniAddSettingValueToSection(SectionId, "BootType", "BootSector"))
        return;

    /* Add the BootPath if we have one */
    if (*BootArcPath && !IniAddSettingValueToSection(SectionId, "BootPath", BootArcPath))
        return;

    /* Add the BootSectorFile if any */
    if (*BootSectorFile && !IniAddSettingValueToSection(SectionId, "BootSectorFile", BootSectorFile))
        return;

    OperatingSystem->SectionId = SectionId;
    OperatingSystem->LoadIdentifier = NULL;
}

VOID
EditCustomBootLinux(
    _Inout_ OperatingSystemItem* OperatingSystem)
{
    TIMEINFO* TimeInfo;
    ULONG_PTR SectionId = OperatingSystem->SectionId;
    CHAR SectionName[100];
    CHAR BootArcPath[200];
    CHAR KernelString[200];
    CHAR InitrdString[200];
    CHAR CommandLine[200];
    BOOLEAN HasPartition = FALSE;

    RtlZeroMemory(SectionName, sizeof(SectionName));
    RtlZeroMemory(BootArcPath, sizeof(BootArcPath));
    RtlZeroMemory(KernelString, sizeof(KernelString));
    RtlZeroMemory(InitrdString, sizeof(InitrdString));
    RtlZeroMemory(CommandLine, sizeof(CommandLine));

    if (SectionId != 0)
    {
        /* Load the settings */
        IniReadSettingByName(SectionId, "BootPath", BootArcPath, sizeof(BootArcPath));
////
#ifdef HAS_DEPRECATED_OPTIONS
        /* If we don't have a "BootPath" value, check for the DEPRECATED values */
        if (!*BootArcPath)
            ConvertDeprecatedBootDrivePart(SectionId, BootArcPath, sizeof(BootArcPath));
#endif // HAS_DEPRECATED_OPTIONS
////

        IniReadSettingByName(SectionId, "Kernel", KernelString, sizeof(KernelString));
        IniReadSettingByName(SectionId, "Initrd", InitrdString, sizeof(InitrdString));
        IniReadSettingByName(SectionId, "CommandLine", CommandLine, sizeof(CommandLine));
    }

retry_boot_path:
    /* Retrieve the corresponding boot path */
    if (!RetrieveBootPath(BootArcPath, sizeof(BootArcPath),
                          FALSE /* NOT optionalpartition */,
                          &HasPartition))
    {
        return;
    }
    /* A partition specification is mandatory */
    if (!HasPartition)
    {
        UiMessageBox("Missing boot partition!");
        goto retry_boot_path;
    }

    if (!UiEditBox(LinuxKernelPrompt, KernelString, sizeof(KernelString)))
        return;

    if (!UiEditBox(LinuxInitrdPrompt, InitrdString, sizeof(InitrdString)))
        return;

    if (!UiEditBox(LinuxCommandLinePrompt, CommandLine, sizeof(CommandLine)))
        return;

    /* Modify the settings values and return if we were in edit mode */
    if (SectionId != 0)
    {
        /* Modify or reset the BootPath. If reset, the files
         * will be relative to the default system partition. */
        IniModifySettingValue(SectionId, "BootPath", BootArcPath);

#ifdef HAS_DEPRECATED_OPTIONS
        /* Deprecate the other values */
        IniModifySettingValue(SectionId, "BootDrive", "");
        IniModifySettingValue(SectionId, "BootPartition", "");
#endif

        /* Always write back the file names */
        IniModifySettingValue(SectionId, "Kernel", KernelString);
        IniModifySettingValue(SectionId, "Initrd", InitrdString);
        IniModifySettingValue(SectionId, "CommandLine", CommandLine);
        return;
    }

    /* Generate a unique section name */
    TimeInfo = ArcGetTime();
    RtlStringCbPrintfA(SectionName, sizeof(SectionName),
                       "CustomLinux%u%u%u%u%u%u",
                       TimeInfo->Year, TimeInfo->Day, TimeInfo->Month,
                       TimeInfo->Hour, TimeInfo->Minutes, TimeInfo->Seconds);

    /* Add the section */
    if (!IniAddSection(SectionName, &SectionId))
        return;

    /* Add the BootType */
    if (!IniAddSettingValueToSection(SectionId, "BootType", "Linux"))
        return;

    /* Add the BootPath if we have one */
    if (*BootArcPath && !IniAddSettingValueToSection(SectionId, "BootPath", BootArcPath))
        return;

    /* Add the Kernel */
    if (!IniAddSettingValueToSection(SectionId, "Kernel", KernelString))
        return;

    /* Add the Initrd if any */
    if (*InitrdString && !IniAddSettingValueToSection(SectionId, "Initrd", InitrdString))
        return;

    /* Add the CommandLine */
    if (!IniAddSettingValueToSection(SectionId, "CommandLine", CommandLine))
        return;

    OperatingSystem->SectionId = SectionId;
    OperatingSystem->LoadIdentifier = "Custom Linux Setup";
}

#endif /* _M_IX86 || _M_AMD64 */

VOID
EditCustomBootReactOS(
    _Inout_ OperatingSystemItem* OperatingSystem,
    _In_ BOOLEAN IsSetup)
{
    TIMEINFO* TimeInfo;
    ULONG_PTR SectionId = OperatingSystem->SectionId;
    CHAR SectionName[100];
    CHAR SystemPath[200];
    CHAR ArcPath[200];
    CHAR Options[200];

    RtlZeroMemory(SectionName, sizeof(SectionName));
    RtlZeroMemory(SystemPath, sizeof(SystemPath));
    RtlZeroMemory(ArcPath, sizeof(ArcPath));
    RtlZeroMemory(Options, sizeof(Options));

    if (SectionId != 0)
    {
        /* Load the settings */
        IniReadSettingByName(SectionId, "SystemPath", ArcPath, sizeof(ArcPath));
        IniReadSettingByName(SectionId, "Options", Options, sizeof(Options));
    }

    if (SectionId == 0)
    {
        /* Construct the ReactOS ARC system path */
        BOOLEAN HasPartition = FALSE;
        RetrieveBootPath(ArcPath, sizeof(ArcPath),
                         TRUE /* optionalpartition */,
                         &HasPartition);

        if (!UiEditBox(ReactOSSystemPathPrompt, SystemPath, sizeof(SystemPath)))
            return;

        /* Append the sub-path */
        if (*SystemPath == '\\' || *SystemPath == '/')
        {
            RtlStringCbCatA(ArcPath, sizeof(ArcPath), SystemPath);
        }
        else
        {
            RtlStringCbCatA(ArcPath, sizeof(ArcPath), "\\");
            RtlStringCbCatA(ArcPath, sizeof(ArcPath), SystemPath);
        }
    }
    else
    {
        if (!UiEditBox(/*ReactOSSystemPathPrompt*/ARCPathPrompt, ArcPath, sizeof(ArcPath)))
            return;
    }

    if (!UiEditBox(IsSetup ? ReactOSSetupOptionsPrompt : ReactOSOptionsPrompt, Options, sizeof(Options)))
        return;

    /* Modify the settings values and return if we were in edit mode */
    if (SectionId != 0)
    {
        IniModifySettingValue(SectionId, "SystemPath", ArcPath);
        IniModifySettingValue(SectionId, "Options", Options);
        return;
    }

    /* Generate a unique section name */
    TimeInfo = ArcGetTime();
    RtlStringCbPrintfA(SectionName, sizeof(SectionName),
                       "CustomReactOS%u%u%u%u%u%u",
                       TimeInfo->Year, TimeInfo->Day, TimeInfo->Month,
                       TimeInfo->Hour, TimeInfo->Minutes, TimeInfo->Seconds);

    /* Add the section */
    if (!IniAddSection(SectionName, &SectionId))
        return;

    /* Add the BootType */
    if (!IniAddSettingValueToSection(SectionId, "BootType", IsSetup ? "ReactOSSetup" : "Windows2003"))
        return;

    /* Add the system path */
    if (!IniAddSettingValueToSection(SectionId, "SystemPath", ArcPath))
        return;

    /* Add the CommandLine */
    if (!IniAddSettingValueToSection(SectionId, "Options", Options))
        return;

    OperatingSystem->SectionId = SectionId;
    OperatingSystem->LoadIdentifier = NULL;
}

#ifdef HAS_OPTION_MENU_REBOOT

VOID OptionMenuReboot(VOID)
{
    UiMessageBox("The system will now reboot.");
    Reboot();
}

#endif // HAS_OPTION_MENU_REBOOT
