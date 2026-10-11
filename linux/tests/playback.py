#!/usr/bin/env python3
# Only used with isolated null sinks created by integration.sh.
import os
import signal
import sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))
from capture_helper import gst_modules
Gst, GLib, _ = gst_modules()
pipeline = Gst.parse_launch('audiotestsrc is-live=true wave=sine volume=0.25 freq=440 ! audioconvert ! audioresample ! pulsesink device=camcord_test')
loop = GLib.MainLoop()
def start(): pipeline.set_state(Gst.State.PLAYING); return False
def stop(*_): loop.quit()
signal.signal(signal.SIGTERM, stop)
GLib.timeout_add(600, start)
GLib.timeout_add(4000, lambda: (loop.quit(), False)[1])
try: loop.run()
finally: pipeline.set_state(Gst.State.NULL)
