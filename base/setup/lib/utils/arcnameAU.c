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
#undef RtlStringToInteger
#if defined(UNICODE) || defined(_UNICODE)
#define RtlInitString       RtlInitUnicodeString
#define RtlInitEmptyString  RtlInitEmptyUnicodeString
#define RtlEqualString      RtlEqualUnicodeString
#define RtlStringToInteger  RtlAnsiStringToInteger /* Private, see below */
#else
#define RtlInitString       RtlInitAnsiString
#define RtlInitEmptyString  RtlInitEmptyAnsiString
// #define RtlEqualString      RtlEqualString
#define RtlStringToInteger  RtlUnicodeStringToInteger
#endif

#undef RtlGUIDFromStringEx
#define RtlGUIDFromStringEx NAME_AU(RtlGUIDFromStringEx) /* Private, see below */

#undef RtlStringCbCopyN
#define RtlStringCbCopyN    NAME_AW(RtlStringCbCopyN)
#undef RtlStringCbCatN
#define RtlStringCbCatN     NAME_AW(RtlStringCbCatN)
#undef RtlStringCbCat
#define RtlStringCbCat      NAME_AW(RtlStringCbCat)


/* EXTRA STRING FUNCTIONS ***************************************************/

NTSTATUS
NTAPI
RtlAnsiStringToInteger(
    IN PCANSI_STRING str,
    IN ULONG base, /* Number base for conversion (allowed 0, 2, 8, 10 or 16) */
    OUT PULONG value)
{
    /*
     * A ULONG value can be written with a maximum of:
     * ° 1 + 2 + 32 characters in base 2 (optional [+/-], prefix '0b' followed by 32 bits [0-1])
     * ° 1 + 1-2 + 11 characters in base 8 (optional [+/-], prefix '0' or '0o' followed by 11 digits [0-7])
     * ° 1 + 10 characters in base 10 (optional [+/-] followed by 10 digits [0-9])
     * ° 1 + 2 + 8 characters in base 16 (optional [+/-], prefix '0x' followed by 8 digits [0-9a-f])
     * And the NULL-termination.
     */
    CHAR buffer[35+1];
    PSTR ptr = str->Buffer;
    USHORT len = str->Length;

    /* Skip any leading whitespace */
    while ((len >= sizeof(CHAR)) && isspace(*ptr))
    {
        ++ptr;
        len -= sizeof(CHAR);
    }

    RtlStringCbCopyNA(buffer, sizeof(buffer), ptr, min(len, sizeof(buffer)));
    return RtlCharToInteger(buffer, base, value);
}

#define GUID_STRING_LENGTH 36

/*
 * Wraps RtlGUIDFromString() functionality around
 * so as to accept GUID strings with optional braces.
 */
NTSTATUS
NTAPI
RtlGUIDFromStringExU(
    IN PCUNICODE_STRING str,
    OUT PGUID guid)
{
    /*
     * The RtlGUIDFromString() function only accepts GUID strings that
     * start and end with braces, while we want to make these optional.
     * Since a GUID string has a known fixed length, use such a buffer
     * to always store the braces (if not already present in the string)
     * for making the RtlGUIDFromString() function happy.
     *
     * A GUID string has the format: {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}
     *                                DWORD... WORD WORD BYTES............
     */
    WCHAR strGuid[GUID_STRING_LENGTH + 2 + 1];
    UNICODE_STRING strUGuid = RTL_CONSTANT_STRING(strGuid);

    /* Check whether the string has braces, or does not */
    if (str->Length == GUID_STRING_LENGTH * sizeof(WCHAR))
    {
        /* Does not have braces: copy the string in the temporary
         * buffer, add the braces and use that buffer instead. */
        RtlCopyMemory(&strGuid[1], str->Buffer, str->Length);
        strGuid[0] = L'{';
        strGuid[GUID_STRING_LENGTH + 1] = L'}';
        str = &strUGuid;
    }
    else if ((str->Length == (GUID_STRING_LENGTH + 2) * sizeof(WCHAR)) &&
             (str->Buffer[0] == L'{') &&
             (str->Buffer[GUID_STRING_LENGTH + 1] == L'}'))
    {
        /* Does have braces: just use the string */
    }
    else
    {
        /* Definitively wrong format */
        return STATUS_INVALID_PARAMETER;
    }

    /* Call the RTL UNICODE function */
    return RtlGUIDFromString(str, guid);
}

