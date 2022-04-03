/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Windows-compatible NT OS Setup Loader.
 * COPYRIGHT:   Copyright 2009-2019 Aleksey Bragin <aleksey@reactos.org>
 *              Copyright 2022 Hermès Bélusca-Maïto
 */

#include <freeldr.h>
#include <ndk/ldrtypes.h>
#include <arc/setupblk.h>
#include "winldr.h"
#include "inffile.h"
#include "setupldr.h"
#include "ntldropts.h"

#include <debug.h>
DBG_DEFAULT_CHANNEL(WINDOWS);

// TODO: Move to .h
VOID
AllocateAndInitLPB(
    IN USHORT VersionToBoot,
    OUT PLOADER_PARAMETER_BLOCK* OutLoaderBlock);

/*static*/ VOID
SetupLdrLoadNlsData(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ HINF InfHandle,
    _In_ PCSTR SearchPath)
{
    BOOLEAN Success;
    INFCONTEXT InfContext;
    PCSTR AnsiData;
    UNICODE_STRING AnsiFileName = {0};
    UNICODE_STRING OemFileName = {0};
    UNICODE_STRING LangFileName = {0}; // CaseTable
    UNICODE_STRING OemHalFileName = {0};

    /* Get ANSI codepage file */
    if (!InfFindFirstLine(InfHandle, "NLS", "AnsiCodepage", &InfContext) ||
        !InfGetDataField(&InfContext, 1, &AnsiData) ||
        !RtlCreateUnicodeStringFromAsciiz(&AnsiFileName, AnsiData))
    {
        ERR("Failed to find or get 'NLS/AnsiCodepage'\n");
        return;
    }

    /* Get OEM codepage file */
    if (!InfFindFirstLine(InfHandle, "NLS", "OemCodepage", &InfContext) ||
        !InfGetDataField(&InfContext, 1, &AnsiData) ||
        !RtlCreateUnicodeStringFromAsciiz(&OemFileName, AnsiData))
    {
        ERR("Failed to find or get 'NLS/OemCodepage'\n");
        goto Quit;
    }

    /* Get the Unicode case table file */
    if (!InfFindFirstLine(InfHandle, "NLS", "UnicodeCasetable", &InfContext) ||
        !InfGetDataField(&InfContext, 1, &AnsiData) ||
        !RtlCreateUnicodeStringFromAsciiz(&LangFileName, AnsiData))
    {
        ERR("Failed to find or get 'NLS/UnicodeCasetable'\n");
        goto Quit;
    }

    /* Get OEM HAL font file */
    if (!InfFindFirstLine(InfHandle, "NLS", "OemHalFont", &InfContext) ||
        !InfGetData(&InfContext, NULL, &AnsiData) ||
        !RtlCreateUnicodeStringFromAsciiz(&OemHalFileName, AnsiData))
    {
        WARN("Failed to find or get 'NLS/OemHalFont'\n");
        /* Ignore, this is an optional file */
        RtlInitEmptyUnicodeString(&OemHalFileName, NULL, 0);
    }

    TRACE("NLS data: '%wZ' '%wZ' '%wZ' '%wZ'\n",
          &AnsiFileName, &OemFileName, &LangFileName, &OemHalFileName);

    /* Load NLS data */
    Success = WinLdrLoadNLSData(LoaderBlock,
                                SearchPath,
                                &AnsiFileName,
                                &OemFileName,
                                &LangFileName,
                                &OemHalFileName);
    TRACE("NLS data loading %s\n", Success ? "successful" : "failed");
    (VOID)Success;

Quit:
    RtlFreeUnicodeString(&OemHalFileName);
    RtlFreeUnicodeString(&LangFileName);
    RtlFreeUnicodeString(&OemFileName);
    RtlFreeUnicodeString(&AnsiFileName);
}

