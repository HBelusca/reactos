/*
 * PROJECT:     ReactOS Console Utilities Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Command-Line Options Parser.
 * COPYRIGHT:   Copyright 2016-2021 Hermes Belusca-Maito
 */

/**
 * @file    parser.h
 * @ingroup ConUtils
 *
 * @brief   A library for parsing command-line options, in a consistent manner
 *          thorough the ReactOS console utilities.
 **/


/**
 * List of #defines to be defined before including this file,
 * so as to include or exclude advanced functionality.
 *
 * ALT_NAMES         //< Supports option name "synonyms" (alts), e.g. "-?" or "-h" for help.
 * LONG_OPTION_NAMES //< Supports "--long_name" in addition to "-n" or "/n".
 * INT_FLAGS         //< Supports different integer-sized flags.
 * INT_TYPES         //< Supports different integer-sized types.
 */


#ifndef __PARSER_H__
#define __PARSER_H__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifndef UINT8_MAX
#define UINT8_MAX ((UINT8)0xffU)
#endif

#ifndef UINT16_MAX
#define UINT16_MAX ((UINT16)0xffffU)
#endif

#ifndef UINT32_MAX
#define UINT32_MAX ((UINT32)0xffffffffUL)
#endif

#ifndef UINT64_MAX
#define UINT64_MAX ((UINT64)0xffffffffffffffffULL)
#endif


/**
 * @brief   Enumerates the possible types of data described by an option.
 **/
typedef enum _OPTION_TYPE
{
    TYPE_None = 0,
#ifdef INT_FLAGS
    TYPE_Flag8,
    TYPE_Flag16,
    TYPE_Flag32,
    TYPE_Flag = TYPE_Flag32, // "Default" flag type.
    TYPE_Flag64,
#else
    TYPE_Flag,
#endif
    TYPE_Str,
#ifdef INT_TYPES
    TYPE_U8,
    TYPE_U16,
    TYPE_U32,
    TYPE_ULong = TYPE_U32,   // "Default" integer type.
    TYPE_U64,
#else
    TYPE_ULong,
#endif
/* Newer option types must be appended below */
} OPTION_TYPE, *POPTION_TYPE;

/**
 * @brief   Option parsing flags.
 **/
#define OPTION_ALLOWED_LIST 0x01
#define OPTION_NOT_EMPTY    0x02
#define OPTION_TRIM_SPACE   0x04
#define OPTION_EXCLUSIVE    0x08
#define OPTION_MANDATORY    0x10

/**
 * @brief   Describes a specific option.
 **/
typedef struct _OPTION
{
    /* Constant data */
    PCWSTR OptionName;      /**< Option switch name **/
#ifdef LONG_OPTION_NAMES
    PCWSTR OptionLongName;  /**< Long-format option switch name **/
#endif

    OPTION_TYPE Type;       /**< Type of data stored in the 'Value' member
                             **  (UNUSED) (bool, string, int, ..., or custom
                             **  parsing by using a callback function) **/

    ULONG Flags;            /**< OPTION-xxx flags **/
    ULONG MaxOfInstances;   /**< Maximum number of times this option can be
                             **  seen in the command line (or 0: do not care). **/

    // PWSTR OptionHelp;       // Help string, or resource ID of the (localized) string (use the MAKEINTRESOURCE macro to create this value).
    // PVOID Callback() ??
    union
    {
        ULONG_PTR Data;         /**< Dummy member whose size encompasses all
                                 **  the other members of this union. **/

#ifdef INT_FLAGS
        UINT8 ValueFlag8;       /**< For an option of type @b TYPE_Flag8, this is
                                 **  the flag value to set when the option is encountered. **/

        UINT16 ValueFlag16;     /**< For an option of type @b TYPE_Flag16, this is
                                 **  the flag value to set when the option is encountered. **/
#endif

        UINT32 ValueFlag32;     /**< For an option of type @b TYPE_Flag32, this is
                                 **  the flag value to set when the option is encountered. **/

#ifdef INT_FLAGS
        UINT64 ValueFlag64;     /**< For an option of type @b TYPE_Flag64, this is
                                 **  the flag value to set when the option is encountered. **/
#endif

        PCWSTR AllowedValues;   /**< For an option of type @b TYPE_Str, using
                                 **  @b OPTION_ALLOWED_LIST, this is an optional
                                 **  list of allowed values, given as a string
                                 ** of values separated by a pipe symbol '|'. **/
    };

    /* Parsing data */
    PCWSTR OptionStr;       // Pointer to the original option string.
    ULONG Instances;        // Number of times this option is seen in the command line.
    ULONG ValueSize;        // Size of the buffer pointed by 'Value' ?? (UNUSED yet)
    PVOID Value;            // A pointer to part of the command line, or an allocated buffer.
} OPTION, *POPTION;

