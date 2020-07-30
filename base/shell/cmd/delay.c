/*
 * DELAY.C - internal command.
 *
 * clone from 4nt delay command
 *
 * 30 Aug 1999
 *     started - Paolo Pantaleo <paolopan@freemail.it>
 *
 *
 */

#include "precomp.h"

#ifdef INCLUDE_CMD_DELAY

INT CommandDelay(LPTSTR param)
{
    DWORD val;
    DWORD mul = 1000;

    nErrorLevel = 0;

    /* Strip leading whitespace */
    while (_istspace(*param))
        ++param;

    if (*param == 0)
    {
        error_req_param_missing();
        return 1;
    }

    if (_tcsnicmp(param, _T("/m"), 2) == 0)
    {
        mul = 1;
        param += 2;
    }

    val = _ttoi(param);
    Sleep(val * mul);

    return 0;
}

#endif /* INCLUDE_CMD_DELAY */
