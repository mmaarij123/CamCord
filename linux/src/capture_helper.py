#!/usr/bin/env python3
"""Unencoded capture only: X11 named window pixmaps or consented portal/PipeWire.

stdout is JSON for list commands and tightly packed BGRA frames for capture.
No shell commands, screenshots on disk, root permissions or persisted handles.
"""
import argparse
import ctypes as C
import json
import os
import re
import select
import signal
import sys
import threading
import time
import uuid


def gst_modules():
    import gi
    gi.require_version('Gst', '1.0')
    from gi.repository import Gst, GLib, Gio
    Gst.init(None)
    return Gst, GLib, Gio


class Attributes(C.Structure):
    _fields_ = [('x', C.c_int), ('y', C.c_int), ('width', C.c_int), ('height', C.c_int),
                ('border_width', C.c_int), ('depth', C.c_int), ('visual', C.c_void_p),
                ('root', C.c_ulong), ('class_', C.c_int), ('bit_gravity', C.c_int),
                ('win_gravity', C.c_int), ('backing_store', C.c_int),
                ('backing_planes', C.c_ulong), ('backing_pixel', C.c_ulong),
                ('save_under', C.c_int), ('colormap', C.c_ulong), ('map_installed', C.c_int),
                ('map_state', C.c_int), ('all_event_masks', C.c_long), ('your_event_mask', C.c_long),
                ('do_not_propagate_mask', C.c_long), ('override_redirect', C.c_int), ('screen', C.c_void_p)]


class XImage(C.Structure):
    _fields_ = [('width', C.c_int), ('height', C.c_int), ('xoffset', C.c_int), ('format', C.c_int),
                ('data', C.c_void_p), ('byte_order', C.c_int), ('bitmap_unit', C.c_int),
                ('bitmap_bit_order', C.c_int), ('bitmap_pad', C.c_int), ('depth', C.c_int),
                ('bytes_per_line', C.c_int), ('bits_per_pixel', C.c_int),
                ('red_mask', C.c_ulong), ('green_mask', C.c_ulong), ('blue_mask', C.c_ulong)]


class Visual(C.Structure):
    _fields_ = [('ext_data', C.c_void_p), ('visual_id', C.c_ulong), ('class_', C.c_int),
                ('red_mask', C.c_ulong), ('green_mask', C.c_ulong), ('blue_mask', C.c_ulong),
                ('bits_per_rgb', C.c_int), ('map_entries', C.c_int)]


class Monitor(C.Structure):
    _fields_ = [('name', C.c_ulong), ('primary', C.c_int), ('automatic', C.c_int), ('noutput', C.c_int),
                ('x', C.c_int), ('y', C.c_int), ('width', C.c_int), ('height', C.c_int),
                ('mwidth', C.c_int), ('mheight', C.c_int), ('outputs', C.c_void_p)]


