/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2024 LibrePods Contributors
 *
 * LibrePods Preferences Window
 */

import Gio from 'gi://Gio';
import Gtk from 'gi://Gtk';
import Adw from 'gi://Adw';
import GObject from 'gi://GObject';

import {ExtensionPreferences} from 'resource:///org/gnome/Shell/Extensions/js/extensions/prefs.js';

/* D-Bus interface */
const AirPodsInterface = `
<node>
  <interface name="org.librepods.AirPods1">
    <property name="Connected" type="b" access="read"/>
    <property name="DeviceName" type="s" access="read"/>
    <property name="ConversationalAwareness" type="b" access="read"/>
    <property name="AdaptiveNoiseLevel" type="i" access="read"/>
    <property name="LeftInEar" type="b" access="read"/>
    <property name="RightInEar" type="b" access="read"/>
    <method name="SetConversationalAwareness">
      <arg type="b" name="enabled" direction="in"/>
    </method>
    <method name="SetAdaptiveNoiseLevel">
      <arg type="i" name="level" direction="in"/>
    </method>
  </interface>
</node>
`;

const AirPodsProxy = Gio.DBusProxy.makeProxyWrapper(AirPodsInterface);

export default class LibrePodsPreferences extends ExtensionPreferences {
    fillPreferencesWindow(window) {
        const page = new Adw.PreferencesPage({
            title: 'LibrePods',
            icon_name: 'audio-headphones-symbolic',
        });
        window.add(page);

        /* Device status group */
        const statusGroup = new Adw.PreferencesGroup({
            title: 'Device Status',
            description: 'Current AirPods connection status',
        });
        page.add(statusGroup);

        this._statusRow = new Adw.ActionRow({
            title: 'Connection',
            subtitle: 'Checking...',
        });
        statusGroup.add(this._statusRow);

        this._earDetectionRow = new Adw.ActionRow({
            title: 'Ear Detection',
            subtitle: 'Unknown',
        });
        statusGroup.add(this._earDetectionRow);

        /* Features group */
        const featuresGroup = new Adw.PreferencesGroup({
            title: 'Features',
            description: 'AirPods advanced features',
        });
        page.add(featuresGroup);

        /* Conversational Awareness toggle */
        this._caSwitch = new Gtk.Switch({
            valign: Gtk.Align.CENTER,
        });

        this._caRow = new Adw.ActionRow({
            title: 'Conversational Awareness',
            subtitle: 'Lower volume when speaking',
        });
        this._caRow.add_suffix(this._caSwitch);
        this._caRow.activatable_widget = this._caSwitch;
        featuresGroup.add(this._caRow);

        /* Adaptive Noise Level slider */
        this._adaptiveScale = new Gtk.Scale({
            orientation: Gtk.Orientation.HORIZONTAL,
            adjustment: new Gtk.Adjustment({
                lower: 0,
                upper: 100,
                step_increment: 1,
                page_increment: 10,
            }),
            draw_value: true,
            value_pos: Gtk.PositionType.RIGHT,
            hexpand: true,
            width_request: 200,
        });

        this._adaptiveRow = new Adw.ActionRow({
            title: 'Adaptive Noise Level',
            subtitle: 'Adjust transparency level',
        });
        this._adaptiveRow.add_suffix(this._adaptiveScale);
        featuresGroup.add(this._adaptiveRow);

        /* About group */
        const aboutGroup = new Adw.PreferencesGroup({
            title: 'About',
        });
        page.add(aboutGroup);

        const aboutRow = new Adw.ActionRow({
            title: 'LibrePods',
            subtitle: 'AirPods integration for GNOME',
        });
        aboutGroup.add(aboutRow);

        const versionRow = new Adw.ActionRow({
            title: 'Version',
            subtitle: '0.1.0',
        });
        aboutGroup.add(versionRow);

        /* Connect to daemon */
        this._connectProxy();

        /* Connect UI signals */
        this._caSwitch.connect('state-set', (widget, state) => {
            if (this._proxy && this._proxy.Connected) {
                this._proxy.SetConversationalAwarenessRemote(state, () => {});
            }
            return false;
        });

        this._adaptiveScale.connect('value-changed', () => {
            if (this._proxy && this._proxy.Connected) {
                const value = Math.round(this._adaptiveScale.get_value());
                this._proxy.SetAdaptiveNoiseLevelRemote(value, () => {});
            }
        });
    }

    _connectProxy() {
        try {
            this._proxy = new AirPodsProxy(
                Gio.DBus.session,
                'org.librepods.Daemon',
                '/org/librepods/AirPods',
                (proxy, error) => {
                    if (error) {
                        this._statusRow.subtitle = 'Daemon not running';
                        this._setSensitive(false);
                        return;
                    }

                    this._onProxyReady();
                }
            );
        } catch (e) {
            this._statusRow.subtitle = 'Error connecting to daemon';
            this._setSensitive(false);
        }
    }

    _onProxyReady() {
        this._proxy.connect('g-properties-changed', () => {
            this._updateState();
        });

        this._updateState();
    }

    _updateState() {
        if (!this._proxy)
            return;

        const connected = this._proxy.Connected;

        if (connected) {
            this._statusRow.subtitle = `Connected: ${this._proxy.DeviceName || 'AirPods'}`;

            const leftEar = this._proxy.LeftInEar ? 'In ear' : 'Out';
            const rightEar = this._proxy.RightInEar ? 'In ear' : 'Out';
            this._earDetectionRow.subtitle = `Left: ${leftEar}, Right: ${rightEar}`;

            this._caSwitch.active = this._proxy.ConversationalAwareness;
            this._adaptiveScale.set_value(this._proxy.AdaptiveNoiseLevel);

            this._setSensitive(true);
        } else {
            this._statusRow.subtitle = 'Disconnected';
            this._earDetectionRow.subtitle = 'No device connected';
            this._setSensitive(false);
        }
    }

    _setSensitive(sensitive) {
        this._caRow.sensitive = sensitive;
        this._adaptiveRow.sensitive = sensitive;
    }
}