/**
 * @brief   Initializes a new option entry.
 **/
#ifndef LONG_OPTION_NAMES
    #define NEW_OPT(Name, Type, Flags, MaxOfInstances, ValueSize, ValueBuffer) \
        {(Name), (Type), (Flags), (MaxOfInstances), {(ULONG_PTR)NULL}, NULL, 0, (ValueSize), (ValueBuffer)}
#else
    #define NEW_OPT(Name, LongName, Type, Flags, MaxOfInstances, ValueSize, ValueBuffer) \
        {(Name), (LongName), (Type), (Flags), (MaxOfInstances), {(ULONG_PTR)NULL}, NULL, 0, (ValueSize), (ValueBuffer)}
#endif

/**
 * @brief   Initializes a new option entry.
 **/
#ifndef LONG_OPTION_NAMES
    #define NEW_OPT_EX(Name, Type, Flags, AllowedValues, MaxOfInstances, ValueSize, ValueBuffer) \
        {(Name), (Type), (Flags), (MaxOfInstances), {(ULONG_PTR)(AllowedValues)}, NULL, 0, (ValueSize), (ValueBuffer)}
#else
    #define NEW_OPT_EX(Name, LongName, Type, Flags, AllowedValues, MaxOfInstances, ValueSize, ValueBuffer) \
        {(Name), (LongName), (Type), (Flags), (MaxOfInstances), {(ULONG_PTR)(AllowedValues)}, NULL, 0, (ValueSize), (ValueBuffer)}
#endif


/**
 * @brief   Enumerates the known parser returned errors.
 **/
typedef enum _PARSER_ERROR
{
    Success = 0,
    InvalidSyntax,
    InvalidOption,
    ValueRequired,
    ValueIsEmpty,
    InvalidValue,
    ValueNotAllowed,
    TooManySameOption,
    MandatoryOptionAbsent,
} PARSER_ERROR, *PPARSER_ERROR;

/**
 * @brief   Parser error handling/output callback.
 **/
typedef VOID (__cdecl *PRINT_ERROR_FUNC)(IN PARSER_ERROR, ...);


/*****************************************************************************/


static PWSTR
TrimLeftRightWhitespace(
    IN PWSTR String)
{
    PWSTR pStr;

    /* Trim whitespace on left (just advance the pointer) */
    while (*String && iswspace(*String))
        ++String;

    /* Trim whitespace on right (NULL-terminate) */
    pStr = String + wcslen(String) - 1;
    while (pStr >= String && iswspace(*pStr))
        --pStr;
    *++pStr = L'\0';

    /* Return the modified pointer */
    return String;
}


/**
 * @name DoParseArgv
 *     Parses command-line options from an arguments vector (argc, argv[]).
 *
 * @param[in]       argc
 *     Number of elements (arguments) in the arguments vector.
 *
 * @param[in]       argv
 *     The arguments vector. Each element is a pointer to a string.
 *
 * @param[in,out]   Options
 *     An array of @b OPTION structures, each describing a supported option
 *     for which to retrieve its value (if any) from the arguments vector.
 *
 * @param[in]       NumOptions
 *     Number of options (elements) in the @p Options array.
 *
 * @param[in,opt]   PrintErrorFunc
 *     Optional @b PRINT_ERROR_FUNC callback for user-specific handling/output
 *     of parser errors.
 *
 * @return
 *     @b TRUE when successful parsing,
 *     @b FALSE if not.
 *
 **/

typedef PCWSTR (*TOKEN_FUNC)(
    IN OUT PVOID Context);

