; MyFunction has a different signature depending on the target OS version.
; Both lines are redirected to the same internal forwarder, which jumps
; through a lazily-initialized pointer (see mymodule.c).
@ stdcall -version=0x500-0x502 MyFunction(long) MyFunction_Fwd
@ stdcall -version=0x600+ MyFunction(long long) MyFunction_Fwd
