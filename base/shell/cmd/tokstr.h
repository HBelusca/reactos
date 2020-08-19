/*
 *  TOKSTR.H - Command-line options tokenizer.
 */

#pragma once

/* By default, whitespace are delimiters */

/* Flags for TokStrIter() */
#define TS_NONE                 0x00
#define TS_WSPACE_NO_SEPS       0x01    /* Whitespace are not separators, but are included in tokens */
#define TS_STRIP_QUOTES         0x02    /* Strip quotes from token */
// For the first set of delimiters
#define TS_DELIMS1_AS_TOKENS    0x04    /* Special delimiters are separate tokens */
#define TS_DELIMS1_PFX_TOKENS   0x08    /* Special delimiters prefixes a new token */
#define TS_DELIMS1_IN_TOKENS    0x10    /* Special delimiters are included within tokens */
// For the second set of delimiters
#define TS_DELIMS2_AS_TOKENS    0x20
#define TS_DELIMS2_PFX_TOKENS   0x40
#define TS_DELIMS2_IN_TOKENS    0x80

#define TS_DELIMS_AS_TOKENS     (TS_DELIMS1_AS_TOKENS  | TS_DELIMS2_AS_TOKENS )
#define TS_DELIMS_PFX_TOKENS    (TS_DELIMS1_PFX_TOKENS | TS_DELIMS2_PFX_TOKENS)
#define TS_DELIMS_IN_TOKENS     (TS_DELIMS1_IN_TOKENS  | TS_DELIMS2_IN_TOKENS )

LPCTSTR
TokStrIter(
    IN OUT LPCTSTR* Str,
    OUT PSIZE_T Length,
    IN DWORD Flags,
    IN BOOL StripWS,
    IN LPCTSTR WSpaceSeps OPTIONAL,
    IN LPCTSTR OptsDelims1 OPTIONAL,
    IN LPCTSTR OptsDelims2 OPTIONAL);
