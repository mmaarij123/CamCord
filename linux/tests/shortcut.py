"""Inject the configured shortcut into the isolated Xvfb integration fixture only."""
import ctypes as C
import os
if os.environ.get('CAMCORD_DESKTOP_TEST') != '1' or os.environ.get('DISPLAY') != ':99':
    raise SystemExit('Shortcut fixture requires isolated Xvfb :99')
x = C.CDLL('libX11.so.6'); xt = C.CDLL('libXtst.so.6')
x.XOpenDisplay.argtypes = [C.c_char_p]; x.XOpenDisplay.restype = C.c_void_p
x.XKeysymToKeycode.argtypes = [C.c_void_p, C.c_ulong]; x.XKeysymToKeycode.restype = C.c_ubyte
x.XSync.argtypes = [C.c_void_p, C.c_int]; x.XCloseDisplay.argtypes = [C.c_void_p]
xt.XTestFakeKeyEvent.argtypes = [C.c_void_p, C.c_uint, C.c_int, C.c_ulong]
d = x.XOpenDisplay(None)
if not d: raise SystemExit('Virtual display unavailable')
try:
    keys = [x.XKeysymToKeycode(d, sym) for sym in (0xffe3, 0xffe1, 0xffc5)]
    for key in keys: xt.XTestFakeKeyEvent(d, key, 1, 0)
    for key in reversed(keys): xt.XTestFakeKeyEvent(d, key, 0, 0)
    x.XSync(d, 0)
finally: x.XCloseDisplay(d)
