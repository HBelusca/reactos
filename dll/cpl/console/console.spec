## CPL applet support
@ stdcall CPlApplet(ptr long ptr ptr)

## Shell extension support
@ stdcall -private DllCanUnloadNow()
@ stdcall -private DllGetClassObject(ptr ptr ptr)
@ stdcall -private DllRegisterServer()
@ stdcall -private DllUnregisterServer()
