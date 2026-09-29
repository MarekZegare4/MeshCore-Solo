# Settings

Settings are saved as you change them and kept through reboots and updates.
On joystick devices they're folding sections: **Enter** on a section opens
it, **Left / Right** changes a value, **Back** leaves. Only the rows your
board supports are shown.

| Section | What's there |
| ------- | ------------ |
| **Display** | Brightness, auto-off, lock screen and [PIN](./lock.md), battery as icon / % / volts, clock format and seconds, wake on message; on e-ink also rotation and full refresh |
| **Sound** | Buzzer on / off / auto (quiet while the app is connected), volume, quiet hours, the melody for messages, channels and adverts |
| **Home Pages** | Order of the home pages, and which are shown |
| **Radio** | TX power, preset, frequency, SF / BW / CR, saved presets, Auto pwr, the scope list |
| **System** | Device name, time zone, low-battery shutdown, GPS power saving, units, reboot |
| **Keyboard** | ABC or T9 layout, the two scripts, CardKB mode |
| **Contacts** | Show all or favourites only (DMs, channels, rooms), favourites on top, contact expiry and prune |
| **Messages** | Automatic resend of direct messages, the ten quick messages |

A few notes:

- **Auto pwr** lowers the TX power on strong links and raises it back on weak
  ones; the power set above is the ceiling.
- **Scope** is a list of named regions (plus `*`, no region). Messages are
  tagged with a scope so repeaters can tell communities on the same frequency
  apart. The default one is used for direct messages and repeating, and each
  channel picks its own (see [Messages](./messages.md#channels)). Anyone who
  types the same name gets the same scope; it isn't encryption.
- **Low battery** shuts the device down at the voltage you choose, which is
  also 0 % on the battery indicator.

> [!NOTE]
> **Wio Tracker L2:** one list of pages:
>
> - **Device**: Display (including the lock and **Tap to wake**), Home apps,
>   Power (battery, GPS), Sound (with the melody editor), Keyboard, Messages
>   & contacts.
> - **Connections**: Radio, Bluetooth, WiFi, GPS.
> - **Map & data**: Map (trail, live sharing and arrival alert options),
>   Storage.
> - **System**: Name, Time, Units, Firmware update, About; Reboot and Power off.
