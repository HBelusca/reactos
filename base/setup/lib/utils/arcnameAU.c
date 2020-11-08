/*
 * PROJECT:     ReactOS Setup Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     ARC path names parser - ANSI / UNICODE template source file.
 * COPYRIGHT:   Copyright 2017-2020 Hermes Belusca-Maito
 */

#include <tchar.h>

#undef NAME_AU
#undef NAME_AW
#if defined(UNICODE) || defined(_UNICODE)
#define NAME_AU(name)   name##U
#define NAME_AW(name)   name##W
#else
#define NAME_AU(name)   name##A
#define NAME_AW(name)   name##A
#endif

#undef STRING
#undef PSTRING
#undef PCSTRING
#if defined(UNICODE) || defined(_UNICODE)
#define STRING      UNICODE_STRING
#define PSTRING     PUNICODE_STRING
#define PCSTRING    PCUNICODE_STRING
#else
#define STRING      ANSI_STRING
#define PSTRING     PANSI_STRING
#define PCSTRING    PCANSI_STRING
#endif

#undef CHAR_NULL
#if defined(UNICODE) || defined(_UNICODE)
#define CHAR_NULL   UNICODE_NULL
#else
#define CHAR_NULL   ANSI_NULL
#endif

#undef RtlInitString
#undef RtlInitEmptyString
#undef RtlEqualString
#if defined(UNICODE) || defined(_UNICODE)
#define RtlInitString       RtlInitUnicodeString
#define RtlInitEmptyString  RtlInitEmptyUnicodeString
#define RtlEqualString      RtlEqualUnicodeString
#else
#define RtlInitString       RtlInitAnsiString
#define RtlInitEmptyString  RtlInitEmptyAnsiString
// #define RtlEqualString      RtlEqualString
#endif

#undef RtlStringCbCopyN
#define RtlStringCbCopyN    NAME_AW(RtlStringCbCopyN)
#undef RtlStringCbCatN
#define RtlStringCbCatN     NAME_AW(RtlStringCbCatN)
#undef RtlStringCbCat
#define RtlStringCbCat      NAME_AW(RtlStringCbCat)


/* ARC PATH TYPES ***********************************************************/

/* Supported adapter types - See ADAPTER_TYPE in arcname.c */
const PCTSTR NAME_AU(AdapterTypes_)[] =
{
    TEXT("eisa"),
    TEXT("scsi"),
    TEXT("multi"),
    TEXT("net"),
    TEXT("ramdisk"),
    NULL
};
#undef AdapterTypes
#define AdapterTypes    NAME_AU(AdapterTypes_)

/* Supported controller types - See CONTROLLER_TYPE in arcname.c */
const PCTSTR NAME_AU(ControllerTypes_)[] =
{
    TEXT("disk"),
    TEXT("cdrom"),
    NULL
};
#undef ControllerTypes
#define ControllerTypes NAME_AU(ControllerTypes_)

/* Supported peripheral types - See PERIPHERAL_TYPE in arcname.c */
const PCTSTR NAME_AU(PeripheralTypes_)[] =
{
//  TEXT("vdisk"), // Enable this when we'll support boot from virtual disks!
    TEXT("rdisk"),
    TEXT("fdisk"),
    TEXT("cdrom"),
    NULL
};
#undef PeripheralTypes
#define PeripheralTypes NAME_AU(PeripheralTypes_)


/* FUNCTIONS ****************************************************************/

