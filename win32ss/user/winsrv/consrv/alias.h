/*
 * LICENSE:         GPL - See COPYING in the top level directory
 * PROJECT:         ReactOS Console Server DLL
 * FILE:            win32ss/user/winsrv/consrv/alias.h
 * PURPOSE:         Alias support functions
 * PROGRAMMERS:     Christoph Wittich
 *                  Johannes Anderwald
 */

#pragma once

VOID
InitConsoleAliases(
    IN PCONSRV_CONSOLE Console);

VOID IntDeleteAllAliases(PCONSRV_CONSOLE Console);
