/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Setup Library
 * FILE:            base/setup/lib/install.c
 * PURPOSE:         Installation functions
 * PROGRAMMERS:     Eric Kohl
 *                  Hervé Poussineau (hpoussin@reactos.org)
 *                  Hermes Belusca-Maito (hermes.belusca@sfr.fr)
 */

/* INCLUDES *****************************************************************/

#include "precomp.h"
#include "filesup.h"
#include "infsupp.h"

#include "setuplib.h" // HAXX for USETUP_DATA!!

#include "install.h"

#define NDEBUG
#include <debug.h>


/* FUNCTIONS ****************************************************************/

static BOOL
LookupDirectoryById(
    IN HINF InfHandle,
    IN OUT PINFCONTEXT InfContext,
    IN PCWSTR DirId,
    OUT PCWSTR* pDirectory)
{
    BOOL Success;

    // ReactOS-specific
    Success = SpInfFindFirstLine(InfHandle, L"Directories", DirId, InfContext);
    if (!Success)
    {
        // Windows-compatible
        Success = SpInfFindFirstLine(InfHandle, L"WinntDirectories", DirId, InfContext);
        if (!Success)
            DPRINT1("SpInfFindFirstLine() failed\n");
    }
    if (Success)
    {
        Success = INF_GetData(InfContext, NULL, pDirectory);
        if (!Success)
            DPRINT1("INF_GetData() failed\n");
    }

    if (!Success)
        DPRINT1("LookupDirectoryById(%S) - directory not found!\n", DirId);

    return Success;
}

/*
 * Note: Modeled after SetupGetSourceFileLocation(), SetupGetSourceInfo()
 * and SetupGetTargetPath() APIs.
 * Technically the target path is the same for a given file section,
 * but here we try to remove this constraint.
 *
 * TXTSETUP.SIF entries syntax explained at:
 * http://www.msfn.org/board/topic/125480-txtsetupsif-syntax/
 */