static BOOL
DoParseWorker(
    IN OUT PVOID Context,
    IN TOKEN_FUNC TokenFunc,
    IN OUT POPTION Options,
    IN ULONG NumOptions,
    IN PRINT_ERROR_FUNC PrintErrorFunc OPTIONAL)
{
    BOOL ExclusiveOptionPresent = FALSE;
    PCWSTR OptionStr = NULL;
    // PCWSTR OptionName = NULL;
    // PCWSTR OptionVal = NULL;
    PCWSTR ptr;
#ifdef LONG_OPTION_NAMES
    BOOL bLongOpt;
#endif
    UINT i;

    /*
     * The 'Option' index is reset to 'NumOptions' (total number of elements in
     * the 'Options' list) before retrieving a new option. This is done so that
     * we know it cannot index a valid option at that moment.
     */
    UINT Option = NumOptions;

    /* Parse command line for options */
    while ((ptr = TokenFunc(Context)))
    {
        /* Check for new options */

        if (ptr[0] == L'-' || ptr[0] == L'/')
        {
            /// FIXME: This test is problematic if this concerns the last option in
            /// the command-line! A hack-fix is to repeat this check after the loop.
            if (Option != NumOptions)
            {
                if (PrintErrorFunc)
                    PrintErrorFunc(ValueRequired, OptionStr);
                return FALSE;
            }

            /*
             * If we have already encountered an (unique) exclusive option,
             * just break now.
             */
            if (ExclusiveOptionPresent)
                break;

#ifdef LONG_OPTION_NAMES
            /* Short form ("-x") or long form ("--xxx") option? */
            // Long option must always start with dash '-'.
            bLongOpt = (ptr[0] == '-') && (ptr[1] == '-');
#endif

            OptionStr = ptr;

            /* Lookup for the option in the list of options */
            for (Option = 0; Option < NumOptions; ++Option)
            {
#ifndef ALT_NAMES

#ifdef LONG_OPTION_NAMES
                if ((!bLongOpt && (_wcsicmp(OptionStr + 1, Options[Option].OptionName) == 0)) ||
                    ( bLongOpt && (_wcsicmp(OptionStr + 2, Options[Option].OptionLongName) == 0)))
#else
                if (_wcsicmp(OptionStr + 1, Options[Option].OptionName) == 0)
#endif
                {
                    break;
                }

#else

                PCWSTR OptionName, Scan;
                SIZE_T Length;

#ifdef LONG_OPTION_NAMES
                if (bLongOpt && (_wcsicmp(OptionStr + 2, Options[Option].OptionLongName) == 0))
                {
                    break;
                }

                if (bLongOpt)
                    continue;
#endif

                OptionName = Options[Option].OptionName;
#if 0
                if (!OptionName)
                {
                    /* The array is empty, no allowed values */
                    if (PrintErrorFunc)
                        PrintErrorFunc(InvalidValue, *pStr, OptionStr);
                    return FALSE;
                }
#endif

                Scan = OptionName;
                while (*Scan)
                {
                    /* Find the values separator */
                    Length = wcscspn(Scan, L"|");

                    /* Check whether this is an allowed value */
                    if ((wcslen(OptionStr + 1) == Length) &&
                        (_wcsnicmp(OptionStr + 1, Scan, Length) == 0))
                    {
                        /* Found it! */
                        break;
                    }

                    /* Go to the next test value */
                    Scan += Length;
                    if (*Scan) ++Scan; // Skip the separator
                }
                if (*Scan)
                {
                    /* Found it! */
                    break;
                }

#endif
            }

            if (Option >= NumOptions)
            {
                if (PrintErrorFunc)
                    PrintErrorFunc(InvalidOption, OptionStr);
                return FALSE;
            }


            /*
             * An option is being set
             */

            if (Options[Option].MaxOfInstances != 0 &&
                Options[Option].Instances >= Options[Option].MaxOfInstances)
            {
                if (PrintErrorFunc)
                    PrintErrorFunc(TooManySameOption, OptionStr, Options[Option].MaxOfInstances);
                return FALSE;
            }
            ++Options[Option].Instances;

            Options[Option].OptionStr = OptionStr;

            /*
             * If this option is exclusive, remember it for later.
             * We will then short-circuit the regular validity checks
             * and instead check whether this is the only option specified
             * on the command-line.
             */
            if (Options[Option].Flags & OPTION_EXCLUSIVE)
                ExclusiveOptionPresent = TRUE;

            /* Pre-process the option before setting its value */
            switch (Options[Option].Type)
            {
                case TYPE_None: // ~= TYPE_Bool
                {
                    /* Set the associated boolean */
                    *(BOOL*)Options[Option].Value = TRUE;

                    /* No associated value, so reset the index */
                    Option = NumOptions;
                    break;
                }

#ifdef INT_FLAGS
                case TYPE_Flag8:
                {
                    /* Set the associated flag */
                    *(UINT8*)Options[Option].Value |= Options[Option].ValueFlag8;

                    /* No associated value, so reset the index */
                    Option = NumOptions;
                    break;
                }

                case TYPE_Flag16:
                {
                    /* Set the associated flag */
                    *(UINT16*)Options[Option].Value |= Options[Option].ValueFlag16;

                    /* No associated value, so reset the index */
                    Option = NumOptions;
                    break;
                }
#endif

#ifdef INT_FLAGS
                case TYPE_Flag32: // == TYPE_Flag
#else
                case TYPE_Flag:
#endif
                {
                    /* Set the associated flag */
                    *(UINT32*)Options[Option].Value |= Options[Option].ValueFlag32;

                    /* No associated value, so reset the index */
                    Option = NumOptions;
                    break;
                }

#ifdef INT_FLAGS
                case TYPE_Flag64:
                {
                    /* Set the associated flag */
                    *(UINT64*)Options[Option].Value |= Options[Option].ValueFlag64;

                    /* No associated value, so reset the index */
                    Option = NumOptions;
                    break;
                }
#endif

                case TYPE_Str:
#ifdef INT_TYPES
                case TYPE_U8:
                case TYPE_U16:
                case TYPE_U32:
                case TYPE_U64:
#else
                case TYPE_ULong:
#endif
                    break;

                default:
                {
                    wprintf(L"PARSER: Unsupported option type %lu\n", Options[Option].Type);
                    break;
                }
            }
        }
        else
        {
            if (Option >= NumOptions)
            {
                break;
            }

            /*
             * A value for an option is being set
             */
            switch (Options[Option].Type)
            {
                case TYPE_None:
#ifdef INT_FLAGS
                case TYPE_Flag8:
                case TYPE_Flag16:
                case TYPE_Flag32: // == TYPE_Flag
                case TYPE_Flag64:
#else
                case TYPE_Flag:
#endif
                {
                    /* There must be no associated value */
                    if (PrintErrorFunc)
                        PrintErrorFunc(ValueNotAllowed, OptionStr);
                    return FALSE;
                }

                case TYPE_Str:
                {
                    /* Retrieve the string */
                    PWSTR* pStr = (PWSTR*)Options[Option].Value;
                    *pStr = (PWSTR)ptr;

                    /* Trim whitespace if needed */
                    if (Options[Option].Flags & OPTION_TRIM_SPACE)
                        *pStr = TrimLeftRightWhitespace(*pStr);

                    /* Check whether or not the value can be empty */
                    if ((Options[Option].Flags & OPTION_NOT_EMPTY) && !**pStr)
                    {
                        /* Value cannot be empty */
                        if (PrintErrorFunc)
                            PrintErrorFunc(ValueIsEmpty, OptionStr);
                        return FALSE;
                    }

                    /* Check whether the value is part of the allowed list of values */
                    if (Options[Option].Flags & OPTION_ALLOWED_LIST)
                    {
                        PCWSTR AllowedValues, Scan;
                        SIZE_T Length;

                        AllowedValues = Options[Option].AllowedValues;
                        if (!AllowedValues)
                        {
                            /* The array is empty, no allowed values */
                            if (PrintErrorFunc)
                                PrintErrorFunc(InvalidValue, *pStr, OptionStr);
                            return FALSE;
                        }

                        Scan = AllowedValues;
                        while (*Scan)
                        {
                            /* Find the values separator */
                            Length = wcscspn(Scan, L"|");

                            /* Check whether this is an allowed value */
                            if ((wcslen(*pStr) == Length) &&
                                (_wcsnicmp(*pStr, Scan, Length) == 0))
                            {
                                /* Found it! */
                                break;
                            }

                            /* Go to the next test value */
                            Scan += Length;
                            if (*Scan) ++Scan; // Skip the separator
                        }

                        if (!*Scan)
                        {
                            /* The value is not allowed */
                            if (PrintErrorFunc)
                                PrintErrorFunc(InvalidValue, *pStr, OptionStr);
                            return FALSE;
                        }
                    }

                    break;
                }

#ifdef INT_TYPES
                case TYPE_U8:
                {
                    PWCHAR pszNext = NULL;

                    /* The number is specified in base 10 */
                    // NOTE: We might use '0' so that the base is automatically determined.
                    ULONG Val = wcstoul(ptr, &pszNext, 10);
                    if (*pszNext || (Val > UINT8_MAX))
                    {
                        /* The value is not a valid numeric value and is not allowed */
                        if (PrintErrorFunc)
                            PrintErrorFunc(InvalidValue, ptr, OptionStr);
                        return FALSE;
                    }

                    *(UINT8*)Options[Option].Value = (UINT8)Val;
                    break;
                }

                case TYPE_U16:
                {
                    PWCHAR pszNext = NULL;

                    /* The number is specified in base 10 */
                    // NOTE: We might use '0' so that the base is automatically determined.
                    ULONG Val = wcstoul(ptr, &pszNext, 10);
                    if (*pszNext || (Val > UINT16_MAX))
                    {
                        /* The value is not a valid numeric value and is not allowed */
                        if (PrintErrorFunc)
                            PrintErrorFunc(InvalidValue, ptr, OptionStr);
                        return FALSE;
                    }

                    *(UINT16*)Options[Option].Value = (UINT16)Val;
                    break;
                }
#endif

#ifdef INT_FLAGS
                case TYPE_U32: // == TYPE_ULong
#else
                case TYPE_ULong:
#endif
                {
                    PWCHAR pszNext = NULL;

                    /* The number is specified in base 10 */
                    // NOTE: We might use '0' so that the base is automatically determined.
                    *(UINT32*)Options[Option].Value = wcstoul(ptr, &pszNext, 10);
                    if (*pszNext)
                    {
                        /* The value is not a valid numeric value and is not allowed */
                        if (PrintErrorFunc)
                            PrintErrorFunc(InvalidValue, ptr, OptionStr);
                        return FALSE;
                    }
                    break;
                }

#ifdef INT_TYPES
                case TYPE_U64:
                {
                    PWCHAR pszNext = NULL;

                    /* The number is specified in base 10 */
                    // NOTE: We might use '0' so that the base is automatically determined.
                    *(UINT64*)Options[Option].Value = wcstoull(ptr, &pszNext, 10);
                    if (*pszNext)
                    {
                        /* The value is not a valid numeric value and is not allowed */
                        if (PrintErrorFunc)
                            PrintErrorFunc(InvalidValue, ptr, OptionStr);
                        return FALSE;
                    }
                    break;
                }
#endif

                default:
                {
                    wprintf(L"PARSER: Unsupported option type %lu\n", Options[Option].Type);
                    break;
                }
            }

            /* Reset the index */
            Option = NumOptions;
        }
    }

    /// HACK-fix for the check done inside the loop.
    if (Option != NumOptions)
    {
        if (PrintErrorFunc)
            PrintErrorFunc(ValueRequired, OptionStr);
        return FALSE;
    }

    /* Finalize options validity checks */

    if (ExclusiveOptionPresent)
    {
        /*
         * An exclusive option present on the command-line:
         * check whether this is the only option specified.
         */
        for (i = 0; i < NumOptions; ++i)
        {
            if (!(Options[i].Flags & OPTION_EXCLUSIVE) && (Options[i].Instances != 0))
            {
                /* A non-exclusive option is present on the command-line, fail */
                if (PrintErrorFunc)
                    PrintErrorFunc(InvalidSyntax);
                return FALSE;
            }
        }

        /* No other checks needed, we are done */
        return TRUE;
    }

    /* Check whether the required options were specified */
    for (i = 0; i < NumOptions; ++i)
    {
        /* Regular validity checks */
        if ((Options[i].Flags & OPTION_MANDATORY) && (Options[i].Instances == 0))
        {
            if (PrintErrorFunc)
                PrintErrorFunc(MandatoryOptionAbsent, Options[i].OptionName);
            return FALSE;
        }
    }

    /* All checks are done */
    return TRUE;
}


