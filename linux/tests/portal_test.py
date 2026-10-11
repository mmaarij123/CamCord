#!/usr/bin/env python3
"""GIO wire-level portal tests on an isolated D-Bus; never a real desktop grant."""
import os
import subprocess
import sys
import unittest
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))
from capture_helper import gst_modules, Portal
Gst, GLib, Gio = gst_modules()

XML = '''<node><interface name="org.freedesktop.portal.ScreenCast">
<method name="CreateSession"><arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method>
<method name="SelectSources"><arg type="o" direction="in"/><arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method>
<method name="Start"><arg type="o" direction="in"/><arg type="s" direction="in"/><arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method>
<method name="OpenPipeWireRemote"><arg type="o" direction="in"/><arg type="a{sv}" direction="in"/><arg type="h" direction="out"/></method>
</interface></node>'''
CLOSE = '<node><interface name="org.freedesktop.portal.Session"><method name="Close"/></interface></node>'
REQUEST = '<node><interface name="org.freedesktop.portal.Request"><method name="Close"/><signal name="Response"><arg type="u"/><arg type="a{sv}"/></signal></interface></node>'


def service():
    bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    loop = GLib.MainLoop(); sessions = {}
    def close(connection, sender, object_path, interface, method, parameters, invocation, *user_data):
        invocation.return_value(None)
    def call(connection, sender, object_path, interface, method, parameters, invocation, *user_data):
        args = parameters.unpack(); options = args[-1]
        if method == 'OpenPipeWireRemote':
            read, write = os.pipe(); os.write(write, b'portal-test'); os.close(write)
            descriptors = Gio.UnixFDList.new(); index = descriptors.append(read); os.close(read)
            invocation.return_value_with_unix_fd_list(GLib.Variant('(h)', (index,)), descriptors)
            return
        request = '/org/freedesktop/portal/desktop/request/test/' + options['handle_token']
        bus.register_object(request, Gio.DBusNodeInfo.new_for_xml(REQUEST).interfaces[0], close, None, None)
        code = 1 if options.get('test-deny') else 0
        data = {}
        if method == 'CreateSession':
            session = '/org/freedesktop/portal/desktop/session/test/' + options['session_handle_token']
            sessions[session] = {}
            bus.register_object(session, Gio.DBusNodeInfo.new_for_xml(CLOSE).interfaces[0], close, None, None)
            data['session_handle'] = GLib.Variant('s', session)
        elif method == 'SelectSources':
            assert options['multiple'] is False and options['cursor_mode'] == 1
            assert options['types'] in (1, 2)
            sessions[args[0]]['kind'] = options['types']
        elif method == 'Start':
            kind = sessions[args[0]]['kind']
            data['streams'] = GLib.Variant('a(ua{sv})', [(17, {'source_type': GLib.Variant('u', kind), 'pipewire-serial': GLib.Variant('t', 4294967000)})])
        invocation.return_value(GLib.Variant('(o)', (request,)))
        def respond():
            bus.emit_signal(sender, request, 'org.freedesktop.portal.Request', 'Response', GLib.Variant('(ua{sv})', (code, data)))
            return False
        GLib.timeout_add(50, respond)
    bus.register_object('/org/freedesktop/portal/desktop', Gio.DBusNodeInfo.new_for_xml(XML).interfaces[0], call, None, None)
    Gio.bus_own_name_on_connection(bus, 'org.freedesktop.portal.Desktop', Gio.BusNameOwnerFlags.NONE,
                                   lambda *_: print('READY', flush=True), None)
    loop.run()


class PortalWireTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.service = subprocess.Popen([sys.executable, __file__, '--service'], stdout=subprocess.PIPE, text=True)
        if cls.service.stdout.readline().strip() != 'READY': raise RuntimeError('Fake portal did not start')
    @classmethod
    def tearDownClass(cls):
        cls.service.terminate(); cls.service.wait(timeout=5)
    def test_screen_and_window_cursor_hidden_fd_and_cancel(self):
        for kind in (1, 2):
            portal = Portal(GLib, Gio)
            try:
                result = portal.request('CreateSession', '(a{sv})', (), {'session_handle_token': GLib.Variant('s', 'test_' + str(kind))})
                portal.session = result['session_handle']
                portal.request('SelectSources', '(oa{sv})', (portal.session,), {'types': GLib.Variant('u', kind),
                               'multiple': GLib.Variant('b', False), 'cursor_mode': GLib.Variant('u', 1)})
                result = portal.request('Start', '(osa{sv})', (portal.session, ''), {})
                self.assertEqual(result['streams'][0][1]['source_type'], kind)
                self.assertEqual(result['streams'][0][1]['pipewire-serial'], 4294967000)
                reply, descriptors = portal.bus.call_with_unix_fd_list_sync(portal.destination, portal.object, portal.interface,
                    'OpenPipeWireRemote', GLib.Variant('(oa{sv})', (portal.session, {})), GLib.VariantType.new('(h)'),
                    Gio.DBusCallFlags.NONE, 30000, None, None)
                descriptor = descriptors.get(reply.unpack()[0])
                try: self.assertEqual(os.read(descriptor, 32), b'portal-test')
                finally: os.close(descriptor)
                source = Gst.ElementFactory.make('pipewiresrc')
                self.assertIsNotNone(source)
                if source.find_property('target-object'):
                    source.set_property('target-object', '4294967000')
                    self.assertEqual(source.get_property('target-object'), '4294967000')
            finally: portal.close()
        with self.assertRaisesRegex(RuntimeError, 'cancelled or denied'):
            Portal(GLib, Gio).request('CreateSession', '(a{sv})', (), {'session_handle_token': GLib.Variant('s', 'denied'), 'test-deny': GLib.Variant('b', True)})


if __name__ == '__main__':
    if '--service' in sys.argv: service()
    else: unittest.main()