/*static*/
BOOLEAN
SetupLdrInitErrataInf(
    IN OUT PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN HINF InfHandle,
    IN PCSTR SystemRoot)
{
    INFCONTEXT InfContext;
    PCSTR FileName;
    ULONG FileSize;
    PVOID PhysicalBase;
    CHAR ErrataFilePath[MAX_PATH];

    /* Retrieve the INF file name value */
    if (!InfFindFirstLine(InfHandle, "BiosInfo", "InfName", &InfContext))
    {
        WARN("Failed to find 'BiosInfo/InfName'\n");
        return FALSE;
    }
    if (!InfGetDataField(&InfContext, 1, &FileName))
    {
        WARN("Failed to read 'InfName' value\n");
        return FALSE;
    }

    RtlStringCbCopyA(ErrataFilePath, sizeof(ErrataFilePath), SystemRoot);
    RtlStringCbCatA(ErrataFilePath, sizeof(ErrataFilePath), FileName);

    /* Load the INF file */
    PhysicalBase = WinLdrLoadModule(ErrataFilePath, &FileSize, LoaderRegistryData);
    if (!PhysicalBase)
    {
        WARN("Could not load '%s'\n", ErrataFilePath);
        return FALSE;
    }

    LoaderBlock->Extension->EmInfFileImage = PaToVa(PhysicalBase);
    LoaderBlock->Extension->EmInfFileSize  = FileSize;

    return TRUE;
}

/*static*/ VOID
SetupLdrScanBootDrivers(
    _Inout_ PLIST_ENTRY BootDriverListHead,
    _In_ HINF InfHandle,
    _In_ PCSTR SearchPath)
{
    INFCONTEXT InfContext, dirContext;
    PCSTR Media, DriverName, dirIndex, ImagePath;
    BOOLEAN Success;
    WCHAR ImagePathW[MAX_PATH];
    WCHAR DriverNameW[256];

    UNREFERENCED_PARAMETER(SearchPath);

    /* Open INF section */
    if (!InfFindFirstLine(InfHandle, "SourceDisksFiles", NULL, &InfContext))
        goto Quit;

    /* Load all listed boot drivers */
    do
    {
        if (InfGetDataField(&InfContext, 7, &Media) &&
            InfGetDataField(&InfContext, 0, &DriverName) &&
            InfGetDataField(&InfContext, 13, &dirIndex))
        {
            if ((strcmp(Media, "x") == 0) &&
                InfFindFirstLine(InfHandle, "Directories", dirIndex, &dirContext) &&
                InfGetDataField(&dirContext, 1, &ImagePath))
            {
                /* Prepare image path */
                RtlStringCbPrintfW(ImagePathW, sizeof(ImagePathW),
                                   L"%S\\%S", ImagePath, DriverName);

                /* Convert name to unicode and remove .sys extension */
                RtlStringCbPrintfW(DriverNameW, sizeof(DriverNameW),
                                   L"%S", DriverName);
                DriverNameW[wcslen(DriverNameW) - 4] = UNICODE_NULL;

                /* Add it to the list */
                Success = WinLdrAddDriverToList(BootDriverListHead,
                                                FALSE,
                                                DriverNameW,
                                                ImagePathW,
                                                NULL,
                                                SERVICE_ERROR_NORMAL,
                                                -1);
                if (!Success)
                {
                    ERR("Could not add boot driver '%s'\n", DriverName);
                    /* Ignore and continue adding other drivers */
                }
            }
        }
    } while (InfFindNextLine(&InfContext, &InfContext));

Quit:
    /* Finally, add the boot filesystem driver to the list */
    if (BootFileSystem)
    {
        TRACE("Adding filesystem driver %S\n", BootFileSystem);
        Success = WinLdrAddDriverToList(BootDriverListHead,
                                        FALSE,
                                        BootFileSystem,
                                        NULL,
                                        L"Boot File System",
                                        SERVICE_ERROR_CRITICAL,
                                        -1);
        if (!Success)
            ERR("Failed to add filesystem driver %S\n", BootFileSystem);
    }
    else
    {
        TRACE("No required filesystem driver\n");
    }
}


/* SETUP STARTER **************************************************************/

/*
 * Update the options in the buffer pointed by LoadOptions, of maximum size
 * BufferSize, by first removing any specified options, and then adding any
 * other ones.
 *
 * OptionsToAdd is a NULL-terminated array of string buffer pointers that
 *    specify the options to be added into LoadOptions. Whether they are
 *    prepended or appended to LoadOptions is controlled via the Append
 *    parameter. The options are added in the order specified by the array.
 *
 * OptionsToRemove is a NULL-terminated array of string buffer pointers that
 *    specify the options to remove from LoadOptions. Specifying also there
 *    any options to add, has the effect of removing from LoadOptions any
 *    duplicates of the options to be added, before adding them later into
 *    LoadOptions. The options are removed in the order specified by the array.
 *
 * The options string buffers in the OptionsToRemove array have the format:
 *    "/option1 /option2[=] ..."
 *
 * An option in the OptionsToRemove list with a trailing '=' or ':' designates
 * an option in LoadOptions with user-specific data appended after the sign.
 * When such an option is being removed from LoadOptions, all the appended
 * data is also removed until the next option.
 */