static NTSTATUS
GetSourceFileAndTargetLocation(
    IN HINF InfHandle,
    IN PINFCONTEXT InfContext OPTIONAL,
    IN PCWSTR SourceFileName OPTIONAL,
    OUT PCWSTR* pSourceRootPath,
    OUT PCWSTR* pSourcePath,
    OUT PCWSTR* pTargetDirectory,
    OUT PCWSTR* pTargetFileName)
{
    BOOL Success;
    INFCONTEXT FileContext;
    INFCONTEXT DirContext;
    PCWSTR SourceRootDirId;
    PCWSTR SourceRootDir;
    PCWSTR SourceRelativePath;
    PCWSTR TargetDirId;
    PCWSTR TargetDir;
    PCWSTR TargetFileName;

    /* Either InfContext or SourceFileName must be specified */
    if (!InfContext && !SourceFileName)
        return STATUS_INVALID_PARAMETER;

    /* InfContext to a file was not given, retrieve one corresponding to SourceFileName */
    if (!InfContext)
    {
        /* Search for the SourceDisksFiles section */

        /* Search in the platform-specific first (currently hardcoded; make it runtime-dependent?) */
        Success = SpInfFindFirstLine(InfHandle, L"SourceDisksFiles." INF_ARCH, SourceFileName, &FileContext);
        if (!Success)
        {
            /* Search in the global section */
            Success = SpInfFindFirstLine(InfHandle, L"SourceDisksFiles", SourceFileName, &FileContext);
        }
        if (!Success)
        {
            // pSetupData->LastErrorNumber = ERROR_TXTSETUP_SECTION;
            // if (pSetupData->ErrorRoutine)
                // pSetupData->ErrorRoutine(pSetupData, SectionName);
            return STATUS_NOT_FOUND;
        }
        InfContext = &FileContext;
    }
    // else, InfContext != NULL and ignore SourceFileName (that may or may not be == NULL).

    /*
     * Getting Source File Location -- SetupGetSourceFileLocation()
     */

    /* Get source root directory id */
    if (!INF_GetDataField(InfContext, 1, &SourceRootDirId))
    {
        /* FIXME: Handle error! */
        DPRINT1("INF_GetData() failed\n");
        return STATUS_NOT_FOUND;
    }

    /* Lookup source root directory -- SetupGetSourceInfo() */
    /* Search in the platform-specific first (currently hardcoded; make it runtime-dependent?) */
    Success = SpInfFindFirstLine(InfHandle, L"SourceDisksNames." INF_ARCH, SourceRootDirId, &DirContext);
    if (!Success)
    {
        /* Search in the global section */
        Success = SpInfFindFirstLine(InfHandle, L"SourceDisksNames", SourceRootDirId, &DirContext);
        if (!Success)
            DPRINT1("SpInfFindFirstLine(\"SourceDisksNames\", \"%S\") failed\n", SourceRootDirId);
    }
    INF_FreeData(SourceRootDirId);
    if (!Success)
    {
        /* FIXME: Handle error! */
        // pSetupData->LastErrorNumber = ERROR_TXTSETUP_SECTION;
        // if (pSetupData->ErrorRoutine)
            // pSetupData->ErrorRoutine(pSetupData, SectionName);
        return STATUS_NOT_FOUND;
    }
    if (!INF_GetDataField(&DirContext, 4, &SourceRootDir))
    {
        /* FIXME: Handle error! */
        DPRINT1("INF_GetData() failed\n");
        return STATUS_NOT_FOUND;
    }

    /* Get optional source relative directory */
    if (!INF_GetDataField(InfContext, 2, &SourceRelativePath))
    {
        SourceRelativePath = NULL;
    }
    else if (!*SourceRelativePath)
    {
        INF_FreeData(SourceRelativePath);
        SourceRelativePath = NULL;
    }
    if (!SourceRelativePath)
    {
        /* Use WinPE directory instead */
        if (INF_GetDataField(InfContext, 13, &TargetDirId))
        {
            /* Lookup directory */
            Success = LookupDirectoryById(InfHandle, &DirContext, TargetDirId, &SourceRelativePath);
            INF_FreeData(TargetDirId);
            if (!Success)
            {
                SourceRelativePath = NULL;
            }
            else if (!*SourceRelativePath)
            {
                INF_FreeData(SourceRelativePath);
                SourceRelativePath = NULL;
            }
        }
    }

    /*
     * Getting Target File Location -- SetupGetTargetPath()
     */

    /* Get target directory id */
    if (!INF_GetDataField(InfContext, 8, &TargetDirId))
    {
        /* FIXME: Handle error! */
        DPRINT1("INF_GetData() failed\n");
        INF_FreeData(SourceRelativePath);
        INF_FreeData(SourceRootDir);
        return STATUS_NOT_FOUND;
    }

    /* Lookup target directory */
    Success = LookupDirectoryById(InfHandle, &DirContext, TargetDirId, &TargetDir);
    INF_FreeData(TargetDirId);
    if (!Success)
    {
        /* FIXME: Handle error! */
        INF_FreeData(SourceRelativePath);
        INF_FreeData(SourceRootDir);
        return STATUS_NOT_FOUND;
    }

    /* Get optional target file name */
    if (!INF_GetDataField(InfContext, 11, &TargetFileName))
        TargetFileName = NULL;
    else if (!*TargetFileName)
        TargetFileName = NULL;

    DPRINT1("GetSourceFileAndTargetLocation(%S) = "
           "SrcRootDir: '%S', SrcRelPath: '%S' --> TargetDir: '%S', TargetFileName: '%S'\n",
           SourceFileName, SourceRootDir, SourceRelativePath, TargetDir, TargetFileName);

#if 0
    INF_FreeData(TargetDir);
    INF_FreeData(TargetFileName);
    INF_FreeData(SourceRelativePath);
    INF_FreeData(SourceRootDir);
#endif

    *pSourceRootPath  = SourceRootDir;
    *pSourcePath      = SourceRelativePath;
    *pTargetDirectory = TargetDir;
    *pTargetFileName  = TargetFileName;

    return STATUS_SUCCESS;
}


