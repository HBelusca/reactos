
extern BOOL  bUseBashCompletion;
extern TCHAR AutoCompletionChar;
extern TCHAR PathCompletionChar;

BOOL
ReadLine(
    IN HANDLE hInput,
    IN HANDLE hOutput,
    IN OUT LPTSTR str,
    IN DWORD maxlen,
    IN COMPLETER_CALLBACK CompleterCallback,
    IN PVOID CompleterParameter OPTIONAL);

BOOL ReadCommand(LPTSTR, DWORD);

BOOL
CompleteFilename(
    IN PCOMPLETION_CONTEXT Context,
    IN PVOID CompleterParameter OPTIONAL,
    IN OUT LPTSTR str, // == CmdLine
    IN UINT cursor,
    IN UINT charcount,
    IN TCHAR CompletionChar,
    IN ULONG ControlKeyState,
    IN OUT PBOOL RestartCompletion);