VOID
NtLdrUpdateLoadOptions(
    IN OUT PSTR LoadOptions,
    IN ULONG BufferSize,
    IN BOOLEAN Append,
    IN PCSTR OptionsToAdd[] OPTIONAL,
    IN PCSTR OptionsToRemove[] OPTIONAL)
{
    PCSTR NextOptions, NextOpt;
    PSTR Options, Option;
    ULONG NextOptLength;
    ULONG OptionLength;

    if (!LoadOptions || (BufferSize == 0))
        return;
    // ASSERT(strlen(LoadOptions) + 1 <= BufferSize);

    /* Loop over the options to remove */
    for (; OptionsToRemove && *OptionsToRemove; ++OptionsToRemove)
    {
        NextOptions = *OptionsToRemove;
        while ((NextOpt = NtLdrGetNextOption(&NextOptions, &NextOptLength)))
        {
            /* Scan the load options */
            Options = LoadOptions;
            while ((Option = (PSTR)NtLdrGetNextOption((PCSTR*)&Options, &OptionLength)))
            {
                /*
                 * Check whether the option to find exactly matches the current
                 * load option, or is a prefix thereof if this is an option with
                 * appended data.
                 */
                if ((OptionLength >= NextOptLength) &&
                    (_strnicmp(Option, NextOpt, NextOptLength) == 0))
                {
                    if ((OptionLength == NextOptLength) ||
                        (NextOpt[NextOptLength-1] == '=') ||
                        (NextOpt[NextOptLength-1] == ':'))
                    {
                        /* Eat any skipped option or whitespace separators */
                        while ((Option > LoadOptions) &&
                               (Option[-1] == '/' ||
                                Option[-1] == ' ' ||
                                Option[-1] == '\t'))
                        {
                            --Option;
                        }

                        /* If the option was not preceded by a whitespace
                         * separator, insert one and advance the pointer. */
                        if ((Option > LoadOptions) &&
                            (Option[-1] != ' ') &&
                            (Option[-1] != '\t') &&
                            (*Options != '\0') /* &&
                            ** Not necessary since NtLdrGetNextOption() **
                            ** stripped any leading separators.         **
                            (*Options != ' ') &&
                            (*Options != '\t') */)
                        {
                            *Option++ = ' ';
                        }

                        /* Move the remaining options back, erasing the current one */
                        ASSERT(Option <= Options);
                        RtlMoveMemory(Option,
                                      Options,
                                      (strlen(Options) + 1) * sizeof(CHAR));

                        /* Reset the iterator */
                        Options = Option;
                    }
                }
            }
        }
    }

    /* Now loop over the options to add */
    for (; OptionsToAdd && *OptionsToAdd; ++OptionsToAdd)
    {
        NtLdrAddOptions(LoadOptions,
                        BufferSize,
                        Append,
                        *OptionsToAdd);
    }
}


/*
 * List of options and their corresponding higher priority ones,
 * that are either checked before any other ones, or whose name
 * includes another option name as a subset (e.g. NODEBUG vs. DEBUG).
 * See also https://geoffchappell.com/notes/windows/boot/editoptions.htm
 */
static const struct
{
    PCSTR Options;
    PCSTR ExtraOptions;
    PCSTR HigherPriorOptions;
} HighPriorOptionsMap[] =
{
    /* NODEBUG has a higher precedence than DEBUG */
    {"/DEBUG/DEBUG=", NULL, "/NODEBUG"},

    /* When using SCREEN debug port, we need boot video */
    {"/DEBUGPORT=SCREEN", NULL, "/NOGUIBOOT"},

    /* DETECTHAL has a higher precedence than HAL= or KERNEL= */
    {"/HAL=/KERNEL=", NULL, "/DETECTHAL"},

    /* NOPAE has a higher precedence than PAE */
    {"/PAE", NULL, "/NOPAE"},

    /* NOEXECUTE(=) has a higher precedence than EXECUTE */
    {"/EXECUTE", "/NOEXECUTE=ALWAYSOFF", "/NOEXECUTE/NOEXECUTE="},
    /* NOEXECUTE(=) options are self-excluding and
     * some have higher precedence than others. */
    {"/NOEXECUTE/NOEXECUTE=", NULL, "/NOEXECUTE/NOEXECUTE="},

    /* SAFEBOOT(:) options are self-excluding */
    {"/SAFEBOOT/SAFEBOOT:", NULL, "/SAFEBOOT/SAFEBOOT:"},
};

