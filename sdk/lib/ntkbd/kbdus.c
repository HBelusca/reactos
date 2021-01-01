/*
 * TODO:
 * - Update and fix our keyboard layouts with the information from
 *   http://kbdlayout.info/
 * - Make this one derive from KBDUS.
 */

static VK_TO_BIT VkToBit_Fallback[] =
{
    {VK_SHIFT,   1},
    {VK_CONTROL, 2},
    {VK_MENU,    4},
    {0, 0}
};

static MODIFIERS CharModifiersFallback =
{
    VkToBit_Fallback,
    3,
    {0, 1, 2, 3}
};


static VK_TO_WCHARS3 VkChars3[] =
{
    {VK_OEM_4,   0, {'[', '{', 0x1B}},
    {VK_OEM_6,   0, {']', '}', 0x1D}},
    {VK_OEM_5,   0, {'\\', '|', 0x1C}},
    {VK_OEM_102, 0, {'\\', '|', 0x1C}},
    {VK_BACK,    0, {0x08, 0x08, 0x7F}},
    {VK_ESCAPE,  0, {0x1B, 0x1B, 0x1B}},
    {VK_RETURN,  0, {0x0D, 0x0D, 0x0A}},
    {VK_SPACE,   0, {' ', ' ', ' '}},
    {VK_CANCEL,  0, {0x03, 0x03, 0x03}},
    {0, 0, {0, 0, 0}},
};

static VK_TO_WCHARS2 VkChars2[] =
{
    {VK_OEM_3, 0, {'`', '~'}},
    {'1', 0, {'1', '!'}},
    {'3', 0, {'3', '#'}},
    {'4', 0, {'4', '$'}},
    {'5', 0, {'5', '%'}},
    {'7', 0, {'7', '&'}},
    {'8', 0, {'8', '*'}},
    {'9', 0, {'9', '('}},
    {'0', 0, {'0', ')'}},
    {VK_OEM_PLUS, 0, {'=', '+'}},
    {'Q', 1, {'q', 'Q'}},
    {'W', 1, {'w', 'W'}},
    {'E', 1, {'e', 'E'}},
    {'R', 1, {'r', 'R'}},
    {'T', 1, {'t', 'T'}},
    {'Y', 1, {'y', 'Y'}},
    {'U', 1, {'u', 'U'}},
    {'I', 1, {'i', 'I'}},
    {'O', 1, {'o', 'O'}},
    {'P', 1, {'p', 'P'}},
    {'A', 1, {'a', 'A'}},
    {'S', 1, {'s', 'S'}},
    {'D', 1, {'d', 'D'}},
    {'F', 1, {'f', 'F'}},
    {'G', 1, {'g', 'G'}},
    {'H', 1, {'h', 'H'}},
    {'J', 1, {'j', 'J'}},
    {'K', 1, {'k', 'K'}},
    {'L', 1, {'l', 'L'}},
    {VK_OEM_1, 0, {';', ':'}},
    {VK_OEM_7, 0, {'\'', '\"'}},
    {'Z', 1, {'z', 'Z'}},
    {'X', 1, {'x', 'X'}},
    {'C', 1, {'c', 'C'}},
    {'V', 1, {'v', 'V'}},
    {'B', 1, {'b', 'B'}},
    {'N', 1, {'n', 'N'}},
    {'M', 1, {'m', 'M'}},

    {VK_OEM_COMMA,  0, {',', '<'}},
    {VK_OEM_PERIOD, 0, {'.', '>'}},
    {VK_OEM_2,      0, {'/', '?'}},

    {VK_DECIMAL,  0, {'.', '.'}},
    {VK_TAB,      0, {'\t', '\t'}},
    {VK_ADD,      0, {'+', '+'}},
    {VK_DIVIDE,   0, {'/', '/'}},
    {VK_MULTIPLY, 0, {'*', '*'}},
    {VK_SUBTRACT, 0, {'-', '-'}},
    {0, 0, {0, 0}},
};

static VK_TO_WCHARS4 VkChars4[] =
{
    {'2',          0, {'2', '@', WCH_NONE, 0x00}},
    {'6',          0, {'6', '^', WCH_NONE, 0x1E}},
    {VK_OEM_MINUS, 0, {'-', '_', WCH_NONE, 0x1F}},
    {0, 0, {0, 0, 0, 0}},
};

