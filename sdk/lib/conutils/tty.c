
BOOL
TTYGetScreenInfo(
    IN HANDLE hOutput,
    OUT PCONSOLE_SCREEN_BUFFER_INFO pcsbi)
{
    BOOL Success;

    /* Parameters validation */
    if (!hOutput || !pcsbi)
        return FALSE;

    // ASSERT(IsTTYHandle(hOutput));

#if 0
    /* TODO: Do something adequate for TTYs */
    // FIXME: At the moment we return hardcoded info.
    pcsbi->dwSize.X = 80;
    pcsbi->dwSize.Y = 25;

    // pcsbi->dwCursorPosition;
    // pcsbi->wAttributes;
    // pcsbi->srWindow;
    pcsbi->dwMaximumWindowSize = pcsbi->dwSize;
#else
    hOutput = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                          OPEN_EXISTING, 0, NULL);

    Success = IsConsoleHandle(hOutput) &&
              GetConsoleScreenBufferInfo(hOutput, pcsbi);

    CloseHandle(hOutput);
#endif

    return Success;
}

BOOL
TTYGetCursorInfo(
    IN HANDLE hOutput,
    OUT PCONSOLE_CURSOR_INFO pcci)
{
    BOOL Success;

    /* Parameters validation */
    if (!hOutput || !pcci)
        return FALSE;

    // ASSERT(IsTTYHandle(hOutput));

#if 0
    /* TODO: Do something adequate for TTYs */
    // FIXME: At the moment we return hardcoded info.
    pcci->dwSize = 25;
    pcci->bVisible = TRUE;
#else
    hOutput = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                          OPEN_EXISTING, 0, NULL);

    Success = IsConsoleHandle(hOutput) &&
              GetConsoleCursorInfo(hOutput, pcci);

    CloseHandle(hOutput);
#endif

    return Success;
}

BOOL
TTYSetCursorInfo(
    IN PCON_STREAM Stream, // IN HANDLE hOutput,
    IN PCONSOLE_CURSOR_INFO pcci)
{
    /* Parameters validation */
    if (!Stream /*hOutput*/ || !pcci)
        return FALSE;

    // ASSERT(IsTTYHandle(hOutput));

    /* Set the cursor information */
    ConPrintf(GET_W32(&Stream->Writer) /*hOutput*/,
              L"\x1B[%hu q"  // Mode style
              L"\x1B[?25%c", // Visible (h) or hidden (l)
              (pcci->dwSize <= 15) ? 3 : 1, // Blinking underline (3) or blinking block (1)
              pcci->bVisible ? 'h' : 'l');
    /*
     * Might as well support the following SCO Terminal command:
     * ESC[= s ; e C
     *   Sets cursor parameters (where s is the starting and e is
     *   the ending scanlines of the cursor).
     */
    return TRUE;
}

BOOL
TTYSetCursorPos(
    IN PCON_STREAM Stream, // IN HANDLE hOutput,
    IN COORD dwCursorPosition)
{
    /* Parameters validation */
    if (!Stream /*hOutput*/)
        return FALSE;

    // ASSERT(IsTTYHandle(hOutput));

    /* Set the cursor position */
    ConPrintf(GET_W32(&Stream->Writer) /*hOutput*/,
              L"\x1B[%d;%dH",
              1 + dwCursorPosition.Y,
              1 + dwCursorPosition.X);
    return TRUE;
}
