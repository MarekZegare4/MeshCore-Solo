# Settings

Settings are saved as you change them and kept through reboots and updates.
On joystick devices they're folding sections: **Enter** on a section opens
it, **Left / Right** changes a value, **Back** leaves. Only the rows your
board supports are shown.

| Section | What's there |
| ------- | ------------ |
| **Display** | Brightness, auto-off, wake on message, the message alert (banner, corner envelope or off), lock screen, the lock clock's look and the [PIN](./lock.md), battery as icon / % / volts, the signal bars, the clock set by hand, time zone and summer time, clock format and seconds, time from GPS; on e-ink also rotation and full refresh, and on a 4.26" panel the text and icon size |
| **Sound** | Buzzer on / off / auto (quiet while the app is connected), volume, quiet hours, the melody for messages, channels and adverts |
| **Home Pages** | Order of the home pages, and which are shown |
| **Radio** | TX power, preset, frequency, SF / BW / CR, saved presets, Auto pwr, Power save, the scope list, then the advanced options (below) |
| **System** | Device name, low-battery shutdown, GPS power saving, units, the battery curve, reboot, factory reset |
| **Keyboard** | ABC or T9 layout, the two scripts, CardKB mode |
| **Contacts** | Show all or favourites only (DMs, channels, rooms), favourites on top, the order of the channel list, contact expiry and prune, which heard nodes are added |
| **Privacy** | Whether your adverts carry your position and which position, who may ask for your status, location and sensor readings, the Bluetooth PIN |
| **Messages** | Automatic resend of direct messages, on the Wio Tracker L1 the messages kept on flash (below), the ten quick messages |

A few notes:

- **Auto pwr** lowers the TX power on strong links and raises it back on weak
  ones; the power set above is the ceiling.
- **Power save** lets the radio sleep between short listens. **On** hears
  every node and sleeps about a third of the time. **Max** sleeps about two
  thirds of the time at SF8 and below, but misses nodes on firmware older
  than companion v1.16. At SF9 and above it is the same as On. Power save
  is off while repeating, and SX1262 radios only. How it works and the
  tests behind it: [RX power save](./developer/rx-power-save.md).