static NTSTATUS
BuildFullDirectoryPath(
    IN PCWSTR RootPath,
    IN PCWSTR BasePath,
    IN PCWSTR RelativePath,
    OUT PWSTR FullPath,
    IN SIZE_T cchFullPathSize)
{
    NTSTATUS Status;

    if ((RelativePath[0] == UNICODE_NULL) || (RelativePath[0] == L'\\' && RelativePath[1] == UNICODE_NULL))
    {
        /* Installation path */
        DPRINT1("InstallationPath: '%S'\n", RelativePath);

        Status = CombinePaths(FullPath, cchFullPathSize, 2,
                              RootPath, BasePath);

        DPRINT1("InstallationPath(2): '%S'\n", FullPath);
    }
    else if (RelativePath[0] == L'\\')
    {
        /* Absolute path */
        DPRINT1("AbsolutePath: '%S'\n", RelativePath);

        Status = CombinePaths(FullPath, cchFullPathSize, 2,
                              RootPath, RelativePath);

        DPRINT1("AbsolutePath(2): '%S'\n", FullPath);
    }
    else // if (RelativePath[0] != L'\\')
    {
        /* Path relative to the installation path */
        DPRINT1("RelativePath: '%S'\n", RelativePath);

        Status = CombinePaths(FullPath, cchFullPathSize, 3,
                              RootPath, BasePath, RelativePath);

        DPRINT1("RelativePath(2): '%S'\n", FullPath);
    }

    return Status;
}


/*
 * This code enumerates the list of files in reactos.dff / reactos.inf
 * that need to be extracted from reactos.cab and be installed in their
 * respective directories.
 */
/*
 * IMPORTANT NOTE: The INF file specification used for the .CAB in ReactOS
 * is not compliant with respect to TXTSETUP.SIF syntax or the standard syntax.
 */
static BOOLEAN
AddSectionToCopyQueueCab(
    IN PUSETUP_DATA pSetupData,
    IN HINF InfFile,
    IN PCWSTR SectionName,
    IN PCWSTR SourceCabinet,
    IN PCUNICODE_STRING DestinationPath)
{
    BOOL Success;
    NTSTATUS Status;
    INFCONTEXT FilesContext;
    INFCONTEXT DirContext;
    PCWSTR SourceFileName;
    PCWSTR TargetDirId;
    PCWSTR TargetDir;
    PCWSTR TargetFileName;
    WCHAR FileDstPath[MAX_PATH];

    /* Search for the SectionName section */
    if (!SpInfFindFirstLine(InfFile, SectionName, NULL, &FilesContext))
    {
        pSetupData->LastErrorNumber = ERROR_TXTSETUP_SECTION;
        if (pSetupData->ErrorRoutine)
            pSetupData->ErrorRoutine(pSetupData, SectionName);
        return FALSE; // ERROR_TXTSETUP_SECTION;
    }

    /*
     * Enumerate the files in the section and add them to the file queue.
     */
    do
    {
        /* Get source file name and target directory id */
        if (!INF_GetData(&FilesContext, &SourceFileName, &TargetDirId))
        {
            /* FIXME: Handle error! */
            DPRINT1("INF_GetData() failed\n");
            break;
        }

        /* Get optional target file name */
        if (!INF_GetDataField(&FilesContext, 2, &TargetFileName))
            TargetFileName = NULL;
        else if (!*TargetFileName)
            TargetFileName = NULL;

        /* Lookup target directory */
        Success = LookupDirectoryById(InfFile, &DirContext, TargetDirId, &TargetDir);
        INF_FreeData(TargetDirId);
        if (!Success)
        {
            /* FIXME: Handle error! */
            INF_FreeData(SourceFileName);
            INF_FreeData(TargetFileName);
            break;
        }

        DPRINT1("GetSourceTargetFromCab(%S) = "
               "SrcRootDir: '%S', SrcRelPath: '%S' --> TargetDir: '%S', TargetFileName: '%S'\n",
               SourceFileName,
               pSetupData->SourcePath.Buffer,
               pSetupData->SourceRootDir.Buffer,
               TargetDir, TargetFileName);

        Status = CombinePaths(FileDstPath, ARRAYSIZE(FileDstPath), 2,
                              pSetupData->DestinationPath.Buffer,
                              TargetDir);
        UNREFERENCED_PARAMETER(Status);
        DPRINT1("  --> FileDstPath = '%S'\n", FileDstPath);

        INF_FreeData(TargetDir);

        if (!SpFileQueueCopy((HSPFILEQ)pSetupData->SetupFileQueue,
                             pSetupData->SourcePath.Buffer, // SourcePath == SourceRootPath ++ SourceRootDir
                             NULL,
                             SourceFileName,
                             NULL,
                             SourceCabinet,
                             NULL,
                             FileDstPath,
                             TargetFileName,
                             0 /* FIXME */))
        {
            /* FIXME: Handle error! */
            DPRINT1("SpFileQueueCopy() failed\n");
        }

        INF_FreeData(SourceFileName);
        INF_FreeData(TargetFileName);

    } while (SpInfFindNextLine(&FilesContext, &FilesContext));

    return TRUE; // ERROR_SUCCESS;
}