class X11:
    def __init__(self):
        self.lib = C.CDLL('libX11.so.6')
        self.lib.XOpenDisplay.argtypes = [C.c_char_p]; self.lib.XOpenDisplay.restype = C.c_void_p
        self.display = self.lib.XOpenDisplay(None)
        if not self.display:
            raise RuntimeError('No X11 desktop is available. Use a graphical login session.')
        for name, args, result in [
            ('XDefaultRootWindow', [C.c_void_p], C.c_ulong),
            ('XInternAtom', [C.c_void_p, C.c_char_p, C.c_int], C.c_ulong),
            ('XGetWindowAttributes', [C.c_void_p, C.c_ulong, C.POINTER(Attributes)], C.c_int),
            ('XGetWindowProperty', [C.c_void_p, C.c_ulong, C.c_ulong, C.c_long, C.c_long, C.c_int, C.c_ulong,
                                   C.POINTER(C.c_ulong), C.POINTER(C.c_int), C.POINTER(C.c_ulong),
                                   C.POINTER(C.c_ulong), C.POINTER(C.c_void_p)], C.c_int),
            ('XGetImage', [C.c_void_p, C.c_ulong, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_ulong, C.c_int], C.POINTER(XImage)),
            ('XDestroyImage', [C.POINTER(XImage)], C.c_int),
            ('XFreePixmap', [C.c_void_p, C.c_ulong], C.c_int),
            ('XSync', [C.c_void_p, C.c_int], C.c_int),
            ('XFree', [C.c_void_p], C.c_int), ('XCloseDisplay', [C.c_void_p], C.c_int)]:
            fn = getattr(self.lib, name); fn.argtypes = args; fn.restype = result
        self.error = False
        self.error_callback = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p)(self.on_error)
        self.lib.XSetErrorHandler.argtypes = [C.c_void_p]
        self.lib.XSetErrorHandler(C.cast(self.error_callback, C.c_void_p))
        self.root = self.lib.XDefaultRootWindow(self.display)

    def on_error(self, *_):
        self.error = True
        return 0

    def atom(self, name):
        return self.lib.XInternAtom(self.display, name.encode(), False)

    def property(self, window, name):
        actual = C.c_ulong(); form = C.c_int(); count = C.c_ulong(); after = C.c_ulong(); data = C.c_void_p()
        self.error = False
        status = self.lib.XGetWindowProperty(self.display, window, self.atom(name), 0, 8192, False, 0,
                                            C.byref(actual), C.byref(form), C.byref(count), C.byref(after), C.byref(data))
        self.lib.XSync(self.display, False)
        try:
            if status or self.error or not data.value:
                return []
            if form.value == 32:
                return list(C.cast(data, C.POINTER(C.c_ulong))[:count.value])
            if form.value == 8:
                return C.string_at(data, count.value).decode('utf8', 'replace').split('\0')[0]
            return []
        finally:
            if data.value: self.lib.XFree(data)

    def attributes(self, window):
        value = Attributes(); self.error = False
        status = self.lib.XGetWindowAttributes(self.display, window, C.byref(value))
        self.lib.XSync(self.display, False)
        if not status or self.error: raise RuntimeError('The selected window is no longer available.')
        return value

    def monitors(self):
        result = []
        try:
            randr = C.CDLL('libXrandr.so.2')
            randr.XRRGetMonitors.argtypes = [C.c_void_p, C.c_ulong, C.c_int, C.POINTER(C.c_int)]
            randr.XRRGetMonitors.restype = C.POINTER(Monitor)
            randr.XRRFreeMonitors.argtypes = [C.POINTER(Monitor)]
            count = C.c_int(); monitors = randr.XRRGetMonitors(self.display, self.root, True, C.byref(count))
            try:
                for index in range(min(count.value, 64)):
                    item = monitors[index]
                    result.append((item.x, item.y, item.width, item.height, bool(item.primary)))
            finally:
                if monitors: randr.XRRFreeMonitors(monitors)
        except (OSError, AttributeError): pass
        if not result:
            root = self.attributes(self.root)
            result.append((0, 0, root.width, root.height, True))
        return sorted(result, key=lambda item: not item[4])

    def identity(self, window):
        pid = self.property(window, '_NET_WM_PID')
        return int(pid[0]) if pid else 0

    def sources(self, exclude):
        result = []
        for index, (x, y, w, h, primary) in enumerate(self.monitors()):
            result.append(dict(kind='display', id=f'display:{x}:{y}:{w}:{h}',
                               label=f'{"Primary display" if primary else "Display " + str(index + 1)} · {w} × {h}',
                               primary=primary, width=w, height=h))
        hidden = self.atom('_NET_WM_STATE_HIDDEN')
        dock = self.atom('_NET_WM_WINDOW_TYPE_DOCK')
        for window in self.property(self.root, '_NET_CLIENT_LIST'):
            try:
                attrs = self.attributes(window); pid = self.identity(window)
                title = self.property(window, '_NET_WM_NAME') or self.property(window, 'WM_NAME')
                if (attrs.map_state != 2 or attrs.width < 16 or attrs.height < 16 or not title or pid == exclude
                        or hidden in self.property(window, '_NET_WM_STATE') or dock in self.property(window, '_NET_WM_WINDOW_TYPE')):
                    continue
                result.append(dict(kind='window', id=f'window:{window}:{pid}', label=str(title)[:1024],
                                   primary=False, width=attrs.width, height=attrs.height))
            except RuntimeError: continue
        return result

    def close(self):
        self.lib.XCloseDisplay(self.display)


def command_reader(stop, paused, dispatch=None):
    for line in sys.stdin:
        command = line.strip()
        if command == 'pause': paused.set()
        elif command == 'resume': paused.clear()
        elif command == 'quit': stop.set()
        if dispatch: dispatch(command)
        if stop.is_set(): break
    stop.set()


