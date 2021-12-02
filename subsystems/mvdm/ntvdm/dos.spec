;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; NTVDM DOS-32 Emulation ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;

@ stdcall demClientErrorEx(long long long)
@ stdcall demFileDelete(ptr)
@ stdcall demFileFindFirst(ptr ptr long)
@ stdcall demFileFindNext(ptr)
;@ stdcall demGetFileTimeByHandle_WOW
@ stdcall demGetPhysicalDriveType(long)
@ stdcall demIsShortPathName(ptr long)
;@ stdcall demLFNCleanup
;@ stdcall demLFNGetCurrentDirectory
@ stdcall demSetCurrentDirectoryGetDrive(ptr ptr)
;@ stdcall demWOWLFNAllocateSearchHandle
;@ stdcall demWOWLFNCloseSearchHandle
;@ stdcall demWOWLFNEntry
;@ stdcall demWOWLFNGetSearchHandle
;@ stdcall demWOWLFNInit