// ArcGetNextTokenA/U
static PCTSTR
NAME_AU(ArcGetNextToken)(
    IN  PCTSTR ArcPath,
    OUT PSTRING TokenSpecifier,
    OUT PULONG Key)
{
    NTSTATUS Status;
    PCTSTR p = ArcPath;
    SIZE_T SpecifierLength;
    ULONG KeyValue;

    /*
     * We must have a valid "specifier(key)" string, where 'specifier'
     * cannot be the empty string, and is followed by '('.
     */
    p = _tcschr(p, _T('('));
    if (p == NULL)
        return NULL; /* No '(' found */
    if (p == ArcPath)
        return NULL; /* Path starts with '(' and is thus invalid */

    SpecifierLength = (p - ArcPath) * sizeof(TCHAR);
    if (SpecifierLength > MAXUSHORT) // UNICODE_STRING_MAX_BYTES
    {
        return NULL;
    }

    /* Skip closing ')' */
    ++p;

    /*
     * The strtoul function skips any leading whitespace.
     *
     * Note that if the token is "specifier()" then strtoul won't perform
     * any conversion and return 0, therefore effectively making the token
     * equivalent to "specifier(0)", as it should be.
     */
    // KeyValue = _ttoi(p);
    KeyValue = _tcstoul(p, (PTSTR*)&p, 10);

    /* Skip any trailing whitespace */
    while (_istspace(*p)) ++p;

    /* The token must terminate with ')' */
    if (*p != _T(')'))
        return NULL;
#if 0
    p = _tcschr(p, _T(')'));
    if (p == NULL)
        return NULL;
#endif

    /* Initialize the token specifier to the buffer */
    RtlInitEmptyString(TokenSpecifier, ArcPath, (USHORT)SpecifierLength)
    TokenSpecifier->Length = (USHORT)SpecifierLength;

    /* We succeeded, return the token key value */
    *Key = KeyValue;

    /* The next token starts just after */
    return ++p;
}

#define ArcGetNextToken NAME_AU(ArcGetNextToken)

// ArcMatchTokenA/U
static ULONG
NAME_AU(ArcMatchToken)(
    IN PCTSTR CandidateToken,
    IN const PCTSTR* TokenTable)
{
    ULONG Index = 0;

    while (TokenTable[Index] && _tcsicmp(CandidateToken, TokenTable[Index]) != 0)
    {
        ++Index;
    }

    return Index;
}

#define ArcMatchToken   NAME_AU(ArcMatchToken)

// ArcMatchToken_AStr / ArcMatchToken_UStr
static ULONG
NAME_AU(ArcMatchToken_)Str(
    IN PCSTRING CandidateToken,
    IN const PCTSTR* TokenTable)
{
    ULONG Index = 0;
#if 0
    SIZE_T Length;
#else
    STRING Token;
#endif

    while (TokenTable[Index])
    {
#if 0
        Length = _tcslen(TokenTable[Index]);
        if ((Length == CandidateToken->Length / sizeof(TCHAR)) &&
            (_tcsnicmp(CandidateToken->Buffer, TokenTable[Index], Length) == 0))
        {
            break;
        }
#else
        RtlInitString(&Token, TokenTable[Index]);
        if (RtlEqualString(CandidateToken, &Token, TRUE))
            break;
#endif

        ++Index;
    }

    return Index;
}

#define ArcMatchToken_Str   NAME_AU(ArcMatchToken_)Str

/*
 * ArcNamePath:
 *      In input, pointer to an ARC path (NULL-terminated) starting by an
 *      ARC name to be parsed into its different components.
 *      In output, ArcNamePath points to the beginning of the path after
 *      the ARC name part.
 */