def capture_x11(args):
    Gst, GLib, _ = gst_modules()
    desktop = X11(); stop = threading.Event(); paused = threading.Event()
    signal.signal(signal.SIGTERM, lambda *_: stop.set())
    signal.signal(signal.SIGINT, lambda *_: stop.set())
    threading.Thread(target=command_reader, args=(stop, paused), daemon=True).start()
    composite = None; window = None; owned_redirect = False
    pipeline = Gst.parse_launch(f'appsrc name=input is-live=true format=time block=true max-bytes={args.width * args.height * 8} '
                               f'! videoconvert ! videoscale add-borders=true ! video/x-raw,format=BGRA,width={args.width},height={args.height},pixel-aspect-ratio=1/1 '
                               '! fdsink fd=1 sync=false')
    source = pipeline.get_by_name('input')
    try:
        if args.kind == 'display':
            match = re.fullmatch(r'display:(-?\d+):(-?\d+):(\d+):(\d+)', args.id)
            if not match: raise RuntimeError('Select a valid display.')
            x, y, w, h = map(int, match.groups())
            if not any((x, y, w, h) == item[:4] for item in desktop.monitors()):
                raise RuntimeError('The selected display changed or disconnected.')
            drawable = desktop.root
        else:
            match = re.fullmatch(r'window:(\d+):(\d+)', args.id)
            if not match: raise RuntimeError('Select a valid window.')
            window, pid = map(int, match.groups())
            attrs = desktop.attributes(window)
            if desktop.identity(window) != pid or attrs.map_state != 2: raise RuntimeError('Restore or select the window again.')
            identity_class = desktop.property(window, 'WM_CLASS')
            composite = C.CDLL('libXcomposite.so.1')
            composite.XCompositeNameWindowPixmap.argtypes = [C.c_void_p, C.c_ulong]
            composite.XCompositeNameWindowPixmap.restype = C.c_ulong
            composite.XCompositeRedirectWindow.argtypes = [C.c_void_p, C.c_ulong, C.c_int]
            composite.XCompositeUnredirectWindow.argtypes = [C.c_void_p, C.c_ulong, C.c_int]
            x = y = 0
            # Prefer the compositor's existing redirect. Never change its ownership.
            desktop.error = False
            probe = composite.XCompositeNameWindowPixmap(desktop.display, window)
            desktop.lib.XSync(desktop.display, False)
            if probe and not desktop.error: desktop.lib.XFreePixmap(desktop.display, probe)
            else:
                desktop.error = False; composite.XCompositeRedirectWindow(desktop.display, window, 0)
                desktop.lib.XSync(desktop.display, False)
                if desktop.error: raise RuntimeError('This desktop compositor cannot capture the selected window.')
                owned_redirect = True
        pipeline.set_state(Gst.State.PLAYING)
        origin = time.monotonic_ns(); last_size = None
        while not stop.is_set():
            if paused.is_set(): time.sleep(0.02); continue
            began = time.monotonic()
            pixmap = 0
            if window:
                attrs = desktop.attributes(window)
                if (attrs.map_state != 2 or desktop.identity(window) != pid or desktop.property(window, 'WM_CLASS') != identity_class
                        or desktop.atom('_NET_WM_STATE_HIDDEN') in desktop.property(window, '_NET_WM_STATE')):
                    raise RuntimeError('The selected window closed, was minimized or changed identity.')
                w, h = attrs.width, attrs.height
                desktop.error = False; pixmap = composite.XCompositeNameWindowPixmap(desktop.display, window)
                desktop.lib.XSync(desktop.display, False)
                if not pixmap or desktop.error: raise RuntimeError('The selected window stopped providing pixels.')
                drawable = pixmap
            else:
                if not any((x, y, w, h) == item[:4] for item in desktop.monitors()): raise RuntimeError('The selected display changed or disconnected.')
            image = None
            try:
                if not (16 <= w <= 16384 and 16 <= h <= 16384): raise RuntimeError('Unsupported capture source dimensions.')
                desktop.error = False
                image = desktop.lib.XGetImage(desktop.display, drawable, x, y, w, h, C.c_ulong(-1), 2)
                desktop.lib.XSync(desktop.display, False)
                if not image or desktop.error: raise RuntimeError('The selected source is unavailable.')
                frame = image.contents
                masks = (frame.red_mask, frame.green_mask, frame.blue_mask)
                # XGetImage on a named pixmap has no Visual and can report zero
                # channel masks. Its owning window's Visual defines that layout.
                if window and masks == (0, 0, 0):
                    visual = C.cast(attrs.visual, C.POINTER(Visual)).contents
                    masks = (visual.red_mask, visual.green_mask, visual.blue_mask)
                if (frame.bits_per_pixel != 32 or frame.byte_order != 0 or frame.bytes_per_line != w * 4
                        or masks != (0xff0000, 0xff00, 0xff)):
                    raise RuntimeError('This X11 color format is not supported. Use a standard 24/32-bit desktop.')
                if last_size != (w, h):
                    source.set_property('caps', Gst.Caps.from_string(f'video/x-raw,format=BGRA,width={w},height={h},framerate={args.fps}/1'))
                    last_size = (w, h)
                data = C.string_at(frame.data, w * h * 4)
                buffer = Gst.Buffer.new_allocate(None, len(data), None); buffer.fill(0, data)
                buffer.pts = time.monotonic_ns() - origin; buffer.duration = Gst.SECOND // args.fps
                if source.emit('push-buffer', buffer) != Gst.FlowReturn.OK: raise RuntimeError('The capture stream stopped.')
            finally:
                if image: desktop.lib.XDestroyImage(image)
                if pixmap: desktop.lib.XFreePixmap(desktop.display, pixmap)
            time.sleep(max(0, 1 / args.fps - (time.monotonic() - began)))
    finally:
        pipeline.set_state(Gst.State.NULL)
        if owned_redirect and window:
            composite.XCompositeUnredirectWindow(desktop.display, window, 0); desktop.lib.XSync(desktop.display, False)
        desktop.close()