static VK_TO_WCHARS1 VkChars1[] =
{
    {VK_NUMPAD0, 0, {'0'}},
    {VK_NUMPAD1, 0, {'1'}},
    {VK_NUMPAD2, 0, {'2'}},
    {VK_NUMPAD3, 0, {'3'}},
    {VK_NUMPAD4, 0, {'4'}},
    {VK_NUMPAD5, 0, {'5'}},
    {VK_NUMPAD6, 0, {'6'}},
    {VK_NUMPAD7, 0, {'7'}},
    {VK_NUMPAD8, 0, {'8'}},
    {VK_NUMPAD9, 0, {'9'}},
    {0, 0, {0}},
};

static VK_TO_WCHAR_TABLE VkToWcharTableFallback[] =
{
    {VkChars3, 3, sizeof(VkChars3[0])},
    {VkChars4, 4, sizeof(VkChars4[0])},
    {VkChars2, 2, sizeof(VkChars2[0])},
    // Numpad must appear last for e.g. VkKeyScan('0') to work properly and not give numpad result.
    {VkChars1, 1, sizeof(VkChars1[0])},
    {NULL, 0, 0}
};


static VSC_LPWSTR KeyNamesFallback[] =
{
    {0x01, L"Esc"},
    {0x0E, L"Backspace"},
    {0x0F, L"Tab"},
    {0x1C, L"Enter"},
    {0x1D, L"Ctrl"},
    {0x2A, L"Shift"},
    {0x36, L"Right Shift"},
    {0x37, L"Num *"},
    {0x38, L"Alt"},
    {0x39, L"Space"},
    {0x3A, L"Caps Lock"},
    {0x3B, L"F1"},
    {0x3C, L"F2"},
    {0x3D, L"F3"},
    {0x3E, L"F4"},
    {0x3F, L"F5"},
    {0x40, L"F6"},
    {0x41, L"F7"},
    {0x42, L"F8"},
    {0x43, L"F9"},
    {0x44, L"F10"},
    {0x45, L"Pause"},
    {0x46, L"Scroll Lock"},
    {0x47, L"Num 7"},
    {0x48, L"Num 8"},
    {0x49, L"Num 9"},
    {0x4A, L"Num -"},
    {0x4B, L"Num 4"},
    {0x4C, L"Num 5"},
    {0x4D, L"Num 6"},
    {0x4E, L"Num +"},
    {0x4F, L"Num 1"},
    {0x50, L"Num 2"},
    {0x51, L"Num 3"},
    {0x52, L"Num 0"},
    {0x53, L"Num Del"},
    {0x54, L"Sys Req"},
    {0x57, L"F11"},
    {0x58, L"F12"},
    {0x7C, L"F13"},
    {0x7D, L"F14"},
    {0x7E, L"F15"},
    {0x7F, L"F16"},
    {0x80, L"F17"},
    {0x81, L"F18"},
    {0x82, L"F19"},
    {0x83, L"F20"},
    {0x84, L"F21"},
    {0x85, L"F22"},
    {0x86, L"F23"},
    {0x87, L"F24"},
    {0, NULL}
};

static VSC_LPWSTR KeyNamesExtFallback[] =
{
    {0x1C, L"Num Enter"},
    {0x1D, L"Right Ctrl"},
    {0x35, L"Num /"},
    {0x37, L"Prnt Scrn"},
    {0x38, L"Right Alt"},
    {0x45, L"Num Lock"},
    {0x46, L"Break"},
    {0x47, L"Home"},
    {0x48, L"Up"},
    {0x49, L"Page Up"},
    {0x4B, L"Left"},
    {0x4D, L"Right"},
    {0x4F, L"End"},
    {0x50, L"Down"},
    {0x51, L"Page Down"},
    {0x52, L"Insert"},
    {0x53, L"Delete"},
    {0x54, L"00"},
    {0x56, L"Help"},
    {0x5B, L"Left Windows"},
    {0x5C, L"Right Windows"},
    {0x5D, L"Application"},
    {0, NULL}
};


