
extern BOOL  bUseBashCompletion;
extern TCHAR AutoCompletionChar;
extern TCHAR PathCompletionChar;

BOOL
ReadLine(
    IN HANDLE hInput,
    IN HANDLE hOutput,
    IN OUT LPTSTR str,
    IN DWORD maxlen);

BOOL ReadCommand(LPTSTR, DWORD);



/*****************************************************************************\
 **     G E N E R A L   A U T O C O M P L E T I O N   I N T E R F A C E     **
\*****************************************************************************/

#define IS_COMPLETION_DISABLED(CompletionCtrl)  \
    ((CompletionCtrl) == 0x00 || (CompletionCtrl) == 0x0D || (CompletionCtrl) >= 0x20)

struct _COMPLETION_CONTEXT;

typedef VOID (*COMPLETER_CLEANUP)(struct _COMPLETION_CONTEXT*);

typedef struct _COMPLETION_CONTEXT
{
    LPTSTR CmdLine;
    // UINT InsertPos;
    UINT MaxSize;
    BOOL OnlyDirs;

/*
 * FIXME: Per-command context ????
 *
 * TODO: When intelligent per-command auto-completion is implemented,
 * this should become a generic list of objects (mappable to strings)
 * that is used by the per-command autocompleter to complete the command line.
 * Also the following member should then be added:
 *     LPCOMMAND cmd;
 * being the command being completed.
 */
    PVOID CompleterContext;
    COMPLETER_CLEANUP CompleterCleanup;
} COMPLETION_CONTEXT, *PCOMPLETION_CONTEXT;

VOID InitCompletionContext(IN PCOMPLETION_CONTEXT Context);
VOID FreeCompletionContext(IN PCOMPLETION_CONTEXT Context);

BOOL
DoCompletion(
    IN PCOMPLETION_CONTEXT Context,
    IN OUT LPTSTR CmdLine,
    IN UINT cursor,
    IN UINT charcount,
    IN ULONG ControlKeyState,
    IN BOOL OnlyDirs,
    OUT PBOOL CompletionRestarted OPTIONAL);
