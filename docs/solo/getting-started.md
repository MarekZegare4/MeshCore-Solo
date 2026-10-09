# Getting started

## Controls

On joystick devices:

| Input | Does |
| ----- | ---- |
| **Up / Down** | Move through a list |
| **Left / Right** | Switch home pages; change the value of a setting |
| **Enter** | Open, select, confirm |
| **Hold Enter** | Options for the selected item (a message, contact, channel…) |
| **Back** | One step back; from a screen, back to the home screen |

Lists wrap around at both ends. Any key wakes a dark screen without acting on it.

Keyboard devices use the same controls, from the arrow keys (or their Fn
combinations), Enter and Esc. [Hardware](./hardware.md) has the key maps.

> [!NOTE]
> **Wio Tracker L2:** tap to open, hold for options, swipe sideways between
> pages. The top button turns the screen off and on. The side button goes
> back to the home screen; hold it and let go to mute or unmute the sound.
> Holding the side button while pressing the top one takes a screenshot.

## Home screen

The home screen is a row of pages. On joystick devices, **Left / Right** steps
through them (on Favourites it moves between the slots first). The default
order:

| Page | Shows |
| ---- | ----- |
| **Clock** | The time, the date and up to three fields of your choice; Enter opens the clock tools |
| **Favourites** | Six slots for the conversations you open most ([Contacts](./contacts.md#favourites)) |
| **Status** | Radio, GPS, battery and mesh at a glance; Enter opens the [Status screen](./tools.md#status) |
| **Bluetooth** | Bluetooth on / off and the phone: connected, or the pairing PIN |
| **Advert** | Send an advert now, and the auto-advert interval |
| **Settings** | All settings, then brightness and GPS on / off |
| **Map** | Your position, the trail, people sharing theirs and the target ([Navigation](./navigation.md#map)) |
| **Tools** | All tools, then the two you used last with their state |
| **Messages** | All messages, then the two latest conversations with their unread counts |
| **Shutdown** | Lock the screen, or hibernate (with the battery level) |

Most pages are **quick panels**: a few rows, the first of which opens the
full screen, the rest shortcuts. **Up / Down** picks a row and **Enter** acts
on it. A value row
(brightness, auto-advert) goes into editing: **Left / Right** change it while
you watch, **Enter** or **Back** keeps it.

A tool opened from the Tools page returns to the home screen on **Back**;
one opened from the full Tools list returns to that list.

Settings › Home Pages sets their order and hides the ones you don't use;
Settings and Messages are always shown.

> [!NOTE]
> **Wio Tracker L2:** pages you swipe between: favourite chats, the clock
> (where it starts), a minimap, then the apps, six to a page. Hold an app to
> arrange or hide them.
>
> Pull down from the status bar (or tap it) for the quick panel: Bluetooth,
> WiFi, GPS, sound, trail and live share switch at a tap, Advert sends an
> advert or sets the automatic one, Lock locks the screen, and the slider
> sets the brightness. Hold a tile for its settings. Swipe it up, or tap
> below it, to put it away.

## Alerts and the battery

A new message, a changed setting and similar notices drop in as a banner from
the top of the screen and slide away after a moment; on e-ink they just
appear and go. A message's lasts two seconds. Typing on the keyboard isn't
interrupted.

Settings › Display › Message alert picks how a new message shows: **Normal**
(the banner), **Compact** (an envelope and a count in the top-right corner,
blinking for a moment, covering nothing else) or **Off** (no alert; the sound
and waking the screen still follow their own settings).

> [!NOTE]
> **Wio Tracker L2:** Settings › Display › **Message banner** turns the banner
> on or off; tapping it opens the conversation.

The battery sits at the top right, as an icon, a percentage or the voltage
(Settings › Display). A small bolt beside it means external power is
connected over USB.

> [!NOTE]
> Boards without a way to sense USB power (the Heltec V3, V4 and the
> Cardputer ADV) don't show the bolt.

## Connecting the phone app

Every build serves the MeshCore app over **Bluetooth and USB**, one at a time:
while Bluetooth is connected, USB is ignored. To use USB, disconnect Bluetooth
first or turn it off on the device.

The Bluetooth pairing PIN is shown on the Bluetooth home page until the phone
is paired. Switching Bluetooth off keeps it off after a restart or
hibernation, until you switch it on again.

> [!NOTE]
> **Wio Tracker L2:** the PIN is under Settings › Bluetooth.

Everything you do on the device and in the app stays in sync: contacts,
channels and messages are the same data.

## Updating

Download the new file from the [releases page](https://github.com/MarekZegare4/MeshCore-Solo/releases)
and flash it the way you did the first time (see the [main README](../../README.md#flashing)).
Settings, contacts and messages are kept.

> [!NOTE]
> **Wio Tracker L2:** Settings › Firmware update checks GitHub for a newer
> release and installs it over WiFi. Set up a network under Settings › WiFi
> first.