/*
 * Does the same job as RtlGUIDFromStringExU(), but takes an ANSI string.
 */
NTSTATUS
NTAPI
RtlGUIDFromStringExA(
    IN PCANSI_STRING str,
    OUT PGUID guid)
{
    /*
     * The RtlGUIDFromString() function only accepts GUID strings that
     * start and end with braces, while we want to make these optional.
     * Since a GUID string has a known fixed length, use such a buffer
     * to always store the braces (if not already present in the string)
     * for making the RtlGUIDFromString() function happy.
     *
     * A GUID string has the format: {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}
     *                                DWORD... WORD WORD BYTES............
     */
    WCHAR strGuid[GUID_STRING_LENGTH + 2 + 1];
    UNICODE_STRING strUGuid = RTL_CONSTANT_STRING(strGuid);
    PWCHAR Src, Dst;
    USHORT Length;

    /*
     * Check whether the string has braces, or does not.
     * In all cases we will manually "convert" the string to "UCS-2"
     * by extending each character to two bytes: this is OK since
     * the string representation of a GUID uses only [0-9a-fA-F] and {}-
     * characters, which are ANSI.
     */
    if (str->Length == GUID_STRING_LENGTH * sizeof(CHAR))
    {
        /* Does not have braces: add the braces */
        strGuid[0] = L'{';
        strGuid[GUID_STRING_LENGTH + 1] = L'}';
        Dst = strGuid+1;
    }
    else if ((str->Length == (GUID_STRING_LENGTH + 2) * sizeof(CHAR)) &&
             (str->Buffer[0] == '{') &&
             (str->Buffer[GUID_STRING_LENGTH + 1] == '}'))
    {
        /* Does have braces: copy from the beginning */
        Dst = strGuid;
    }
    else
    {
        /* Definitively wrong format */
        return STATUS_INVALID_PARAMETER;
    }

    /* Manually convert the string */
    Src = str->Buffer;
    Length = str->Length / sizeof(CHAR);
    while (Length--)
        *Dst++ = (WCHAR)*Src++;

    /* Call the RTL UNICODE function */
    return RtlGUIDFromString(&strUGuid, guid);
}


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

// ArcGetNextTokenExA/U
static PCTSTR
NAME_AU(ArcGetNextTokenEx)(
    IN  PCTSTR ArcPath,
    OUT PSTRING TokenSpecifier,
    OUT PSTRING Key)
{
    PCTSTR p = ArcPath;
    PCTSTR KeyValue, KeyValueEnd;
    SIZE_T SpecifierLength;
    SIZE_T KeyLength;

    /*
     * We must have a valid "specifier(key)" string, where 'specifier'
     * cannot be the empty string, and is followed by '('.
     */
    p = _tcschr(p, _T('('));
    if (!p)
        return NULL; /* No '(' found */
    if (p == ArcPath)
        return NULL; /* Path starts with '(' and is thus invalid */

    SpecifierLength = (p - ArcPath) * sizeof(TCHAR);
    if (SpecifierLength > MAXUSHORT) // UNICODE_STRING_MAX_BYTES
    {
        return NULL;
    }

    /* Skip the opening '(' */
    ++p;

    /* Skip any leading whitespace */
    while (_istspace(*p))
        ++p;

    /* Get the start of the key value */
    KeyValue = p;

    /* The token must terminate with ')' */
    p = _tcschr(p, _T(')'));
    if (!p)
        return NULL; /* No ')' found */

    /* Go past the end of the key value */
    KeyValueEnd = p;

    /* Skip any trailing whitespace */
    --KeyValueEnd;
    while ((KeyValueEnd > KeyValue) && _istspace(*KeyValueEnd))
        --KeyValueEnd;
    ++KeyValueEnd;

    KeyLength = (KeyValueEnd - KeyValue) * sizeof(TCHAR);
    if (KeyLength > MAXUSHORT) // UNICODE_STRING_MAX_BYTES
    {
        return NULL;
    }

    /* Initialize the token specifier to the buffer */
    RtlInitEmptyString(TokenSpecifier, ArcPath, (USHORT)SpecifierLength)
    TokenSpecifier->Length = (USHORT)SpecifierLength;

    /* Initialize the key value to the buffer */
    RtlInitEmptyString(Key, KeyValue, (USHORT)KeyLength)
    Key->Length = (USHORT)KeyLength;

    /* The next token starts just after */
    return ++p;
}

