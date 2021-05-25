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
 * PARSER_ALT_NAMES         ///< Supports option name "synonyms" (alts), e.g. "-?" or "-h" for help.
 * PARSER_LONG_OPTION_NAMES ///< Supports "--long_name" in addition to "-n" or "/n".
 * PARSER_INT_FLAGS         ///< Supports different integer-sized flags.
 * PARSER_INT_TYPES         ///< Supports different integer-sized types.
 */


#ifndef __PARSER_H__
#define __PARSER_H__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Limits of the different supported integer types */

/* NOTE: Use (x-1) to prevent warnings */
#ifndef INT8_MIN
#define INT8_MIN    ((INT8)(-127 - 1))
#endif
#ifndef INT8_MAX
#define INT8_MAX    ((INT8)127)
#endif

#ifndef INT16_MIN
#define INT16_MIN   ((INT16)(-32767 - 1))
#endif
#ifndef INT16_MAX
#define INT16_MAX   ((INT16)32767)
#endif

#ifndef INT32_MIN
#define INT32_MIN   ((INT32)(-2147483647L - 1))
#endif
#ifndef INT32_MAX
#define INT32_MAX   ((INT32)2147483647L)
#endif

#ifndef INT64_MIN
#define INT64_MIN   ((INT64)(-9223372036854775807LL - 1))
#endif
#ifndef INT64_MAX
#define INT64_MAX   ((INT64)9223372036854775807LL)
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
#ifdef PARSER_INT_FLAGS
    TYPE_Flag8,
    TYPE_Flag16,
    TYPE_Flag32,
    TYPE_Flag = TYPE_Flag32, // "Default" flag type.
    TYPE_Flag64,
#else
    TYPE_Flag,
#endif
    TYPE_Str,
#ifdef PARSER_INT_TYPES
    TYPE_I8,
    TYPE_I16,
    TYPE_I32,
    TYPE_Long = TYPE_I32,    // "Default" signed integer type.
    TYPE_I64,
#else
    TYPE_Long,
#endif
#ifdef PARSER_INT_TYPES
    TYPE_U8,
    TYPE_U16,
    TYPE_U32,
    TYPE_ULong = TYPE_U32,   // "Default" unsigned integer type.
    TYPE_U64,
#else
    TYPE_ULong,
#endif
/* Newer option types must be appended below */
} OPTION_TYPE, *POPTION_TYPE;

/**
 * @brief   Option parsing flags.
 **/
#define OPTION_DEFAULT      0x01
#define OPTION_ALLOWED_LIST 0x02
#define OPTION_NOT_EMPTY    0x04
#define OPTION_TRIM_SPACE   0x08
#define OPTION_EXCLUSIVE    0x10
#define OPTION_MANDATORY    0x20

/**
 * @brief   Describes a specific option.
 **/
typedef struct _OPTION
{
    /*
     * Constant static data
     */
    PCWSTR OptionName;      /**< Option switch name. When @b PARSER_ALT_NAMES
                             **  support is enabled, this can be a list of
                             **  synonyms of the possible option names, given
                             **  as a string list separated by a pipe symbol '|'.
                             **  Example: "?|h" for designating an option that
                             **  can be either specified by "/?" or by "/h".
                             **/
#ifdef PARSER_LONG_OPTION_NAMES
    PCWSTR OptionLongName;  /**< Long-format option switch name **/
#endif

    OPTION_TYPE Type;       /**< Type of data stored in the 'Value' member
                             **  (UNUSED) (bool, string, int, ..., or custom
                             **  parsing by using a callback function).
                             **/

    ULONG Flags;            /**< OPTION_xxx flags **/

    ULONG MaxOfInstances;   /**< Maximum number of times this option can be
                             **  seen in the command line (or 0: do not care).
                             **/

    // PWSTR OptionHelp;       // Help string, or resource ID of the (localized) string (use the MAKEINTRESOURCE macro to create this value).
    // PVOID Callback() ??
    union
    {
        ULONG_PTR Data;         /**< Dummy member whose size encompasses all
                                 **  the other members of this union. **/

#ifdef PARSER_INT_FLAGS
        UINT8 ValueFlag8;       /**< For an option of type @b TYPE_Flag8, this is
                                 **  the flag value to set when the option is encountered. **/

        UINT16 ValueFlag16;     /**< For an option of type @b TYPE_Flag16, this is
                                 **  the flag value to set when the option is encountered. **/
#endif

        UINT32 ValueFlag32;     /**< For an option of type @b TYPE_Flag32, this is
                                 **  the flag value to set when the option is encountered.
                                 **/

#ifdef PARSER_INT_FLAGS
        UINT64 ValueFlag64;     /**< For an option of type @b TYPE_Flag64, this is
                                 **  the flag value to set when the option is encountered.
                                 **/
#endif

        PCWSTR AllowedValues;   /**< For an option of type @b TYPE_Str, using
                                 **  @b OPTION_ALLOWED_LIST, this is an optional
                                 **  list of allowed values, given as a string
                                 **  of values separated by a pipe symbol '|'.
                                 **/
    };

    /*
     * Dynamic parsing data
     */
    PCWSTR OptionStr;       // Pointer to the original option string.
    ULONG Instances;        // Number of times this option is seen in the command line.
    ULONG ValueSize;        // Size of the buffer pointed by 'Value' ?? (UNUSED yet)
    PVOID Value;            // A pointer to part of the command line, or an allocated buffer.
} OPTION, *POPTION;