#define TAG_BOOT_OPTIONS 'pOtB'

VOID
NtLdrGetHigherPriorityOptions(
    IN PCSTR BootOptions,
    OUT PSTR* ExtraOptions,
    OUT PSTR* HigherPriorityOptions)
{
    ULONG i;
    PCSTR NextOptions, NextOpt;
    ULONG NextOptLength;
    SIZE_T ExtraOptsSize = 0;
    SIZE_T HighPriorOptsSize = 0;

    /* Masks specifying the presence (TRUE) or absence (FALSE) of the options */
    BOOLEAN Masks[RTL_NUMBER_OF(HighPriorOptionsMap)];

    /* Just return if we cannot return anything */
    if (!ExtraOptions && !HigherPriorityOptions)
        return;

    if (ExtraOptions)
        *ExtraOptions = NULL;
    if (HigherPriorityOptions)
        *HigherPriorityOptions = NULL;

    /* Just return if no initial options were given */
    if (!BootOptions || !*BootOptions)
        return;

    /* Determine the presence of the colliding options, and the
     * maximum necessary sizes for the pointers to be allocated. */
    RtlZeroMemory(Masks, sizeof(Masks));
    for (i = 0; i < RTL_NUMBER_OF(HighPriorOptionsMap); ++i)
    {
        /* Loop over the given options to search for */
        NextOptions = HighPriorOptionsMap[i].Options;
        while ((NextOpt = NtLdrGetNextOption(&NextOptions, &NextOptLength)))
        {
            /* If any of these options are present... */
            if (NtLdrGetOptionExN(BootOptions, NextOpt, NextOptLength, NULL))
            {
                /* ... set the mask, retrieve the sizes and stop looking for these options */
                Masks[i] = TRUE;
                if (ExtraOptions && HighPriorOptionsMap[i].ExtraOptions)
                {
                    ExtraOptsSize += strlen(HighPriorOptionsMap[i].ExtraOptions) * sizeof(CHAR);
                }
                if (HigherPriorityOptions && HighPriorOptionsMap[i].HigherPriorOptions)
                {
                    HighPriorOptsSize += strlen(HighPriorOptionsMap[i].HigherPriorOptions) * sizeof(CHAR);
                }
                break;
            }
        }
    }
    /* Count the NULL-terminator */
    if (ExtraOptions)
        ExtraOptsSize += sizeof(ANSI_NULL);
    if (HigherPriorityOptions)
        HighPriorOptsSize += sizeof(ANSI_NULL);

    /* Allocate the string pointers */
    if (ExtraOptions)
    {
        *ExtraOptions = FrLdrHeapAlloc(ExtraOptsSize, TAG_BOOT_OPTIONS);
        if (!*ExtraOptions)
            return;
    }
    if (HigherPriorityOptions)
    {
        *HigherPriorityOptions = FrLdrHeapAlloc(HighPriorOptsSize, TAG_BOOT_OPTIONS);
        if (!*HigherPriorityOptions)
        {
            if (ExtraOptions)
            {
                FrLdrHeapFree(*ExtraOptions, TAG_BOOT_OPTIONS);
                *ExtraOptions = NULL;
            }
            return;
        }
    }

    /* Initialize the strings */
    if (ExtraOptions)
        *(*ExtraOptions) = '\0';
    if (HigherPriorityOptions)
        *(*HigherPriorityOptions) = '\0';

    /* Go through the masks that determine the options to check */
    for (i = 0; i < RTL_NUMBER_OF(HighPriorOptionsMap); ++i)
    {
        if (Masks[i])
        {
            /* Retrieve the strings */
            if (ExtraOptions && HighPriorOptionsMap[i].ExtraOptions)
            {
                RtlStringCbCatA(*ExtraOptions,
                                ExtraOptsSize,
                                HighPriorOptionsMap[i].ExtraOptions);
            }
            if (HigherPriorityOptions && HighPriorOptionsMap[i].HigherPriorOptions)
            {
                RtlStringCbCatA(*HigherPriorityOptions,
                                HighPriorOptsSize,
                                HighPriorOptionsMap[i].HigherPriorOptions);
            }
        }
    }
}