// Note: Modeled after the SetupQueueCopySection() API
/*
BOOL SetupQueueCopySection(
  _In_ HSPFILEQ QueueHandle,
  _In_ PCTSTR   SourceRootPath,
  _In_ HINF     InfHandle,
  _In_ HINF     ListInfHandle,
  _In_ PCTSTR   Section,
  _In_ DWORD    CopyStyle
);
*/
static BOOLEAN
AddSectionToCopyQueue(
    IN PUSETUP_DATA pSetupData,
    IN HINF InfFile,
    IN PCWSTR SectionName,
    IN PCWSTR SourceCabinet,
    IN PCUNICODE_STRING DestinationPath)
{
    NTSTATUS Status;
    INFCONTEXT FilesContext;
    PCWSTR SourceFileName;
    PCWSTR SourceRootPath;
    PCWSTR SourcePath;
    PCWSTR TargetDirectory;
    PCWSTR TargetFileName;
    WCHAR FileSrcRootPath[MAX_PATH];
    WCHAR FileDstPath[MAX_PATH];

    if (SourceCabinet)
    {
        return AddSectionToCopyQueueCab(pSetupData, InfFile, L"SourceFiles",
                                        SourceCabinet, DestinationPath);
    }

    ASSERT(SourceCabinet == NULL);

    /*
     * This code enumerates the list of files in txtsetup.sif
     * that need to be installed in their respective directories.
     */

    /* Search for the SectionName section */
    if (!SpInfFindFirstLine(InfFile, SectionName, NULL, &FilesContext))
    {
        pSetupData->LastErrorNumber = ERROR_TXTSETUP_SECTION;
        if (pSetupData->ErrorRoutine)
            pSetupData->ErrorRoutine(pSetupData, SectionName);
        return FALSE; // ERROR_TXTSETUP_SECTION;
    }

    /*
     * Enumerate the files in the section and add them to the file queue.
     */
    do
    {
        /* Get source file name */
        if (!INF_GetDataField(&FilesContext, 0, &SourceFileName))
        {
            /* FIXME: Handle error! */
            DPRINT1("INF_GetData() failed\n");
            break;
        }

        Status = GetSourceFileAndTargetLocation(InfFile,
                                                &FilesContext,
                                                SourceFileName,
                                                &SourceRootPath, // SourceRootDir
                                                &SourcePath,
                                                &TargetDirectory,
                                                &TargetFileName);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Could not find source and target location for file '%S'\n", SourceFileName);
            INF_FreeData(SourceFileName);

            pSetupData->LastErrorNumber = ERROR_TXTSETUP_SECTION;
            if (pSetupData->ErrorRoutine)
                pSetupData->ErrorRoutine(pSetupData, SectionName);
            return FALSE; // ERROR_TXTSETUP_SECTION;
            // break;
        }
        /*
         * SourcePath: '\Device\CdRom0\I386'
         * SourceRootPath: '\Device\CdRom0'
         * SourceRootDir: '\I386'
         */

        Status = CombinePaths(FileSrcRootPath, ARRAYSIZE(FileSrcRootPath), 2,
                              pSetupData->SourceRootPath.Buffer,
                              SourceRootPath);
        UNREFERENCED_PARAMETER(Status);
        // DPRINT1("Could not build the full path for '%S', skipping...\n", SourceRootPath);
        DPRINT1("  --> FileSrcRootPath = '%S'\n", FileSrcRootPath);

        INF_FreeData(SourceRootPath);

        Status = CombinePaths(FileDstPath, ARRAYSIZE(FileDstPath), 2,
                              pSetupData->DestinationPath.Buffer,
                              TargetDirectory);
        UNREFERENCED_PARAMETER(Status);
        // DPRINT1("Could not build the full path for '%S', skipping...\n", TargetDirectory);
        DPRINT1("  --> FileDstPath = '%S'\n", FileDstPath);

        INF_FreeData(TargetDirectory);

        if (!SpFileQueueCopy((HSPFILEQ)pSetupData->SetupFileQueue,
                             FileSrcRootPath,
                             SourcePath,
                             SourceFileName,
                             NULL,
                             NULL, // SourceCabinet == NULL
                             NULL,
                             FileDstPath,
                             TargetFileName,
                             0 /* FIXME */))
        {
            /* FIXME: Handle error! */
            DPRINT1("SpFileQueueCopy() failed\n");
        }

        INF_FreeData(SourceFileName);
        INF_FreeData(SourcePath);
        INF_FreeData(TargetFileName);

    } while (SpInfFindNextLine(&FilesContext, &FilesContext));

    return TRUE; // ERROR_SUCCESS;
}