#define ArcGetNextTokenEx   NAME_AU(ArcGetNextTokenEx)

// ArcGetNextTokenA/U
static PCTSTR
NAME_AU(ArcGetNextToken)(
    IN  PCTSTR ArcPath,
    OUT PSTRING TokenSpecifier,
    OUT PULONG Key)
{
    NTSTATUS Status;
    STRING Token, KeyStr;
    ULONG KeyValue;

    ArcPath = ArcGetNextTokenEx(ArcPath, &Token, &KeyStr);
    if (!ArcPath)
        return NULL;

    /*
     * If the token is "specifier()", i.e. the key is empty, default to
     * ULONG value 0 so as to make the token equivalent to "specifier(0)",
     * as it should be.
     */
    if (KeyStr.Length / sizeof(TCHAR) == 0)
    {
        KeyValue = 0;
        Status = STATUS_SUCCESS;
    }
    else
    {
        Status = RtlStringToInteger(&KeyStr, 10, &KeyValue);
    }
    if (!NT_SUCCESS(Status))
        return NULL;

    /* Return the token specifier and the key value */
    *TokenSpecifier = Token;
    *Key = KeyValue;

    /* Return the next token */
    return ArcPath;
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
    OUT PDEVICE_SIGNATURE pSignature)
{
    NTSTATUS Status;
    STRING Token, KeyStr;
    PCTSTR p, q;
    ULONG AdapterKey = 0;
    ULONG ControllerKey = 0;
    ULONG PeripheralKey = 0;
    ULONG PartitionNumber = 0;
    ADAPTER_TYPE AdapterType = AdapterTypeMax;
    CONTROLLER_TYPE ControllerType = ControllerTypeMax;
    PERIPHERAL_TYPE PeripheralType = PeripheralTypeMax;
    DEVICE_SIGNATURE Signature = {SignatureNone, {0}};

    /*
     * The format of ArcName is:
     *    adapter(www)[controller(xxx)peripheral(yyy)[partition(zzz)][filepath]] ,
     * where the [filepath] part is not being parsed.
     */

    p = *ArcNamePath;

    /* Retrieve the adapter */
    p = ArcGetNextTokenEx(p, &Token, &KeyStr);
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
         * We have a signature. Set the adapter type to SCSI and parse
         * the key value, determining whether it is a ULONG or as a GUID.
         * Default to ULONG value 0 if the key value is empty.
         */
        AdapterKey = 0;
        AdapterType = ScsiAdapter;

        /*
         * If the key value length is smaller than 2+8 characters,
         * (optional prefix '0x' followed by 8 digits [0-9a-f])
         * consider it possibly represents a ULONG in hexadecimal;
         * otherwise consider it to be a GUID.
         */
        if (KeyStr.Length / sizeof(TCHAR) <= 2+8)
        {
            Signature.Type = SignatureLong;
            if (KeyStr.Length / sizeof(TCHAR) == 0)
            {
                Signature.Long = 0;
                Status = STATUS_SUCCESS;
            }
            else
            {
                Status = RtlStringToInteger(&KeyStr, 16, &Signature.Long);
            }
        }
        else
        {
            Signature.Type = SignatureGuid;
            Status = RtlGUIDFromStringEx(&KeyStr, &Signature.Guid);
        }
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Invalid signature key value!\n");
            return STATUS_OBJECT_PATH_SYNTAX_BAD;
        }
    }
    else
    {
        /*
         * If the token is "specifier()", i.e. the key is empty, default to
         * ULONG value 0 so as to make the token equivalent to "specifier(0)",
         * as it should be.
         */
        if (KeyStr.Length / sizeof(TCHAR) == 0)
        {
            AdapterKey = 0;
            Status = STATUS_SUCCESS;
        }
        else
        {
            Status = RtlStringToInteger(&KeyStr, 10, &AdapterKey);
        }
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Invalid adapter key value!\n");
            return STATUS_OBJECT_PATH_SYNTAX_BAD;
        }

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
    *pSignature       = Signature;

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
#undef RtlStringToInteger

#undef RtlGUIDFromStringEx

#undef RtlStringCbCopyN
#undef RtlStringCbCatN
#undef RtlStringCbCat

#undef AdapterTypes
#undef ControllerTypes
#undef PeripheralTypes

#undef ArcGetNextTokenEx
#undef ArcGetNextToken
#undef ArcMatchToken
#undef ArcMatchToken_Str

/* EOF */