/**
 * @brief   Initializes a new option entry.
 **/
#ifndef PARSER_LONG_OPTION_NAMES
    #define NEW_OPT(Name, Type, Flags, MaxOfInstances, ValueSize, ValueBuffer) \
        {(Name), (Type), (Flags), (MaxOfInstances), {(ULONG_PTR)NULL}, NULL, 0, (ValueSize), (ValueBuffer)}
#else
    #define NEW_OPT(Name, LongName, Type, Flags, MaxOfInstances, ValueSize, ValueBuffer) \
        {(Name), (LongName), (Type), (Flags), (MaxOfInstances), {(ULONG_PTR)NULL}, NULL, 0, (ValueSize), (ValueBuffer)}
#endif

/**
 * @brief   Initializes a new option entry.
 **/
#ifndef PARSER_LONG_OPTION_NAMES
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


/**
 * @brief
 *     Trims a string for whitespace on both sides (left and right).
 *
 * @param[in]   String
 *     String to trim for whitespace.
 *
 * @return
 *     Adjusted pointer to the trimmed string.
 **/
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
 * @brief
 *     Finds a sub-string in a string list.
 *
 * @param[in]   SubStrToFind
 *     The string to find in the string list.
 *
 * @param[in]   StrList
 *     A (NULL-terminated) string list; each sub-string is separated
 *     with one of the separator characters listed in the @b Separators
 *     string argument.
 *
 * @param[in]   Separators
 *     A string specifying the different characters that can serve as
 *     separators for the sub-strings in the @b StrList string list.
 *
 * @param[in]   CaseInsensitive
 *     TRUE if the search is to be done in a case insensitive way;
 *     FALSE if not.
 *
 * @return
 *     A pointer to the matched sub-string in the string list.
 *     NULL if the sub-string could not found.
 **/