NTSTATUS
NAME_AU(ParseArcName)(
    IN OUT PCTSTR* ArcNamePath,
    OUT PULONG pAdapterKey,
    OUT PULONG pControllerKey,
    OUT PULONG pPeripheralKey,
    OUT PULONG pPartitionNumber,
    OUT PADAPTER_TYPE pAdapterType,
    OUT PCONTROLLER_TYPE pControllerType,
    OUT PPERIPHERAL_TYPE pPeripheralType,
    OUT PBOOLEAN pUseSignature)
{
    // NTSTATUS Status;
    STRING Token;
    PCTSTR p, q;
    ULONG AdapterKey = 0;
    ULONG ControllerKey = 0;
    ULONG PeripheralKey = 0;
    ULONG PartitionNumber = 0;
    ADAPTER_TYPE AdapterType = AdapterTypeMax;
    CONTROLLER_TYPE ControllerType = ControllerTypeMax;
    PERIPHERAL_TYPE PeripheralType = PeripheralTypeMax;
    BOOLEAN UseSignature = FALSE;

    /*
     * The format of ArcName is:
     *    adapter(www)[controller(xxx)peripheral(yyy)[partition(zzz)][filepath]] ,
     * where the [filepath] part is not being parsed.
     */

    p = *ArcNamePath;

    /* Retrieve the adapter */
    p = ArcGetNextToken(p, &Token, &AdapterKey);
    if (!p)
    {
        DPRINT1("No adapter specified!\n");
        return STATUS_OBJECT_PATH_SYNTAX_BAD;
    }

    /* Check for the 'signature()' pseudo-adapter, introduced in Windows 2000 */
    if ((Token.Length == sizeof(TEXT("signature"))-sizeof(CHAR_NULL)) &&
        _tcsnicmp(Token.Buffer, TEXT("signature"), Token.Length / sizeof(TCHAR)) == 0)
    {
        /*
         * We've got a signature! Remember this for later, and set the adapter type to SCSI.
         * We however check that the rest of the ARC path is valid by parsing the other tokens.
         * AdapterKey stores the disk signature value (that holds in a ULONG).
         */
        UseSignature = TRUE;
        AdapterType = ScsiAdapter;
    }
    else
    {
        /* Check for regular adapters */
        AdapterType = (ADAPTER_TYPE)ArcMatchToken_Str(&Token, AdapterTypes);
        if (AdapterType >= AdapterTypeMax)
        {
#if defined(UNICODE) || defined(_UNICODE)
            DPRINT1("Invalid adapter type %wZ\n", &Token);
#else
            DPRINT1("Invalid adapter type %Z\n", &Token);
#endif
            return STATUS_OBJECT_NAME_INVALID;
        }

        /* Check for adapters that don't take any extra controller or peripheral nodes */
        if (AdapterType == NetAdapter || AdapterType == RamdiskAdapter)
        {
            // if (*p)
            //     return STATUS_OBJECT_PATH_SYNTAX_BAD;

            if (AdapterType == NetAdapter)
            {
#if defined(UNICODE) || defined(_UNICODE)
                DPRINT1("%S(%lu) path is not supported!\n",
                        AdapterTypes[AdapterType], AdapterKey);
#else
                DPRINT1("%s(%lu) path is not supported!\n",
                        AdapterTypes[AdapterType], AdapterKey);
#endif
                return STATUS_NOT_SUPPORTED;
            }

            goto Quit;
        }
    }

    /* Here, we have either an 'eisa', a 'scsi/signature', or a 'multi' adapter */

    /* Check for a valid controller */
    p = ArcGetNextToken(p, &Token, &ControllerKey);
    if (!p)
    {
#if defined(UNICODE) || defined(_UNICODE)
        DPRINT1("%S(%lu) adapter doesn't have a controller!\n",
                AdapterTypes[AdapterType], AdapterKey);
#else
        DPRINT1("%s(%lu) adapter doesn't have a controller!\n",
                AdapterTypes[AdapterType], AdapterKey);
#endif
        return STATUS_OBJECT_PATH_SYNTAX_BAD;
    }
    ControllerType = (CONTROLLER_TYPE)ArcMatchToken_Str(&Token, ControllerTypes);
    if (ControllerType >= ControllerTypeMax)
    {
#if defined(UNICODE) || defined(_UNICODE)
        DPRINT1("Invalid controller type %wZ\n", &Token);
#else
        DPRINT1("Invalid controller type %Z\n", &Token);
#endif
        return STATUS_OBJECT_NAME_INVALID;
    }

    /* Here the controller can only be either a disk or a CDROM */

    /*
     * Ignore the controller in case we have a 'multi' adapter.
     * I guess a similar condition holds for the 'eisa' adapter too...
     *
     * For SignatureAdapter, as similar for ScsiAdapter, the controller key corresponds
     * to the disk target ID. Note that actually, the implementation just ignores the
     * target ID, as well as the LUN, and just loops over all the available disks and
     * searches for the one having the correct signature.
     */
    if ((AdapterType == MultiAdapter /* || AdapterType == EisaAdapter */) && ControllerKey != 0)
    {
#if defined(UNICODE) || defined(_UNICODE)
        DPRINT1("%S(%lu) adapter with %S(%lu non-zero), ignored!\n",
               AdapterTypes[AdapterType], AdapterKey,
               ControllerTypes[ControllerType], ControllerKey);
#else
        DPRINT1("%s(%lu) adapter with %s(%lu non-zero), ignored!\n",
               AdapterTypes[AdapterType], AdapterKey,
               ControllerTypes[ControllerType], ControllerKey);
#endif
        ControllerKey = 0;
    }

    /*
     * Only the 'scsi' adapter supports a direct 'cdrom' controller.
     * For the others, we need a 'disk' controller to which a 'cdrom' peripheral can talk to.
     */
    if ((AdapterType != ScsiAdapter) && (ControllerType == CdRomController))
    {
#if defined(UNICODE) || defined(_UNICODE)
        DPRINT1("%S(%lu) adapter cannot have a CDROM controller!\n",
                AdapterTypes[AdapterType], AdapterKey);
#else
        DPRINT1("%s(%lu) adapter cannot have a CDROM controller!\n",
                AdapterTypes[AdapterType], AdapterKey);
#endif
        return STATUS_OBJECT_PATH_INVALID;
    }

    /* Check for a valid peripheral */
    p = ArcGetNextToken(p, &Token, &PeripheralKey);
    if (!p)
    {
#if defined(UNICODE) || defined(_UNICODE)
        DPRINT1("%S(%lu)%S(%lu) adapter-controller doesn't have a peripheral!\n",
               AdapterTypes[AdapterType], AdapterKey,
               ControllerTypes[ControllerType], ControllerKey);
#else
        DPRINT1("%s(%lu)%s(%lu) adapter-controller doesn't have a peripheral!\n",
               AdapterTypes[AdapterType], AdapterKey,
               ControllerTypes[ControllerType], ControllerKey);
#endif
        return STATUS_OBJECT_PATH_SYNTAX_BAD;
    }
    PeripheralType = (PERIPHERAL_TYPE)ArcMatchToken_Str(&Token, PeripheralTypes);
    if (PeripheralType >= PeripheralTypeMax)
    {
#if defined(UNICODE) || defined(_UNICODE)
        DPRINT1("Invalid peripheral type %wZ\n", &Token);
#else
        DPRINT1("Invalid peripheral type %Z\n", &Token);
#endif
        return STATUS_OBJECT_NAME_INVALID;
    }

    /*
     * If we had a 'cdrom' controller already, the corresponding peripheral can only be 'fdisk'
     * (see for example the ARC syntax for SCSI CD-ROMs: scsi(x)cdrom(y)fdisk(z) where z == 0).
     */
    if ((ControllerType == CdRomController) && (PeripheralType != FDiskPeripheral))
    {
#if defined(UNICODE) || defined(_UNICODE)
        DPRINT1("%S(%lu) controller cannot have a %S(%lu) peripheral! (note that we haven't check whether the adapter was SCSI or not)\n",
               ControllerTypes[ControllerType], ControllerKey,
               PeripheralTypes[PeripheralType], PeripheralKey);
#else
        DPRINT1("%s(%lu) controller cannot have a %s(%lu) peripheral! (note that we haven't check whether the adapter was SCSI or not)\n",
               ControllerTypes[ControllerType], ControllerKey,
               PeripheralTypes[PeripheralType], PeripheralKey);
#endif
        return STATUS_OBJECT_PATH_INVALID;
    }

    /* For a 'scsi' adapter, the possible peripherals are only 'rdisk' or 'fdisk' */
    if (AdapterType == ScsiAdapter && !(PeripheralType == RDiskPeripheral || PeripheralType == FDiskPeripheral))
    {
#if defined(UNICODE) || defined(_UNICODE)
        DPRINT1("%S(%lu)%S(%lu) SCSI adapter-controller has an invalid peripheral %S(%lu) !\n",
               AdapterTypes[AdapterType], AdapterKey,
               ControllerTypes[ControllerType], ControllerKey,
               PeripheralTypes[PeripheralType], PeripheralKey);
#else
        DPRINT1("%s(%lu)%s(%lu) SCSI adapter-controller has an invalid peripheral %s(%lu) !\n",
               AdapterTypes[AdapterType], AdapterKey,
               ControllerTypes[ControllerType], ControllerKey,
               PeripheralTypes[PeripheralType], PeripheralKey);
#endif
        return STATUS_OBJECT_PATH_INVALID;
    }

#if 0
    if (AdapterType == SignatureAdapter && PeripheralKey != 0)
    {
#if defined(UNICODE) || defined(_UNICODE)
        DPRINT1("%S(%lu) adapter with %S(%lu non-zero), ignored!\n",
               AdapterTypes[AdapterType], AdapterKey,
               PeripheralTypes[PeripheralType], PeripheralKey);
#else
        DPRINT1("%s(%lu) adapter with %s(%lu non-zero), ignored!\n",
               AdapterTypes[AdapterType], AdapterKey,
               PeripheralTypes[PeripheralType], PeripheralKey);
#endif
        PeripheralKey = 0;
    }
#endif

    /* Check for the optional 'partition' specifier */
    q = ArcGetNextToken(p, &Token, &PartitionNumber);
    if (q && (Token.Length == sizeof(TEXT("partition"))-sizeof(CHAR_NULL)) &&
        _tcsnicmp(Token.Buffer, TEXT("partition"), Token.Length / sizeof(TCHAR)) == 0)
    {
        /* We've got a partition! */
        p = q;
    }
    else
    {
        /*
         * Either no other ARC token was found, or we've got something else
         * (possibly invalid or not)...
         */
        PartitionNumber = 0;
    }

    // TODO: Check the partition number in case of fdisks and cdroms??

Quit:
    /* Return the results */
    *ArcNamePath      = p;
    *pAdapterKey      = AdapterKey;
    *pControllerKey   = ControllerKey;
    *pPeripheralKey   = PeripheralKey;
    *pPartitionNumber = PartitionNumber;
    *pAdapterType     = AdapterType;
    *pControllerType  = ControllerType;
    *pPeripheralType  = PeripheralType;
    *pUseSignature    = UseSignature;

    return STATUS_SUCCESS;
}