BOOLEAN // ERROR_NUMBER
PrepareCopyInfFile(
    IN OUT PUSETUP_DATA pSetupData,
    IN HINF InfFile,
    IN PCWSTR SourceCabinet)
{
    BOOLEAN Success;
    NTSTATUS Status;
    INFCONTEXT DirContext;
    PWCHAR AdditionalSectionName = NULL;
    PCWSTR DirKeyValue;
    WCHAR PathBuffer[MAX_PATH];

    /* Add common files -- Search for the SourceDisksFiles section */
    /* Search in the platform-specific first (currently hardcoded; make it runtime-dependent?) */
    Success = AddSectionToCopyQueue(pSetupData, InfFile,
                                    L"SourceDisksFiles." INF_ARCH,
                                    SourceCabinet,
                                    &pSetupData->DestinationPath);
    if (!Success)
    {
        DPRINT1("AddSectionToCopyQueue(%S) failed!\n", L"SourceDisksFiles." INF_ARCH);
    }
    /* Search in the global section */
    Success = AddSectionToCopyQueue(pSetupData, InfFile,
                                    L"SourceDisksFiles",
                                    SourceCabinet,
                                    &pSetupData->DestinationPath);
    if (!Success)
    {
        DPRINT1("AddSectionToCopyQueue(%S) failed!\n", L"SourceDisksFiles");
        // pSetupData->LastErrorNumber = ERROR_TXTSETUP_SECTION;
        // if (pSetupData->ErrorRoutine)
            // pSetupData->ErrorRoutine(pSetupData, SectionName);
        return FALSE;
    }

    /* Add specific files depending of computer type */
    if (SourceCabinet == NULL)
    {
        if (!ProcessComputerFiles(InfFile, pSetupData->ComputerList, &AdditionalSectionName))
            // return ERROR_LOAD_COMPUTER;
            return FALSE;

        if (AdditionalSectionName)
        {
            ASSERT(SourceCabinet == NULL);
            if (!AddSectionToCopyQueue(pSetupData, InfFile,
                                       AdditionalSectionName,
                                       NULL,
                                       &pSetupData->DestinationPath))
            {
                return FALSE;
            }
        }
    }

    /* Create directories */

    /*
     * NOTE: This is technically optional since SpFileQueueCommit
     * does that. This is however needed if one wants to create
     * empty directories.
     */

    /*
     * FIXME:
     * Copying files to pSetupData->DestinationRootPath should be done from within
     * the SystemPartitionFiles section.
     * At the moment we check whether we specify paths like '\foo' or '\\' for that.
     * For installing to pSetupData->DestinationPath specify just '\' .
     */

    /* Get destination path */
    RtlStringCchCopyW(PathBuffer, ARRAYSIZE(PathBuffer),
                      pSetupData->DestinationPath.Buffer);

    DPRINT("FullPath(1): '%S'\n", PathBuffer);

    /* Create the install directory */
    Status = SetupCreateDirectory(PathBuffer);
    if (!NT_SUCCESS(Status) && Status != STATUS_OBJECT_NAME_COLLISION)
    {
        DPRINT1("Creating directory '%S' failed: Status = 0x%08lx\n", PathBuffer, Status);
        pSetupData->LastErrorNumber = ERROR_CREATE_INSTALL_DIR;
        if (pSetupData->ErrorRoutine)
            pSetupData->ErrorRoutine(pSetupData, PathBuffer);
        return FALSE;
    }

    /* Search for the 'Directories' section */
    // ReactOS-specific
    if (!SpInfFindFirstLine(InfFile, L"Directories", NULL, &DirContext))
    {
        // Windows-compatible
        if (!SpInfFindFirstLine(InfFile, L"WinntDirectories", NULL, &DirContext))
        {
            if (SourceCabinet)
                pSetupData->LastErrorNumber = ERROR_CABINET_SECTION;
            else
                pSetupData->LastErrorNumber = ERROR_TXTSETUP_SECTION;

            if (pSetupData->ErrorRoutine)
                pSetupData->ErrorRoutine(pSetupData, L"Directories");
            return FALSE;
        }
    }

    /* Enumerate the directory values and create the subdirectories */
    do
    {
        if (!INF_GetData(&DirContext, NULL, &DirKeyValue))
        {
            DPRINT1("break\n");
            break;
        }

        Status = BuildFullDirectoryPath(pSetupData->DestinationRootPath.Buffer,
                                        pSetupData->InstallPath.Buffer,
                                        DirKeyValue,
                                        PathBuffer,
                                        ARRAYSIZE(PathBuffer));
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Could not build the full path for '%S', skipping...\n", DirKeyValue);
            INF_FreeData(DirKeyValue);
            continue;
        }

        if ((DirKeyValue[0] == UNICODE_NULL) || (DirKeyValue[0] == L'\\' && DirKeyValue[1] == UNICODE_NULL))
        {
            /*
             * Installation path -- No need to create it
             * because it has been already created above.
             */
        }
        else
        {
            /* Arbitrary path -- Create it */
            Status = SetupCreateDirectory(PathBuffer);
            if (!NT_SUCCESS(Status) && Status != STATUS_OBJECT_NAME_COLLISION)
            {
                INF_FreeData(DirKeyValue);
                DPRINT("Creating directory '%S' failed: Status = 0x%08lx", PathBuffer, Status);
                pSetupData->LastErrorNumber = ERROR_CREATE_DIR;
                if (pSetupData->ErrorRoutine)
                    pSetupData->ErrorRoutine(pSetupData, PathBuffer);
                return FALSE;
            }
        }

        INF_FreeData(DirKeyValue);
    } while (SpInfFindNextLine(&DirContext, &DirContext));

    return TRUE;
}