struct _PARSE_ARGV_CTX
{
    IN UINT argc;
    IN WCHAR** argv;
    IN OUT UINT ind;
};

static PCWSTR
ParseTokenArgv(
    IN OUT struct _PARSE_ARGV_CTX* Context)
{
    if (Context->ind >= Context->argc)
        return NULL;
    return Context->argv[Context->ind++];
}

BOOL
DoParseArgv(
    IN UINT argc,
    IN WCHAR* argv[],
    IN OUT POPTION Options,
    IN ULONG NumOptions,
    IN PRINT_ERROR_FUNC PrintErrorFunc OPTIONAL)
{
    struct _PARSE_ARGV_CTX Context = { argc, argv, 1 };
    return DoParseWorker(&Context, ParseTokenArgv,
                         Options, NumOptions, PrintErrorFunc);
}

#if 0
BOOL
DoParseLine(
    IN PCWSTR CmdLine,
    IN OUT POPTION Options,
    IN ULONG NumOptions,
    IN PRINT_ERROR_FUNC PrintErrorFunc OPTIONAL)
{
    return DoParseWorker(CmdLine, ParseTokenStr,
                         Options, NumOptions, PrintErrorFunc);
}
#endif

#ifdef __cplusplus
}
#endif

#endif  /* __PARSER_H__ */

/* EOF */