- **Scope** is a list of named regions (plus `*`, no region). Messages are
  tagged with a scope so repeaters can tell communities on the same frequency
  apart. The default one is used for direct messages and repeating, and a channel
  follows it unless it picks its own (see [Messages](./messages.md#channels)). Anyone who
  types the same name gets the same scope; it isn't encryption.
- **Summer time** puts the clock an hour ahead and back again by itself, by
  the rule of your region: Europe (the last Sundays of March and October),
  North America, south-east Australia or New Zealand. The time zone stays
  the winter one, e.g. UTC+1 for Poland. The alarm and quiet hours follow it.
- **Clock** sets the date and time by hand, as local time: on joystick
  devices a field at a time (Left / Right between year, month, day, hour and
  minute, Up / Down to change it), on the L2 on wheels, as the alarm's (year, month, day, hour, minute), then
  **Set the clock**.
  Otherwise the clock comes from the GPS, the phone app or the network (L2
  WiFi). Putting it more than 5 minutes back asks first, because other
  nodes keep the newest time they had from this one: until the clock is past
  it again they ignore its adverts (a new name or position) and its logins
  to repeaters and room servers. Messages are not affected. The phone app
  may put the clock back by up to an hour (to correct a clock that ran
  ahead); further back is done here.
- **Time from GPS** off: a fix no longer sets the clock. For a GPS that gives
  a wrong time (spoofing), or to keep a clock set by hand; the position is
  still used.
- **Low battery** shuts the device down at the voltage you choose, which is
  also 0 % on the battery indicator.
- **Batt curve** sets the cell voltage at 0, 10 … 100 % for a battery that
  doesn't follow the usual LiPo curve: **Left / Right** move a point by
  10 mV, **Reset** goes back to LiPo. The current voltage is shown, so a point
  can be set against what the battery reads.
- **Keep messages** (Wio Tracker L1, Watch13, Heltec V3 / V4, Cardputer ADV) saves messages on the flash, across a
  power-off and past what fits in memory (see
  [How much is kept](./messages.md#how-much-is-kept)): **Off**, 500, 1000 or
  2500 in total. When the limit is reached the oldest go, in batches. **Off**
  stops saving at once and deletes what was saved at the next start, so turn it
  back on before then to keep it. **Delete history** deletes right away.
- **Delete after** (the same boards) removes saved messages older than 7, 30 or
  90 days, or **Never**. They go in batches too, so some stay a little longer,
  and nothing goes while the clock isn't set.
- **Delete history** (the same boards) deletes every message, saved and in
  memory, after asking.
- **Factory reset** erases contacts, channels, messages, settings and the
  node's keys, then restarts as a new node. It asks twice. On the Wio Tracker
  L2 the SD card is left as it is.

### Adding contacts

A node you hear is added to your contacts according to **Auto-add**:

- **All**: every node heard.
- **By type**: only the types switched on below it (companions, repeaters,
  rooms, sensors). With none on, nodes are added only by hand (Nodes ›
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
- **Precision**: how exactly adverts give your position: **Exact**, or
  rounded to a grid of about **100 m**, **500 m** or **1 km**. A fixed grid,
  not random noise, so many adverts from one place can't be averaged back
  to the exact spot.
- **Position**: where your adverts say you are while the GPS is off. **Enter**
  to type it in (`50.06143, 19.93658`), take it from a waypoint, or clear it.
  With the GPS on, the row shows **GPS** and each fix replaces it; turning the
  GPS off keeps the last fix. The phone app sets the same position.
- **Ask status / loc / sens**: who may request your telemetry: **Nobody**,
  **Allowed** (the contacts you gave that permission: **Telem** in a node's
  options in [Nodes](./contacts.md#nearby-nodes), or the phone app) or
  **Everyone**. Status is the battery; a request for location or
  sensors is answered only where status is allowed too. Status also lets
  others find a route to you (**Discover** in
  [Path and trace](./contacts.md#path-and-trace), or the phone app's path
  discovery).
- **BT PIN**: the six-digit Bluetooth pairing PIN. Leave it empty for a new
  random one at each start, shown on screen. A new PIN is used from the next
  start; the phone pairs again after that.

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
> **Wio Tracker L2:** one list of pages, the same pieces everywhere: a
> switch turns a thing on and off, a chevron opens its page, and a page with
> one group has no heading over it.
>
> - At the top: **Name** and **Radio** (with **Advanced** at the bottom).
> - **Connections**: Bluetooth, WiFi and GPS, their switches side by side.
>   A tap on a row opens its details: Bluetooth the **Pairing PIN**, WiFi the
>   saved networks (saving one checks its password and sets the clock), GPS the satellites and an **Options** tab with GPS power
>   saving and the **Fixed position**.
> - **Device**: Display (including the lock, **Tap to wake**, the **Theme**,
>   dark or light, which restarts the device, and the accent colour), Sound
>   (with the melody editor), Power (battery, and the **Battery curve** as a
>   chart whose points you drag), Time & units, Keyboard.
> - **Mesh & data**: Messages & contacts (adding contacts too; the node types
>   show only with **By type**), Map (trail, live sharing and arrival alert
>   options), Adverts & privacy (**Auto-advert** with the rest of what an
>   advert carries), Storage, [Coverage map](./coverage-map.md) (MatterMap
>   recording, voluntary, off by default).
> - **System**: Firmware update, About; then Reboot, Power off and Factory
>   reset.

## Console

The phone app's console and the console in the site's
[configurator](https://solo.marekzegarek.com/configure.html#console) change settings by
text. The commands are the ones upstream MeshCore's companion firmware has,
so its documentation applies:

| Command | Does |
| --- | --- |
| `get radio`, `set radio 869.525,250,11,5` | Frequency, bandwidth, SF and CR, applied at once |
| `get repeater.radio`, `set repeater.radio 869.525,62.5,8,8` | The repeater's own frequency, bandwidth, SF and CR (Repeater › Relay on: Custom; `set repeater.network Custom`), applied at once while it relays on them |
| `get repeater.scopes`, `set repeater.scopes alps,tatry`, `set repeater.scopes none` | The scopes the repeater relays besides the default one (Repeater › Extra scopes), by name from the scope list (Settings › Radio) |
| `get tx`, `set tx 14` | TX power in dBm |
| `get af` / `set af`, `get dutycycle` / `set dutycycle` | Airtime factor, or the same as a duty cycle in % |
| `rxdelay`, `int.thresh`, `cad`, `radio.rxgain`, `path.hash.mode`, `multi.acks` | The advanced radio options, with `get` / `set` |
| `txdelay`, `direct.txdelay` | How long a repeated packet waits, as a share of its airtime (0–2; 0.5 and 0.2 by default), with `get` / `set` |
| `agc.reset.interval` | Seconds between radio gain resets (in steps of 4, `0` off), with `get` / `set` |
| `get radio.fem.rxgain`, `set radio.fem.rxgain on` | Heltec V4 only: the extra receive amplifier in front of the radio, `on` / `off` |
| `get name`, `set name …` | The device name |
| `get pin`, `set pin 123456` | The Bluetooth PIN, `0` for a random one; used from the next start |
| `get tz.offset`, `set tz.offset 1` | Time zone, in hours from UTC (the winter one when summer time is on) |
| `clock`, `time 2026-11-14 12:30`, `time 1794655800` | The local date and time; set it as local time or as seconds since 1970 (UTC), back too |
| `get lat`, `set lat …`, `get lon`, `set lon …` | The fixed position |
| `ver`, `board` | Firmware version, hardware |
| `reboot`, `poweroff` (or `shutdown`) | Restart or turn off |

Every other setting has a key: `keys` lists them a page at a time (`keys 2`,
`keys 3` …). The repeater's are there too (`repeater`, `repeater.network`,
`repeater.skip.adverts`, `repeater.max.hops`, `repeater.yield`,
`repeater.min.snr`, `repeater.skip.dups`, `repeater.scope.only`), so a
one-button device can be set up as a repeater from the configurator.
The bot's too: `bot.ch1`, `bot.ch2`, `bot.room` and `bot.dm` switch each
target on, and each has `.commands`, `.actions`, `.allowed` (the commands it
answers, `set bot.dm.allowed ping,batt,help` or `none`), `.trigger` and
`.reply` (a text: `set bot.dm.reply Away, back at {time}`; nothing after the
key empties it); `bot.ch1.channel` / `bot.ch2.channel` and `bot.room.room`
pick where by name; `bot.dm.allow`, `bot.dm.position`, `bot.quiet.from`,
`bot.quiet.to`, `bot.actions.by` and `bot.gap` the rest. And the SOS's: `sos` (the top
button), `sos.countdown`, `sos.channels` (by name, `none` for none),
`sos.contact1` to `sos.contact3` (a contact's name, `none` to clear),
`sos.msg`, `sos.repeat`, `sos.advert`, `sos.beep` and `sos.ack`. The
alarm: `alarm` (on / off), `alarm.time` (`set alarm.time 6:45`) and
`alarm.days` (`mon,tue,wed,thu,fri`, `none` for once). On the L2,
coverage: `coverage`, `coverage.home` (`50.06143,19.93658` or `none`),
`coverage.home.radius` and `coverage.upload`. On the L1, the pins:
`gpio.1` to `gpio.4` (`Off`, `Input`, `Output low`, `Output high`, and
`Analog` on 1 and 2). The rows the screens keep to themselves: which
chats the lists show (`chats.dm`, `chats.channels`, `chats.rooms`: `All` /
`Favourites`), the keyboard (`keyboard.alphabet`, `keyboard.additional`;
on the L1 also `keyboard.type` and, with a CardKB, `keyboard.cardkb`; none
on a one-button device without a CardKB, whose strip types Latin only), the
radio's `radio.powersave` (`Off`, `On`, `Max`), and on the e-ink L1s
`display.rotation`, `display.joystick`, `display.full.refresh` and
`display.panel.sleep`. Live share's target, `share.to` (a channel's name,
else a contact's), and `share.scope` (`Target's` follows the target, or a
name from the scope list); the channels' own options, as lists of names:
`channels.favourite`, `channels.muted`, `channels.always` (a sound even in
quiet hours) and, where messages are kept on flash, `channels.unsaved`.
What is switched on: `gps`, `share` (live share, a session as from the
Map), `bluetooth`, `sound` (`On`, `Off`, `Auto`); on the L1 `trail.pace`;
on the L2 `display.tap.wake`, `display.accent` and `display.theme`
(changing it restarts the device). Sounds for one channel or contact, as
lists of names: `channels.melody1` / `channels.melody2`, `contacts.muted`,
`contacts.always`, `contacts.melody1` / `contacts.melody2`; and
`contacts.favourite`. Home: `home.field1` to `home.field3` (the clock's
fields) and `home.fav1` to `home.fav6` (a channel's or a contact's name,
`none` for an empty slot). The saved radio presets, `radio.preset1` to
`radio.preset4` (`Alps,869.4625,62.5,8,8`, empty to delete). `lock.pin`
sets the screen PIN (4 to 8 digits, empty for none); it reads back empty,
as only a hash of it is kept. On the L2, WiFi: `wifi` (allowed or not),
`wifi.networks` (the saved ones: leave one out to forget it) and
`wifi.add Home,password`.
`opts` marks
a text `#text:<max bytes>` and a list or a name to pick `#list` / `#pick`,
with the choices after it (`...` when they don't all fit). `get` shows a value as the Settings screen does, and `set`
takes it the same way, case ignored:

```
get telemetry.base     > Nobody
set telemetry.base Allowed
set display.off 30 s
set advert.loc on
set autoadd.room off
```

A number works too: the position in the list (`set telemetry.base 2`), or a
stored value such as seconds or millivolts (`set display.off 60`,
`set batt.shutdown 3300`). A wrong value answers with the ones that fit.
Factory reset and the node's keys are not in the console.

`opts <key>` shows where a setting is, its label, its value and every choice
(`> Display & power/DISPLAY|Screen off after|30 s|Never|5 s|…`); the
configurator builds its forms from it. `optsall` (Solo 2.4) sends that for
every key at once, a reply each (`@key > …`, a text's value after it as
`=key …`, then `> end <count>`): the configurator reads all the settings with
it in one go. And since Solo 2.3, what has no settings key:

| Command | Does |
| --- | --- |
| `msgs`, `get msg.3`, `set msg.3 …`, `set msg.3` | The quick messages, numbered from 1; the last empties one |
| `get melody.1`, `set melody.1 120 292a2b` | A melody of the Ringtone editor: BPM and a hex byte a note |
| `get batt.curve`, `set batt.curve 3400,3690,3720,3750,3780,3820,3860,3920,3990,4060,4170`, `set batt.curve builtin` | The battery curve: the voltage in mV at 0, 10, ... 100 %, rising, or `builtin` for the built-in LiPo curve -- a whole curve at once, e.g. one measured on another device |
| `wpts`, `get wpt.2`, `set wpt.2 50.061,19.937 Camp`, `set wpt.2` | The waypoints, numbered from 1 as the list shows them: `wpts` says how many there are and how many fit; `set` replaces one, or adds one with the number after the last; the last form deletes it, and the ones after move up |
| `pages`, `set pages Clock, Messages, Status` | Home's pages in order; the ones left out are hidden (not on the L2) |
| `input`; `key down`, `key enter long`; `btn click`; `tap 160 120`, `swipe 300 120 20 120` | The device used from the console as from its own keys (Solo 2.4; the configurator's Live view): `input` says how -- `keys`, `button` (one button: `click`, `double`, `long`, `hold`) or `touch` (the L2: x, y on the 320x240 screen, and the ms held or dragged at the end) |
| `batch on` … `batch off` | The `set`s in between are saved once, at `batch off` (or 5 s after the last command): a configurator's backup put back in seconds, not minutes |