// #define USE_CABINET_INF

ERROR_NUMBER
PrepareCopy(
    IN OUT PUSETUP_DATA pSetupData,
    IN PFILE_COPY_STATUS_ROUTINE StatusRoutine OPTIONAL)
{
    // ERROR_NUMBER ErrorNumber;
    HINF InfHandle;
    INFCONTEXT CabinetsContext;
    PCWSTR KeyValue;
    UINT ErrorLine;
#if defined(__REACTOS__) && defined(USE_CABINET_INF)
    ULONG InfFileSize;
    PVOID InfFileData;
    CABINET_CONTEXT CabinetContext;
#endif
    WCHAR PathBuffer[MAX_PATH];

    /* Create the file queue */
    pSetupData->SetupFileQueue = (PVOID)SpFileQueueOpen();
    if (pSetupData->SetupFileQueue == NULL)
    {
        pSetupData->LastErrorNumber = ERROR_COPY_QUEUE;
        if (pSetupData->ErrorRoutine)
            pSetupData->ErrorRoutine(pSetupData);
        return ERROR_COPY_QUEUE;
    }

    /* Prepare the copy of the common files that are not in installation cabinets */
    if (!PrepareCopyInfFile(pSetupData, pSetupData->SetupInf, NULL))
    {
        // /* FIXME: show an error dialog */
        // return QUIT_PAGE;
        return FALSE;
    }

    /* Search for the 'Cabinets' section */
    if (!SpInfFindFirstLine(pSetupData->SetupInf, L"Cabinets", NULL, &CabinetsContext))
    {
        return ERROR_SUCCESS;
    }

    /*
     * Enumerate the installation cabinets listed in the
     * 'Cabinets' section and parse their inf files.
     */
    do
    {
        if (!INF_GetData(&CabinetsContext, NULL, &KeyValue))
            break;

        CombinePaths(PathBuffer, ARRAYSIZE(PathBuffer), 2,
                     pSetupData->SourcePath.Buffer, KeyValue);

#if defined(__REACTOS__) && defined(USE_CABINET_INF)
        CabinetInitialize(&CabinetContext);
        CabinetSetEventHandlers(&CabinetContext, NULL, NULL, NULL);
        CabinetSetCabinetName(&CabinetContext, PathBuffer);

        if (CabinetOpen(&CabinetContext) == CAB_STATUS_SUCCESS)
        {
            DPRINT("Cabinet %S\n", PathBuffer /*CabinetGetCabinetName(&CabinetContext)*/);

            InfFileData = CabinetGetCabinetReservedArea(&CabinetContext, &InfFileSize);
            if (InfFileData == NULL)
            {
                CabinetCleanup(&CabinetContext);

                pSetupData->LastErrorNumber = ERROR_CABINET_SCRIPT;
                if (pSetupData->ErrorRoutine)
                    pSetupData->ErrorRoutine(pSetupData, PathBuffer /*CabinetGetCabinetName(&CabinetContext)*/);
                return ERROR_CABINET_SCRIPT;
            }
        }
        else
        {
            DPRINT("Cannot open cabinet: %S.\n", PathBuffer /*CabinetGetCabinetName(&CabinetContext)*/);
            CabinetCleanup(&CabinetContext);

            pSetupData->LastErrorNumber = ERROR_CABINET_MISSING;
            if (pSetupData->ErrorRoutine)
                pSetupData->ErrorRoutine(pSetupData, PathBuffer /*CabinetGetCabinetName(&CabinetContext)*/);
            return ERROR_CABINET_MISSING;
        }

        InfHandle = INF_OpenBufferedFileA((PSTR)InfFileData,
                                          InfFileSize,
                                          NULL,
                                          INF_STYLE_WIN4,
                                          pSetupData->LanguageId,
                                          &ErrorLine);

        CabinetCleanup(&CabinetContext);
#else
        {
        PWCHAR ptr;
        ptr = wcsrchr(PathBuffer, L'.');
        if (!ptr)
            ptr = PathBuffer + wcslen(PathBuffer);
        wcscpy(ptr, L".inf");

        InfHandle = SpInfOpenInfFile(PathBuffer,
                                     NULL,
                                     INF_STYLE_OLDNT, // INF_STYLE_WIN4,
                                     pSetupData->LanguageId,
                                     &ErrorLine);
        }
#endif

        if (InfHandle == INVALID_HANDLE_VALUE)
        {
            pSetupData->LastErrorNumber = ERROR_INVALID_CABINET_INF;
            if (pSetupData->ErrorRoutine)
                pSetupData->ErrorRoutine(pSetupData, PathBuffer /*CabinetGetCabinetName(&CabinetContext)*/);
            return ERROR_INVALID_CABINET_INF;
        }

        if (!PrepareCopyInfFile(pSetupData, InfHandle, KeyValue))
        {
#if !(defined(__REACTOS__) && defined(USE_CABINET_INF))
            SpInfCloseInfFile(InfHandle);
#endif
            // /* FIXME: show an error dialog */
            // return QUIT_PAGE;
            return FALSE;
        }

#if !(defined(__REACTOS__) && defined(USE_CABINET_INF))
        SpInfCloseInfFile(InfHandle);
#endif
    } while (SpInfFindNextLine(&CabinetsContext, &CabinetsContext));

    return ERROR_SUCCESS;
}

BOOLEAN
DoCopy(
    IN OUT PUSETUP_DATA pSetupData,
    IN PSP_FILE_CALLBACK_W MsgHandler,
    IN PVOID Context OPTIONAL)
{
    BOOLEAN Success;

    Success = SpFileQueueCommit(NULL,
                                (HSPFILEQ)pSetupData->SetupFileQueue,
                                MsgHandler,
                                Context);

    SpFileQueueClose((HSPFILEQ)pSetupData->SetupFileQueue);
    pSetupData->SetupFileQueue = NULL;

    return Success;
}

/* EOF */