static USHORT usVSCtoVK[128] =
{
    VK_EMPTY, VK_ESCAPE, '1', '2',
    '3', '4', '5', '6',
    '7', '8', '9', '0',
    VK_OEM_MINUS, VK_OEM_PLUS, VK_BACK,

    VK_TAB, 'Q', 'W', 'E',
    'R', 'T', 'Y', 'U',
    'I', 'O', 'P',
    VK_OEM_4, VK_OEM_6, VK_RETURN,

    VK_LCONTROL,
    'A', 'S', 'D', 'F',
    'G', 'H', 'J', 'K',
    'L', VK_OEM_1, VK_OEM_7, VK_OEM_3,
    VK_LSHIFT, VK_OEM_5,

    'Z', 'X', 'C', 'V',
    'B', 'N', 'M', VK_OEM_COMMA,
    VK_OEM_PERIOD, VK_OEM_2, 0x01A1,

    0x026A, VK_LMENU, VK_SPACE, VK_CAPITAL,

    VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7,
    VK_F8, VK_F9, VK_F10,

    0x0390,
    0x0291,

    0x0C24,
    0x0C26, 0x0C21, 0x006D, 0x0C25, 0x0C0C, 0x0C27, 0x006B, 0x0C23,
    0x0C28, 0x0C22, 0x0C2D, 0x0C2E,
    VK_SNAPSHOT,
    VK_EMPTY, VK_OEM_102, VK_F11, VK_F12,
    VK_CLEAR, 0x00EE, 0x00F1, 0x00EA, 0x00F9, 0x00F5, 0x00F3, VK_EMPTY, VK_EMPTY,
    0x00FB, VK_HELP,
    VK_F13, VK_F14, VK_F15, VK_F16, VK_F17, VK_F18, VK_F19, VK_F20, VK_F21,
    VK_F22, VK_F23,
    VK_OEM_PA3 /*0xED*/, VK_EMPTY, VK_OEM_RESET, VK_EMPTY, 0x00C1, VK_EMPTY, VK_EMPTY,
    VK_F24,
    VK_EMPTY, VK_EMPTY, VK_EMPTY, VK_EMPTY, VK_OEM_PA1 /*0xEB*/, VK_TAB /* VK_OEM_RESET */,
    VK_EMPTY, 0x00C2, 0
}

static VSC_VK VSCtoVK_E0_Fallback[] =
{
    {0x10, 0x1B1},
    {0x19, 0x1B0},
    {0x1D, 0x1A3},
    {0x20, 0x1AD},
    {0x21, 0x1B7},
    {0x22, 0x1B3},
    {0x24, 0x1B2},
    {0x2E, 0x1AE},
    {0x30, 0x1AF},
    {0x32, 0x1AC},
    {0x35, 0x16F},
    {0x37, 0x12C},
    {0x38, 0x1A5},
    {0x47, 0x124},
    {0x48, 0x126},
    {0x49, 0x121},
    {0x4B, 0x125},
    {0x4D, 0x127},
    {0x4F, 0x123},
    {0x50, 0x128},
    {0x51, 0x122},
    {0x52, 0x12D},
    {0x53, 0x12E},
    {0x5B, 0x15B},
    {0x5C, 0x15C},
    {0x5D, 0x15D},
    {0x5F, 0x15F},
    {0x65, 0x1AA},
    {0x66, 0x1AB},
    {0x67, 0x1A8},
    {0x68, 0x1A9},
    {0x69, 0x1A7},
    {0x6A, 0x1A6},
    {0x6B, 0x1B6},
    {0x6C, 0x1B4},
    {0x6D, 0x1B5},
    {0x1C, 0x10D},
    {0x46, 0x103},
    {0, 0}
};

static VSC_VK VSCtoVK_E1_Fallback[] =
{
    {0x1D, 0x13},
    {0, 0}
};

KBDTABLES KbdTablesFallback =
{
    &CharModifiersFallback,
    &VkToWcharTableFallback,

    /* No diacritical marks (dead keys )*/
    NULL,

    &KeyNamesFallback,
    &KeyNamesExtFallback,
    NULL, /* Dead key names */

    &usVSCtoVK,
    sizeof(usVSCtoVK) / sizeof(usVSCtoVK[0]),
    &VSCtoVK_E0_Fallback,
    &VSCtoVK_E1_Fallback,

    MAKELONG(0, 1),

    /* No ligatures */
    0,
    0,
    NULL
};