VOID
SetupLdrPostProcessBootOptions(
    _Out_z_bytecap_(BootOptionsSize)
         PSTR BootOptions,
    _In_ SIZE_T BootOptionsSize,
    _In_ PCSTR ArgsBootOptions,
    // _In_ ULONG Argc,
    // _In_ PCHAR Argv[],
    _In_ HINF InfHandle)
{
    INFCONTEXT InfContext;

    // UseLocalSif = NtLdrGetOption(ArgsBootOptions, "USELOCALSIF");
    if (NtLdrGetOption(ArgsBootOptions, "SIFOPTIONSOVERRIDE"))
    {
        PCSTR OptionsToRemove[2] = {"SIFOPTIONSOVERRIDE", NULL};

        /* Do not use any load options from TXTSETUP.SIF, but
         * use instead those passed from the command line. */
        RtlStringCbCopyA(BootOptions, BootOptionsSize, ArgsBootOptions);

        /* Remove the private switch from the options */
        NtLdrUpdateLoadOptions(BootOptions,
                               BootOptionsSize,
                               FALSE,
                               NULL,
                               OptionsToRemove);
    }
    else // if (!*ArgsBootOptions || NtLdrGetOption(ArgsBootOptions, "SIFOPTIONSADD"))
    {
        PCSTR LoadOptions = NULL;
        PCSTR DbgLoadOptions = NULL;
        PSTR ExtraOptions, HigherPriorityOptions;
        PSTR OptionsToAdd[3];
        PSTR OptionsToRemove[4];

        /* Load the options from TXTSETUP.SIF */
        if (InfFindFirstLine(InfHandle, "SetupData", "OsLoadOptions", &InfContext))
        {
            if (!InfGetDataField(&InfContext, 1, &LoadOptions))
                WARN("Failed to get load options\n");
        }

#if !DBG
        /* Non-debug mode: get the debug load options only if /DEBUG was specified
         * in the Argv command-line options (was e.g. added to the options when
         * the user selected "Debugging Mode" in the advanced boot menu). */
        if (NtLdrGetOption(ArgsBootOptions, "DEBUG") ||
            NtLdrGetOption(ArgsBootOptions, "DEBUG="))
        {
#else
        /* Debug mode: always get the debug load options */
#endif
        if (InfFindFirstLine(InfHandle, "SetupData", "SetupDebugOptions", &InfContext))
        {
            if (!InfGetDataField(&InfContext, 1, &DbgLoadOptions))
                WARN("Failed to get debug load options\n");
        }
        /* If none was found, default to enabling debugging */
        if (!DbgLoadOptions)
            DbgLoadOptions = "/DEBUG";
#if !DBG
        }
#endif

        /* Initialize the load options with those from TXTSETUP.SIF */
        *BootOptions = ANSI_NULL;
        if (LoadOptions && *LoadOptions)
            RtlStringCbCopyA(BootOptions, BootOptionsSize, LoadOptions);

        /* Merge the debug load options if any */
        if (DbgLoadOptions)
        {
            RtlZeroMemory(OptionsToAdd, sizeof(OptionsToAdd));
            RtlZeroMemory(OptionsToRemove, sizeof(OptionsToRemove));

            /*
             * Retrieve any option patterns that we should remove from the
             * SIF load options because they are of higher precedence than
             * those specified in the debug load options to be added.
             * Also always remove NODEBUG (even if the debug load options
             * do not contain explicitly the DEBUG option), since we want
             * to have debugging enabled if possible.
             */
            OptionsToRemove[0] = "/NODEBUG";
            NtLdrGetHigherPriorityOptions(DbgLoadOptions,
                                          &ExtraOptions,
                                          &HigherPriorityOptions);
            OptionsToAdd[1] = (ExtraOptions ? ExtraOptions : "");
            OptionsToRemove[1] = (HigherPriorityOptions ? HigherPriorityOptions : "");

            /*
             * Prepend the debug load options, so that in case it contains
             * redundant options with respect to the SIF load options, the
             * former can take precedence over the latter.
             */
            OptionsToAdd[0] = (PSTR)DbgLoadOptions;
            OptionsToRemove[2] = (PSTR)DbgLoadOptions;
            NtLdrUpdateLoadOptions(BootOptions,
                                   BootOptionsSize,
                                   FALSE,
                                   (PCSTR*)OptionsToAdd,
                                   (PCSTR*)OptionsToRemove);

            if (ExtraOptions)
                FrLdrHeapFree(ExtraOptions, TAG_BOOT_OPTIONS);
            if (HigherPriorityOptions)
                FrLdrHeapFree(HigherPriorityOptions, TAG_BOOT_OPTIONS);
        }

        RtlZeroMemory(OptionsToAdd, sizeof(OptionsToAdd));
        RtlZeroMemory(OptionsToRemove, sizeof(OptionsToRemove));

        /*
         * Retrieve any option patterns that we should remove from the
         * SIF load options because they are of higher precedence than
         * those specified in the options to be added.
         */
        NtLdrGetHigherPriorityOptions(ArgsBootOptions,
                                      &ExtraOptions,
                                      &HigherPriorityOptions);
        OptionsToAdd[1] = (ExtraOptions ? ExtraOptions : "");
        OptionsToRemove[0] = (HigherPriorityOptions ? HigherPriorityOptions : "");

        /* Finally, prepend the user-specified options that
         * take precedence over those from TXTSETUP.SIF. */
        OptionsToAdd[0] = (PSTR)ArgsBootOptions;
        OptionsToRemove[1] = (PSTR)ArgsBootOptions;
        NtLdrUpdateLoadOptions(BootOptions,
                               BootOptionsSize,
                               FALSE,
                               (PCSTR*)OptionsToAdd,
                               (PCSTR*)OptionsToRemove);

        if (ExtraOptions)
            FrLdrHeapFree(ExtraOptions, TAG_BOOT_OPTIONS);
        if (HigherPriorityOptions)
            FrLdrHeapFree(HigherPriorityOptions, TAG_BOOT_OPTIONS);
    }
}

