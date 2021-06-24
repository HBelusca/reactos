
BOOL
Win32ConGetScreenInfo(
    IN HANDLE hOutput,
    OUT PCONSOLE_SCREEN_BUFFER_INFO pcsbi)
{
    // ASSERT(IsConsoleHandle(hOutput));
    return GetConsoleScreenBufferInfo(hOutput, pcsbi);
}

BOOL
Win32ConGetCursorInfo(
    IN HANDLE hOutput,
    OUT PCONSOLE_CURSOR_INFO pcci)
{
    // ASSERT(IsConsoleHandle(hOutput));
    return GetConsoleCursorInfo(hOutput, pcci);
}

BOOL
Win32ConSetCursorInfo(
    IN HANDLE hOutput,
    IN PCONSOLE_CURSOR_INFO pcci)
{
    // ASSERT(IsConsoleHandle(hOutput));
    return SetConsoleCursorInfo(hOutput, pcci);
}

BOOL
Win32ConSetCursorPos(
    IN HANDLE hOutput,
    IN COORD dwCursorPosition)
{
    // ASSERT(IsConsoleHandle(hOutput));
    return SetConsoleCursorPosition(hOutput, dwCursorPosition);
}