static PCWSTR
FindSubStrInStrList(
    IN PCWSTR SubStrToFind,
    IN PCWSTR StrList,
    IN PCWSTR Separators,
    IN BOOLEAN CaseInsensitive)
{
    SIZE_T Length;

    while (*StrList)
    {
        /* Find the next separator */
        Length = wcscspn(StrList, Separators);

        /* Check whether the sub-string has been found */
        if ((wcslen(SubStrToFind) == Length) &&
            ((CaseInsensitive ? _wcsnicmp(SubStrToFind, StrList, Length)
                              :   wcsncmp(SubStrToFind, StrList, Length)) == 0))
        {
            /* Found it, return a pointer to it in the string list */
            return StrList;
        }

        /* Go to the next sub-string */
        StrList += Length;
        if (*StrList) ++StrList; // Skip the separator
    }

    /* Sub-string not found */
    return NULL;
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
 * @param[in,opt]   SwitchChars
 *     Optional string specifying different allowed characters that prefix
 *     command-line options (switches). Default is "-/", i.e. an option can
 *     either start with '-' or with '/'. Note that when long option names
 *     support is enabled (compile with the @b PARSER_LONG_OPTION_NAMES define),
 *     long names can only start with two dashes (e.g. "--xxx").
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
 **/

typedef PCWSTR (*TOKEN_FUNC)(
    IN OUT PVOID Context);

static BOOL
DoParseWorker(
    IN OUT PVOID Context,
    IN TOKEN_FUNC TokenFunc,
    IN PCWSTR SwitchChars OPTIONAL,
    IN OUT POPTION Options,
    IN ULONG NumOptions,
    IN PRINT_ERROR_FUNC PrintErrorFunc OPTIONAL)
{
    POPTION DefaultOption = NULL;   // The default option (if any).
    POPTION Option = NULL;          // Current option. Is reset before retrieving a new option.
    BOOL ExclusiveOptionPresent = FALSE;
    PCWSTR OptionStr = NULL;
    // PCWSTR OptionName = NULL;
    // PCWSTR OptionVal = NULL;
    PCWSTR ptr;
    UINT i;

    /* Set default switch characters if none were provided */
    if (!SwitchChars)
        SwitchChars = L"-/";

    /* Retrieve any default option in the options list */
    for (i = 0; i < NumOptions; ++i)
    {
        /* If this is a nameless option, it must have the OPTION_DEFAULT flag */
        if (!Options[i].OptionName || !*Options[i].OptionName)
        {
            if (!(Options[i].Flags & OPTION_DEFAULT))
            {
#if 0
                /* No option name */
                if (PrintErrorFunc)
                    PrintErrorFunc(InvalidValue, *pStr, OptionStr);
#endif
                // ASSERT(FALSE);
                return FALSE;
            }
        }

        if (Options[i].Flags & OPTION_DEFAULT)
        {
            if (!DefaultOption)
            {
                /* Found it! */
                DefaultOption = &Options[i];
                /* But don't break yet; we verify that there is
                 * no other default option specification. */
            }
            else
            {
#if 0
                /* No option name */
                if (PrintErrorFunc)
                    PrintErrorFunc(InvalidValue, *pStr, OptionStr);
#endif
                // ASSERT(FALSE);
                return FALSE;
            }
        }
    }

    /* Parse the command line for options */
    while ((ptr = TokenFunc(Context)))
    {
        /*
         * Check for named options (switches).
         */

#ifdef PARSER_LONG_OPTION_NAMES
        /* Short form ("-x") or long form ("--xxx") option? */
        // Long option must always start with dash '-'.
        BOOL bLongOpt = (ptr[0] == '-') && (ptr[1] == '-');
#endif
        if (
#ifdef PARSER_LONG_OPTION_NAMES
            bLongOpt ||
#endif
            wcschr(SwitchChars, ptr[0]))
        {
            /// FIXME: This test is problematic if this concerns the last option in
            /// the command-line! A hack-fix is to repeat this check after the loop.
            if (Option)
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

            OptionStr = ptr;

            /* Lookup for the option in the options list */
            for (i = 0; i < NumOptions; ++i)
            {
                /* Ignore nameless options (must be unique;
                 * this is the default option) */
                if (!Options[i].OptionName || !*Options[i].OptionName)
                {
                    // ASSERT(DefaultOption == &Options[i]);
                    continue;
                }

#ifndef PARSER_ALT_NAMES
#define __CHECK_OPTION_NAME(Name, ExpectedName) \
    (_wcsicmp(Name, ExpectedName) == 0)
#else
#define __CHECK_OPTION_NAME(Name, ExpectedName) \
    (FindSubStrInStrList(Name, ExpectedName, L"|", TRUE) != NULL)
#endif

#ifdef PARSER_LONG_OPTION_NAMES
                if ((!bLongOpt && __CHECK_OPTION_NAME(OptionStr + 1, Options[i].OptionName)) ||
                    ( bLongOpt && (_wcsicmp(OptionStr + 2, Options[i].OptionLongName) == 0)))
#else
                if (__CHECK_OPTION_NAME(OptionStr + 1, Options[i].OptionName))
#endif
                {
                    /* Found it! */
                    break;
                }

#undef __CHECK_OPTION_NAME
            }
            if (i >= NumOptions)
            {
                if (PrintErrorFunc)
                    PrintErrorFunc(InvalidOption, OptionStr);
                return FALSE;
            }
            Option = &Options[i];


            /*
             * A named option is being set.
             */

            if (Option->MaxOfInstances != 0 &&
                Option->Instances >= Option->MaxOfInstances)
            {
                if (PrintErrorFunc)
                    PrintErrorFunc(TooManySameOption, OptionStr, Option->MaxOfInstances);
                return FALSE;
            }
            ++Option->Instances;

            Option->OptionStr = OptionStr;

            /*
             * If this option is exclusive, remember it for later.
             * We will then short-circuit the regular validity checks
             * and instead check whether this is the only option specified
             * on the command-line.
             */
            if (Option->Flags & OPTION_EXCLUSIVE)
                ExclusiveOptionPresent = TRUE;

            /* Pre-process the option before setting its value */
            switch (Option->Type)
            {
                case TYPE_None: // ~= TYPE_Bool
                {
                    /* Set the associated boolean */
                    *(BOOL*)Option->Value = TRUE;

                    /* No associated value, so reset the current option */
                    Option = NULL;
                    break;
                }

#ifdef PARSER_INT_FLAGS
                case TYPE_Flag8:
                {
                    /* Set the associated flag */
                    *(UINT8*)Option->Value |= Option->ValueFlag8;

                    /* No associated value, so reset the current option */
                    Option = NULL;
                    break;
                }

                case TYPE_Flag16:
                {
                    /* Set the associated flag */
                    *(UINT16*)Option->Value |= Option->ValueFlag16;

                    /* No associated value, so reset the current option */
                    Option = NULL;
                    break;
                }
#endif

#ifdef PARSER_INT_FLAGS
                case TYPE_Flag32: // == TYPE_Flag
#else
                case TYPE_Flag:
#endif
                {
                    /* Set the associated flag */
                    *(UINT32*)Option->Value |= Option->ValueFlag32;

                    /* No associated value, so reset the current option */
                    Option = NULL;
                    break;
                }

#ifdef PARSER_INT_FLAGS
                case TYPE_Flag64:
                {
                    /* Set the associated flag */
                    *(UINT64*)Option->Value |= Option->ValueFlag64;

                    /* No associated value, so reset the current option */
                    Option = NULL;
                    break;
                }
#endif

                case TYPE_Str:
#ifdef PARSER_INT_TYPES
                case TYPE_I8:  case TYPE_U8:
                case TYPE_I16: case TYPE_U16:
                case TYPE_I32: case TYPE_U32:
                case TYPE_I64: case TYPE_U64:
#else
                case TYPE_Long: case TYPE_ULong:
#endif
                    break;

                default:
                {
                    wprintf(L"PARSER: Unsupported option type %lu\n", Option->Type);
                    break;
                }
            }
        }
        else
        {
            if (!Option && DefaultOption)
            {
                /* Handle the default option */
                Option = DefaultOption;

                OptionStr = Option->OptionName; // FIXME: Fine but choose the "main" option name in case of synonyms.

                if (Option->MaxOfInstances != 0 &&
                    Option->Instances >= Option->MaxOfInstances)
                {
                    if (PrintErrorFunc)
                        PrintErrorFunc(TooManySameOption, OptionStr, Option->MaxOfInstances);
                    return FALSE;
                }
                ++Option->Instances;

                Option->OptionStr = OptionStr;
            }
            if (!Option)
            {
                /* just stop parsing */
                /****/printf("No default option\n");/****/
                break;
            }

            /*
             * A value for a named option or a default option is being set.
             */
            switch (Option->Type)
            {
                case TYPE_None:
#ifdef PARSER_INT_FLAGS
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
                    PWSTR* pStr = (PWSTR*)Option->Value;
                    *pStr = (PWSTR)ptr;

                    /* Trim whitespace if needed */
                    if (Option->Flags & OPTION_TRIM_SPACE)
                        *pStr = TrimLeftRightWhitespace(*pStr);

                    /* Check whether or not the value can be empty */
                    if ((Option->Flags & OPTION_NOT_EMPTY) && !**pStr)
                    {
                        /* Value cannot be empty */
                        if (PrintErrorFunc)
                            PrintErrorFunc(ValueIsEmpty, OptionStr);
                        return FALSE;
                    }

                    /* Check whether the value is part of the allowed list of values */
                    if (Option->Flags & OPTION_ALLOWED_LIST)
                    {
                        PCWSTR AllowedValues = Option->AllowedValues;

                        if (!AllowedValues)
                        {
                            /* The array is empty, no allowed values */
                            if (PrintErrorFunc)
                                PrintErrorFunc(InvalidValue, *pStr, OptionStr);
                            return FALSE;
                        }

                        /* Check whether this is an allowed value */
                        if (!FindSubStrInStrList(*pStr, AllowedValues, L"|", TRUE))
                        {
                            /* The value is not allowed */
                            if (PrintErrorFunc)
                                PrintErrorFunc(InvalidValue, *pStr, OptionStr);
                            return FALSE;
                        }
                    }

                    break;
                }

#ifdef PARSER_INT_TYPES
                case TYPE_I8:
                {
                    PWCHAR pszNext = NULL;

                    /* The number is specified in base 10 */
                    // NOTE: We might use '0' so that the base is automatically determined.
                    LONG Val = wcstol(ptr, &pszNext, 10);
                    if (*pszNext || (Val < INT8_MIN) || (Val > INT8_MAX))
                    {
                        /* The value is not a valid numeric value and is not allowed */
                        if (PrintErrorFunc)
                            PrintErrorFunc(InvalidValue, ptr, OptionStr);
                        return FALSE;
                    }

                    *(INT8*)Option->Value = (INT8)Val;
                    break;
                }
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

                    *(UINT8*)Option->Value = (UINT8)Val;
                    break;
                }

                case TYPE_I16:
                {
                    PWCHAR pszNext = NULL;

                    /* The number is specified in base 10 */
                    // NOTE: We might use '0' so that the base is automatically determined.
                    LONG Val = wcstol(ptr, &pszNext, 10);
                    if (*pszNext || (Val < INT16_MIN) || (Val > INT16_MAX))
                    {
                        /* The value is not a valid numeric value and is not allowed */
                        if (PrintErrorFunc)
                            PrintErrorFunc(InvalidValue, ptr, OptionStr);
                        return FALSE;
                    }

                    *(INT16*)Option->Value = (INT16)Val;
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

                    *(UINT16*)Option->Value = (UINT16)Val;
                    break;
                }
#endif

#ifdef PARSER_INT_FLAGS
                case TYPE_I32: // == TYPE_Long
#else
                case TYPE_Long:
#endif
                {
                    PWCHAR pszNext = NULL;

                    /* The number is specified in base 10 */
                    // NOTE: We might use '0' so that the base is automatically determined.
                    *(INT32*)Option->Value = wcstol(ptr, &pszNext, 10);
                    if (*pszNext)
                    {
                        /* The value is not a valid numeric value and is not allowed */
                        if (PrintErrorFunc)
                            PrintErrorFunc(InvalidValue, ptr, OptionStr);
                        return FALSE;
                    }
                    break;
                }
#ifdef PARSER_INT_FLAGS
                case TYPE_U32: // == TYPE_ULong
#else
                case TYPE_ULong:
#endif
                {
                    PWCHAR pszNext = NULL;

                    /* The number is specified in base 10 */
                    // NOTE: We might use '0' so that the base is automatically determined.
                    *(UINT32*)Option->Value = wcstoul(ptr, &pszNext, 10);
                    if (*pszNext)
                    {
                        /* The value is not a valid numeric value and is not allowed */
                        if (PrintErrorFunc)
                            PrintErrorFunc(InvalidValue, ptr, OptionStr);
                        return FALSE;
                    }
                    break;
                }

#ifdef PARSER_INT_TYPES
                case TYPE_I64:
                {
                    PWCHAR pszNext = NULL;

                    /* The number is specified in base 10 */
                    // NOTE: We might use '0' so that the base is automatically determined.
                    *(INT64*)Option->Value = wcstoll(ptr, &pszNext, 10);
                    if (*pszNext)
                    {
                        /* The value is not a valid numeric value and is not allowed */
                        if (PrintErrorFunc)
                            PrintErrorFunc(InvalidValue, ptr, OptionStr);
                        return FALSE;
                    }
                    break;
                }
                case TYPE_U64:
                {
                    PWCHAR pszNext = NULL;

                    /* The number is specified in base 10 */
                    // NOTE: We might use '0' so that the base is automatically determined.
                    *(UINT64*)Option->Value = wcstoull(ptr, &pszNext, 10);
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
                    wprintf(L"PARSER: Unsupported option type %lu\n", Option->Type);
                    break;
                }
            }

            /* Reset the current option */
            Option = NULL;
        }
    }

    /// HACK-fix for the check done inside the loop.
    if (Option)
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
    IN PCWSTR SwitchChars OPTIONAL,
    IN OUT POPTION Options,
    IN ULONG NumOptions,
    IN PRINT_ERROR_FUNC PrintErrorFunc OPTIONAL)
{
    struct _PARSE_ARGV_CTX Context = { argc, argv, 1 };
    return DoParseWorker(&Context, ParseTokenArgv, SwitchChars,
                         Options, NumOptions, PrintErrorFunc);
}

#if 0
BOOL
DoParseLine(
    IN PCWSTR CmdLine,
    IN PCWSTR SwitchChars OPTIONAL,
    IN OUT POPTION Options,
    IN ULONG NumOptions,
    IN PRINT_ERROR_FUNC PrintErrorFunc OPTIONAL)
{
    return DoParseWorker(CmdLine, ParseTokenStr, SwitchChars,
                         Options, NumOptions, PrintErrorFunc);
}
#endif

#ifdef __cplusplus
}
#endif

#endif  /* __PARSER_H__ */

/* EOF */