ARC_STATUS
SetupLdrFindConfigSource(
    _Out_ PHINF InfHandle,
    _Inout_z_bytecap_(BootPathSize)
         PSTR BootPath,
    _In_ SIZE_T BootPathSize,
    _Out_z_bytecap_(FilePathSize)
         PSTR FilePath,
    _In_ SIZE_T FilePathSize)
{
    PSTR FileName;
    ULONG FileNameLength;
    PCSTR SystemPath;
    ULONG i, ErrorLine;
    BOOLEAN BootFromFloppy;

    static PCSTR SourcePaths[] =
    {
        "", /* Only for floppy boot */
#if defined(_M_IX86)
        "I386\\",
#elif defined(_M_AMD64)
        "AMD64\\",
#elif defined(_M_ARM)
        "ARM\\",
#elif defined(_M_ARM64)
        "ARM64\\",
#elif defined(_M_MPPC)
        "PPC\\",
#elif defined(_M_MRX000)
        "MIPS\\",
#endif
        "reactos\\",
    };

    /* Check if we booted from floppy */
    BootFromFloppy = (strstr(BootPath, ")fdisk(") != NULL);

    /* Open 'txtsetup.sif' from any of the source paths */
    FileName = BootPath + strlen(BootPath);
    for (i = BootFromFloppy ? 0 : 1; ; i++)
    {
        if (i >= RTL_NUMBER_OF(SourcePaths))
        {
            UiMessageBox("Failed to open txtsetup.sif");
            return ENOENT;
        }

        SystemPath = SourcePaths[i];

        /* Adjust the tentative BootPath */
        FileNameLength = (ULONG)(BootPathSize - (FileName - BootPath)*sizeof(CHAR));
        RtlStringCbCopyA(FileName, FileNameLength, SystemPath);

        /* Try to open 'txtsetup.sif' from this boot path */
        RtlStringCbCopyA(FilePath, FilePathSize, BootPath);
        RtlStringCbCatA(FilePath, FilePathSize, "txtsetup.sif");
        if (InfOpenFile(InfHandle, FilePath, &ErrorLine))
        {
            /* Found and opened it: txtsetup.sif is in the correct BootPath */
            break;
        }
        else
        {
            if (ErrorLine != -1)
                UiMessageBox("Error in %s at line %lu", FilePath, ErrorLine);
        }
    }

    TRACE("BootPath: '%s', SystemPath: '%s'\n", BootPath, SystemPath);
    return ESUCCESS;
}

/* EOF */