class Portal:
    def __init__(self, GLib, Gio):
        self.GLib = GLib; self.Gio = Gio
        self.bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        self.session = None
        self.destination = 'org.freedesktop.portal.Desktop'
        self.object = '/org/freedesktop/portal/desktop'
        self.interface = 'org.freedesktop.portal.ScreenCast'

    def request(self, method, signature, values, options):
        token = 'camcord_' + uuid.uuid4().hex
        options['handle_token'] = self.GLib.Variant('s', token)
        loop = self.GLib.MainLoop(); result = {}; request_path = [None]
        def response(_, sender, object_path, interface, name, parameters, *_):
            if object_path == request_path[0]:
                code, data = parameters.unpack(); result.update(code=code, data=data); loop.quit()
        subscription = self.bus.signal_subscribe(self.destination, 'org.freedesktop.portal.Request', 'Response',
                                                  None, None, self.Gio.DBusSignalFlags.NONE, response)
        timeout = None
        try:
            reply = self.bus.call_sync(self.destination, self.object, self.interface, method,
                                       self.GLib.Variant(signature, (*values, options)), None,
                                       self.Gio.DBusCallFlags.NONE, 30000, None)
            request_path[0] = reply.unpack()[0]
            timeout = self.GLib.timeout_add_seconds(150, lambda: (loop.quit(), False)[1])
            loop.run()
            if result.get('code') != 0:
                raise RuntimeError('Screen sharing was cancelled or denied by the desktop portal.')
            return result['data']
        finally:
            if timeout:
                try: self.GLib.source_remove(timeout)
                except Exception: pass
            self.bus.signal_unsubscribe(subscription)
            if request_path[0]:
                try: self.bus.call_sync(self.destination, request_path[0], 'org.freedesktop.portal.Request', 'Close', None, None,
                                        self.Gio.DBusCallFlags.NONE, 1000, None)
                except Exception: pass

    def close(self):
        if self.session:
            try: self.bus.call_sync(self.destination, self.session, 'org.freedesktop.portal.Session', 'Close', None, None,
                                   self.Gio.DBusCallFlags.NONE, 1000, None)
            except Exception: pass


