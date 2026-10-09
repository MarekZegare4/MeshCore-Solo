# Settings

Settings are saved as you change them and kept through reboots and updates.
On joystick devices they're folding sections: **Enter** on a section opens
it, **Left / Right** changes a value, **Back** leaves. Only the rows your
board supports are shown.

| Section | What's there |
| ------- | ------------ |
| **Display** | Brightness, auto-off, wake on message, the message alert (banner, corner envelope or off), lock screen, the lock clock's look and the [PIN](./lock.md), battery as icon / % / volts, time zone, clock format and seconds; on e-ink also rotation and full refresh |
| **Sound** | Buzzer on / off / auto (quiet while the app is connected), volume, quiet hours, the melody for messages, channels and adverts |
| **Home Pages** | Order of the home pages, and which are shown |
| **Radio** | TX power, preset, frequency, SF / BW / CR, saved presets, Auto pwr, the scope list, then the advanced options (below) |
| **System** | Device name, low-battery shutdown, GPS power saving, units, the battery curve, reboot |
| **Keyboard** | ABC or T9 layout, the two scripts, CardKB mode |
| **Contacts** | Show all or favourites only (DMs, channels, rooms), favourites on top, contact expiry and prune, which heard nodes are added |
| **Privacy** | Whether your adverts carry your position, and who may ask for your status, location and sensor readings |
| **Messages** | Automatic resend of direct messages, the ten quick messages |

A few notes:

- **Auto pwr** lowers the TX power on strong links and raises it back on weak
  ones; the power set above is the ceiling.
- **Scope** is a list of named regions (plus `*`, no region). Messages are
  tagged with a scope so repeaters can tell communities on the same frequency
  apart. The default one is used for direct messages and repeating, and a channel
  follows it unless it picks its own (see [Messages](./messages.md#channels)). Anyone who
  types the same name gets the same scope; it isn't encryption.
- **Low battery** shuts the device down at the voltage you choose, which is
  also 0 % on the battery indicator.
- **Batt curve** sets the cell voltage at 0, 10 … 100 % for a battery that
  doesn't follow the usual LiPo curve: **Left / Right** move a point by
  10 mV, **Reset** goes back to LiPo. The current voltage is shown, so a point
  can be set against what the battery reads.

### Adding contacts

A node you hear is added to your contacts according to **Auto-add**:

- **All**: every node heard.
- **By type**: only the types switched on below it (companions, repeaters,
  rooms, sensors). With none on, nodes are added only by hand (Nearby ›
  **Add contact**).

**Up to** limits how far away an added node may be: **Direct** (heard with
no repeater) or a number of hops. **Replace old** makes room when the list is
full by dropping the oldest contact that isn't a favourite; without it, new
nodes aren't added until you make room.

### Privacy

- **Advert pos** (Position in adverts): your adverts carry your position,
  including the automatic ones. Off, nodes see your name but not where you
  are. Live sharing and `{loc}` in a message still send it, because you send
  those on purpose.
- **Ask status / loc / sens**: who may request your telemetry: **Nobody**,
  **Allowed** (the contacts you gave that permission, for now in the phone
  app) or **Everyone**. Status is the battery; a request for location or
  sensors is answered only where status is allowed too.

### Advanced radio

At the end of **Radio**. The defaults suit almost everyone; change them only
if your mesh agrees on it.

| Setting | Does |
| --- | --- |
| **Hash size** | Bytes per repeater in a message's path: 1 (the default) fits the most hops, 2 or 3 tell repeaters apart in a big mesh |
| **Double ACK** | Sends each acknowledgement twice, for a lossy link |
| **LBT (CAD)** | Listen before talk: waits for a quiet channel before sending |
| **Interf.** | Treats the channel as busy when the signal is this far above the noise floor |
| **RX boost** | The radio's boosted receive gain: hears weaker signals for a little more power (SX126x radios) |
| **RX delay** | How long a repeated packet waits, so the best path wins; off by default |
| **Airtime** | The pause after sending, as a multiple of the time on air |

> [!NOTE]
> **Wio Tracker L2:** one list of pages:
>
> - **Device**: Display (including the lock and **Tap to wake**), Home apps,
>   Power (battery, GPS, and the **Battery curve** as a chart whose points you
>   drag), Sound (with the melody editor), Keyboard, Messages
>   & contacts.
> - **Connections**: Radio (with **Advanced** at the bottom), Privacy,
>   Bluetooth, WiFi, GPS. Adding contacts is in Messages & contacts.
> - **Map & data**: Map (trail, live sharing and arrival alert options),
>   Storage.
> - **System**: Name, Time, Units, Firmware update, About; Reboot and Power off.
