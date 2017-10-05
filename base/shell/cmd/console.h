
#pragma once

/* Cache codepage for text streams */
extern UINT InputCodePage;
extern UINT OutputCodePage;

/* Global console Screen and Pager */
extern CON_SCREEN StdOutScreen;
extern CON_PAGER  StdOutPager;


VOID ConInDisable(VOID);
VOID ConInEnable(VOID);
VOID ConInFlush(VOID);
VOID ConInKey(PKEY_EVENT_RECORD);
VOID ConInString(LPTSTR, DWORD);

/* Unique variable names generation inside macros */
#define CONCAT1(a, b) a ## b
#define CONCAT(a, b) CONCAT1(a, b)

#define ConOutChar(c) \
do { \
    TCHAR CONCAT(ch, __LINE__) = (c); \
    ConStreamWrite(StdOut, &CONCAT(ch, __LINE__), 1); \
} while(0)

#define ConErrChar(c) \
do { \
    TCHAR CONCAT(ch, __LINE__) = (c); \
    ConStreamWrite(StdErr, &CONCAT(ch, __LINE__), 1); \
} while(0)

#define ConOutPuts(szStr) \
    ConPuts(StdOut, (szStr))

#define ConErrPuts(szStr) \
    ConPuts(StdErr, (szStr))

#define ConOutResPuts(uID) \
    ConResPuts(StdOut, (uID))

#define ConErrResPuts(uID) \
    ConResPuts(StdErr, (uID))

#define ConOutPrintf(szStr, ...) \
    ConPrintf(StdOut, (szStr), ##__VA_ARGS__)

#define ConErrPrintf(szStr, ...) \
    ConPrintf(StdErr, (szStr), ##__VA_ARGS__)

#define ConOutResPrintf(uID, ...) \
    ConResPrintf(StdOut, (uID), ##__VA_ARGS__)

#define ConErrResPrintf(uID, ...) \
    ConResPrintf(StdErr, (uID), ##__VA_ARGS__)

VOID __cdecl ConFormatMessage(PCON_STREAM Stream, DWORD MessageId, ...);

#define ConOutFormatMessage(MessageId, ...) \
    ConFormatMessage(StdOut, (MessageId), ##__VA_ARGS__)

#define ConErrFormatMessage(MessageId, ...) \
    ConFormatMessage(StdErr, (MessageId), ##__VA_ARGS__)

BOOL ConPrintfVPaging(PCON_PAGER Pager, BOOL StartPaging, LPTSTR szFormat, va_list arg_ptr);
BOOL __cdecl ConOutPrintfPaging(BOOL StartPaging, LPTSTR szFormat, ...);
VOID ConOutResPaging(BOOL StartPaging, UINT resID);

SHORT GetCursorX(VOID);
SHORT GetCursorY(VOID);
VOID  GetCursorXY(PSHORT, PSHORT);
VOID  SetCursorXY(SHORT, SHORT);

VOID GetScreenSize(PSHORT, PSHORT);
VOID SetCursorType(BOOL, BOOL);


#ifdef INCLUDE_CMD_COLOR
BOOL ConGetDefaultAttributes(PWORD pwDefAttr);
#endif

BOOL ConSetTitle(IN LPCTSTR lpConsoleTitle);

#ifdef INCLUDE_CMD_BEEP
VOID ConRingBell(HANDLE hOutput);
#endif

#ifdef INCLUDE_CMD_COLOR
BOOL ConSetScreenColor(HANDLE hOutput, WORD wColor, BOOL bFill);
#endif


//
// The following is possibly of no use at all...
//

// TCHAR  cgetchar (VOID);
// BOOL   CheckCtrlBreak (INT);

// #define PROMPT_NO    0
// #define PROMPT_YES   1
// #define PROMPT_ALL   2
// #define PROMPT_BREAK 3

// INT FilePromptYN (UINT);
// INT FilePromptYNA (UINT);
