#!/usr/bin/env python3
import ctypes as C
import os
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))
from capture_helper import X11

desktop = X11(); lib = desktop.lib
for name, arguments, result in [
    ('XCreateSimpleWindow', [C.c_void_p, C.c_ulong, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_uint, C.c_ulong, C.c_ulong], C.c_ulong),
    ('XMapWindow', [C.c_void_p, C.c_ulong], C.c_int), ('XStoreName', [C.c_void_p, C.c_ulong, C.c_char_p], C.c_int),
    ('XChangeProperty', [C.c_void_p, C.c_ulong, C.c_ulong, C.c_ulong, C.c_int, C.c_int, C.c_void_p, C.c_int], C.c_int),
    ('XSetWindowBackground', [C.c_void_p, C.c_ulong, C.c_ulong], C.c_int), ('XClearWindow', [C.c_void_p, C.c_ulong], C.c_int),
    ('XRaiseWindow', [C.c_void_p, C.c_ulong], C.c_int), ('XDestroyWindow', [C.c_void_p, C.c_ulong], C.c_int),
    ('XIconifyWindow', [C.c_void_p, C.c_ulong, C.c_int], C.c_int)]:
    function = getattr(lib, name); function.argtypes = arguments; function.restype = result

window = lib.XCreateSimpleWindow(desktop.display, desktop.root, 80, 80, 640, 360, 0, 0, 0x0a14dc)
lib.XStoreName(desktop.display, window, b'CamCord isolated fixture')
pid = C.c_ulong(os.getpid())
lib.XChangeProperty(desktop.display, window, desktop.atom('_NET_WM_PID'), desktop.atom('CARDINAL'), 32, 0, C.byref(pid), 1)
lib.XMapWindow(desktop.display, window); lib.XSync(desktop.display, False)
print(f'window:{window}:{os.getpid()}', flush=True)
cover = None
try:
    for line in sys.stdin:
        command = line.strip()
        if command == 'green':
            lib.XSetWindowBackground(desktop.display, window, 0x0adc14); lib.XClearWindow(desktop.display, window)
        elif command == 'cover':
            cover = lib.XCreateSimpleWindow(desktop.display, desktop.root, 65, 50, 700, 420, 0, 0, 0xdc1414)
            lib.XMapWindow(desktop.display, cover); lib.XRaiseWindow(desktop.display, cover)
        elif command == 'minimize': lib.XIconifyWindow(desktop.display, window, 0)
        elif command == 'close': lib.XDestroyWindow(desktop.display, window); window = None
        elif command == 'quit': break
        lib.XSync(desktop.display, False)
        print('ok', flush=True)
finally:
    if window: lib.XDestroyWindow(desktop.display, window)
    if cover: lib.XDestroyWindow(desktop.display, cover)
    desktop.close()