def capture_portal(args):
    Gst, GLib, Gio = gst_modules(); portal = Portal(GLib, Gio)
    pipeline = None; remote = None
    try:
        data = portal.request('CreateSession', '(a{sv})', (), {'session_handle_token': GLib.Variant('s', 'camcord_' + uuid.uuid4().hex)})
        portal.session = data['session_handle']
        portal.request('SelectSources', '(oa{sv})', (portal.session,), {'types': GLib.Variant('u', 1 if args.kind == 'display' else 2),
                       'multiple': GLib.Variant('b', False), 'cursor_mode': GLib.Variant('u', 1)})
        data = portal.request('Start', '(osa{sv})', (portal.session, ''), {})
        streams = data.get('streams', [])
        if len(streams) != 1: raise RuntimeError('Choose exactly one screen or window in the system picker.')
        node = int(streams[0][0]); properties = streams[0][1]
        reply, descriptors = portal.bus.call_with_unix_fd_list_sync(portal.destination, portal.object, portal.interface,
            'OpenPipeWireRemote', GLib.Variant('(oa{sv})', (portal.session, {})), GLib.VariantType.new('(h)'),
            Gio.DBusCallFlags.NONE, 30000, None, None)
        remote = descriptors.get(reply.unpack()[0])
        pipeline = Gst.parse_launch(f'pipewiresrc name=input do-timestamp=true ! videoconvert ! videoscale add-borders=true '
                                   f'! video/x-raw,format=BGRA,width={args.width},height={args.height},pixel-aspect-ratio=1/1 '
                                   '! fdsink fd=1 sync=false')
        source = pipeline.get_by_name('input'); source.set_property('fd', remote)
        # Portal v6 supplies a stable PipeWire object serial; do not reconnect
        # to a recycled node ID after hotplug/suspend. Older portals use path.
        serial = properties.get('pipewire-serial')
        if serial is not None and source.find_property('target-object'):
            source.set_property('target-object', str(serial))
        elif serial is not None and source.find_property('stream-properties'):
            source.set_property('stream-properties', Gst.Structure.new_from_string(f'props,target.object=(string){serial}'))
        else: source.set_property('path', str(node))
        loop = GLib.MainLoop(); stop = threading.Event(); paused = threading.Event(); errors = []
        def command(value):
            def apply():
                if value == 'pause': pipeline.set_state(Gst.State.PAUSED)
                elif value == 'resume': pipeline.set_state(Gst.State.PLAYING)
                elif value == 'quit': loop.quit()
                return False
            GLib.idle_add(apply)
        threading.Thread(target=command_reader, args=(stop, paused, command), daemon=True).start()
        bus = pipeline.get_bus(); bus.add_signal_watch()
        def on_message(_, message):
            if message.type == Gst.MessageType.ERROR:
                error, _ = message.parse_error(); errors.append(str(error)); loop.quit()
            elif message.type == Gst.MessageType.EOS: loop.quit()
        bus.connect('message', on_message)
        def terminated(*_): stop.set(); GLib.idle_add(lambda: (loop.quit(), False)[1])
        signal.signal(signal.SIGTERM, terminated); signal.signal(signal.SIGINT, terminated)
        portal.bus.signal_subscribe(portal.destination, 'org.freedesktop.portal.Session', 'Closed', portal.session,
                                   None, Gio.DBusSignalFlags.NONE, lambda *_: loop.quit())
        pipeline.set_state(Gst.State.PLAYING); loop.run()
        if errors: raise RuntimeError(errors[0])
    finally:
        if pipeline: pipeline.set_state(Gst.State.NULL)
        if remote is not None: os.close(remote)
        portal.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('command', choices=['list', 'capture'])
    parser.add_argument('--exclude-pid', type=int, default=-1)
    parser.add_argument('--portal', action='store_true')
    parser.add_argument('--kind', choices=['display', 'window'])
    parser.add_argument('--id', default='')
    parser.add_argument('--width', type=int, default=1920)
    parser.add_argument('--height', type=int, default=1080)
    parser.add_argument('--fps', type=int, default=60)
    args = parser.parse_args()
    if args.command == 'list':
        desktop = X11()
        try: print(json.dumps(desktop.sources(args.exclude_pid), ensure_ascii=False))
        finally: desktop.close()
    else:
        if not args.kind or args.width not in [854, 1280, 1920] or args.height not in [480, 720, 1080] or args.fps not in [15, 30, 60, 120]:
            raise RuntimeError('Invalid recording settings.')
        (capture_portal if args.portal else capture_x11)(args)


if __name__ == '__main__':
    try: main()
    except (Exception, BrokenPipeError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(2)