BOOLEAN
NAME_AU(ArcPathNormalize)(
    OUT PSTRING NormalizedArcPath,
    IN  PCTSTR ArcPath)
{
#define _OBJ_NAME_PATH_SEPARATOR    ((TCHAR)_T('\\'))

    NTSTATUS Status;
    PCTSTR EndOfArcName;
    PCTSTR p;
    SIZE_T PathLength;

    if (NormalizedArcPath->MaximumLength < sizeof(CHAR_NULL))
        return FALSE;

    *NormalizedArcPath->Buffer = CHAR_NULL;
    NormalizedArcPath->Length = 0;

    EndOfArcName = _tcschr(ArcPath, _OBJ_NAME_PATH_SEPARATOR);
    if (!EndOfArcName)
        EndOfArcName = ArcPath + _tcslen(ArcPath);

    while ((p = _tcsstr(ArcPath, TEXT("()"))) && (p < EndOfArcName))
    {
#if 0
        Status = RtlStringCbCopyN(NormalizedArcPath->Buffer,
                                  NormalizedArcPath->MaximumLength,
                                  ArcPath, (p - ArcPath) * sizeof(TCHAR));
#else
        Status = RtlStringCbCatN(NormalizedArcPath->Buffer,
                                 NormalizedArcPath->MaximumLength,
                                 ArcPath, (p - ArcPath) * sizeof(TCHAR));
#endif
        if (!NT_SUCCESS(Status))
            return FALSE;

        Status = RtlStringCbCat(NormalizedArcPath->Buffer,
                                NormalizedArcPath->MaximumLength,
                                TEXT("(0)"));
        if (!NT_SUCCESS(Status))
            return FALSE;
#if 0
        NormalizedArcPath->Buffer += _tcslen(NormalizedArcPath->Buffer);
#endif
        ArcPath = p + 2;
    }

    Status = RtlStringCbCat(NormalizedArcPath->Buffer,
                            NormalizedArcPath->MaximumLength,
                            ArcPath);
    if (!NT_SUCCESS(Status))
        return FALSE;

    PathLength = _tcslen(NormalizedArcPath->Buffer);
    if (PathLength > UNICODE_STRING_MAX_CHARS)
    {
        return FALSE;
    }

    NormalizedArcPath->Length = (USHORT)PathLength * sizeof(TCHAR);
    return TRUE;

#undef _OBJ_NAME_PATH_SEPARATOR
}


#undef NAME_AU
#undef NAME_AW

#undef STRING
#undef PSTRING
#undef PCSTRING

#undef CHAR_NULL

#undef RtlInitString
#undef RtlInitEmptyString
#undef RtlEqualString

#undef RtlStringCbCopyN
#undef RtlStringCbCatN
#undef RtlStringCbCat

#undef AdapterTypes
#undef ControllerTypes
#undef PeripheralTypes

#undef ArcGetNextToken
#undef ArcMatchToken
#undef ArcMatchToken_Str

/* EOF */
