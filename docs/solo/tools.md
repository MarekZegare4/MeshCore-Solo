# Tools

On joystick devices, the tools are in **Tools**, grouped into Location, Comms
and System; the Tools home page keeps the two you used last. The navigation
tools are on the [Navigation](./navigation.md) page, Nodes and Admin on
[Contacts](./contacts.md); the [Status](#status) screen opens from its own
home page.

> [!NOTE]
> **Wio Tracker L2:** the tools are apps on the home screen: Messages, Nodes,
> Settings, Compass, Clock, GPS, Bot, Repeater, Admin, Diagnostics, Flashlight,
> Gallery, SOS. The trail, live sharing and the locator are on the map (its tools
> button) and in Settings › Map.

## Clock

The **Clock** home page shows the time with a bar under it that fills with
the seconds (Settings › Display › Clock seconds), the date, and up to three
fields you choose (hold Enter): battery, temperature, humidity, pressure, altitude
(barometric or GPS), light, CO₂, GPS position, satellites, contacts, unread
messages. The time comes from GPS or the phone app (on the Wio Tracker L2
also from the internet whenever WiFi is connected); the time zone and summer
time are in Settings.

**Enter** on the clock opens the clock tools:

- **Alarm**: a time, repeating daily, on weekdays, at weekends or once. It
  rings with the screen off or locked, but not after a shutdown.
- **Timer**: a countdown that rings when it reaches zero, whatever screen
  you're on.
- **Stopwatch**.
- **Calendar**: the month with today marked and the alarm's days
  underlined; **Left / Right** pages through the months, **Enter** comes
  back to this one.

Any key silences the ringing; it stops by itself after a minute.

## SOS

> [!IMPORTANT]
> **Experimental.** SOS is new and hasn't been proven in the field yet. How it
> starts, its settings and [its message](#the-message-is-open-for-discussion)
> may change from one release to the next. Try it out, and don't make it your
> only way to call for help.

**Tools › SOS** sends your position to the people you choose, in a few seconds
and without opening a menu. Set it up before you need it:

| Row | Does |
| --- | ---- |
| **Hold Back** | Lets a hold of **Back** start it. Off by default |
| **Countdown** | 5, 10 or 15 seconds to cancel in |
| **Repeat** | How often it's sent again: every 1, 2, 5 or 10 minutes, or **Off** for once |
| **Beep** | Off, or every 30 seconds, 1 or 5 minutes: `... --- ...` on the buzzer at full volume, so someone nearby can find you if you can't answer |
| **Channels** | Which channels it goes to; **Enter** switches each one |
| **Contact 1-3** | Up to three direct contacts. **Enter** picks one, **Left / Right** empties the row |
| **ACK word** | The word that means "received", `ACK` by default |
| **Note** | Added to the end of the message, for example your blood group |
| **Send SOS** | Starts it from here, on devices without a Back button too |

**Starting:** hold **Back** for 3 seconds. It works with the screen off and on a
locked device, and needs no PIN. The device beeps every second and counts down,
even with the buzzer off. **Any key** cancels.

**The message** is short, to reach as far as possible:

```
SOS 50.06143,19.93658 ALT 212m sent 14:32
```

The GPS is turned on for it, if it was off, and back off when the SOS ends.
Without a fix it gives the last position from the past hour, with its age
(`SOS last 50.06143,19.93658 12min ago`). An older one could be far from where
you are, so then it says `SOS no GPS`. Either way one more goes as soon as the
GPS gets a fix. The **Note** comes last. The format isn't final, see
[below](#the-message-is-open-for-discussion).

**Receiving one:** a banner names the sender, also when there's no position.
A position shows on the map and in Nearby as `SOS name`, even with **Track loc**
off. It stays for 24 hours,
or until a restart. **Hold Enter** on the message navigates to it. Other
firmware and the phone app show it as an ordinary message.

**While it runs**, the status shows how many were sent and when the next goes.
It's sent up to 12 times, then the device goes on listening, with one more
send if the GPS gets its first fix after that. **Beep** goes on, even with the
buzzer off, after an answer too, until you end the SOS.

To save the battery while it runs, the screen goes off after 30 seconds whatever
your auto-off says. GPS power saving waits until nothing more will be sent, and
then a GPS the SOS turned on goes back off. The repeater (and WiFi on the L2)
rests until the SOS ends, so the message goes out on your own network. During an
SOS the bot refuses `!gps`.

**Back** hides the status and the SOS carries on, with `SOS` in the top line;
holding **Back** again, or **Send SOS**, brings it back. **Hold Enter** ends it.
A restart ends it too, and doesn't put back what it rested: WiFi stays off, and
so does the repeater if a setting was saved in the meantime.

**The answer:** when a contact, or anyone on one of the chosen channels, replies
with the ACK word, the buzzer sounds whatever its setting, the repeats stop and
the screen shows **Answered**: who, what they wrote and how long ago. The word
counts on its own and in any case: `ack, on my way` does, `back` doesn't. On a
public channel anyone can send it.

Whoever has answered needs no ACK word after that: `10 more minutes` shows up
too, with a short sound, and so does anyone else who answers (the L2 shows the
latest two). On the L2 the top bar turns green, and the screen comes back to
this each time it wakes.

> [!WARNING]
> This is not a rescue service. A message only reaches people whose devices are
> in range of you or of a repeater, and nothing guarantees that anyone is there
> or awake. Agree with the people you pick that they'll answer, and test it
> together, on a private channel, before you rely on it.

> [!NOTE]
> Other devices without a Back button start it from **Send SOS**.

### The message is open for discussion

The text above is a first version, and a change is easy to make while SOS is
experimental. Only its start is fixed for receivers: `SOS` followed by a
position, `SOS last` followed by a position, or `SOS no GPS`. That's what the
map and the banner read, so a change there needs both sides updated. Everything
after it is for people to read.

Open questions, for [issue #56](https://github.com/MarekZegare4/MeshCore-Solo/issues/56):

- **Accuracy:** the request asked for `±3m` after the position. It isn't sent
  yet. The GPS reports HDOP, which is only a rough guide to metres, and not
  every module gives it. Is an estimate worth the extra bytes?
- **Digits:** the position has 5 decimals, about 1 metre. 4 decimals, about
  11 metres, saves 2 bytes.
- **Labels:** `ALT 212m sent 14:32` reads easily. Commas, or dropping `sent`,
  would change little in length. Is the time useful at all, given that a
  repeat can arrive late?
- **Anything missing:** the battery level would tell a rescuer how long the
  device will last, at a few more bytes.
- **Own language:** the note can be anything, but the rest stays in English.

### On the Wio Tracker L2

The **SOS** app has the same settings, with **Top button** in place of Hold
Back; favourites with their star come first in the contact list. **Holding the top button for 5 seconds** beeps and opens **Emergency SOS**
over everything, with the screen off or locked too and without the PIN. **Slide**
the knob to the end to start: a tap or a short slide does nothing, so a button
pressed in a pocket can't send one. **Send SOS** in the app opens the same slider.

**Cancel** or either button ends the countdown. While it runs, **Hide** puts the
view away, and the status bar shows a red `SOS` and a title bar's clock turns
red; hold the top button again, or tap **Send SOS** in the app, to come back.
**Slide to stop** ends it. A received SOS shows as a short message at the bottom
of the screen and on the map.

## Remote bot

**Tools › Bot** answers messages for you. It watches your direct messages, one
channel and one room, each switched on separately:

- **Trigger and reply**: when a message contains the trigger (several can be
  separated by commas, `*` matches any message), the bot sends the reply. The
  reply can use placeholders, plus `{name}` (the sender), `{hops}`, `{path}` (the route in, as hop hashes oldest first: `3f:a1:c8`, or `direct`), `{snr}` and `{rssi}` (how its last hop was heard).
- **Commands**: a message starting with `!` gets live data back (a `!word`
  further on in the text does nothing, so people can talk about them):

  | Command | Reply |
  | ------- | ----- |
  | `!ping` | `pong` |
  | `!batt`, `!temp`, `!time`, `!loc` | battery, temperature, time, position |
  | `!hops` | how many hops the command took (`known route` as for `!path`) |
  | `!path` | which way it came: the hop hashes, oldest first (`3f:a1:c8`; 2-byte mode `3fa1:c802`), `direct`, or `known route` when it came over a saved path (whose hops aren't carried) |
  | `!snr` | how its last hop was heard here: SNR and RSSI |
  | `!dist` | how far away and which way the sender is (both need a position) |
  | `!status` | battery, position and time |
  | `!help` | the list of commands |

  Several in one message get one reply: `!batt !time` → `4.10V | 14:30`.

  **Allowed**, under **Commands** on the Channel, Room and Direct tabs, sets
  which of them that place answers, so `!loc` can work in a direct message from
  a favourite and not on a channel. All are on by default. A command that's off
  gets no reply, as if the word meant nothing, and `!help` leaves it out.

  **`!loc`** is the switch for your position: off, `!status` answers without
  it, `!dist` is off too and `!gps fix` answers `GPS: fix off`. It doesn't touch
  a **Reply** you wrote yourself: a `{loc}` there still sends it.
- **Actions** (off by default) let the command change the device: `!buzz`
  (sounds the buzzer to find it), `!gps on` / `!gps off`, `!gps fix` (turns
  the GPS on, waits for a good fix and sends it), `!advert`. With **Actions** on,
  **Allowed** lists these too. During an
  [SOS](#sos) `!gps` is refused (`GPS: on for SOS`). **Actions by =
  Fav** (Other tab) lets only your favourites run them; on a channel the
  sender is just a name, so there it's a weak check.

On ESP32 boards (L2, Heltec, ...) the **Log** tab lists the bot's last 32
replies: when, who, what they asked and what it answered. Kept until a restart.

**DM allow = Fav** limits the DM bot to your favourites. **Position = Fav**
(Direct tab) keeps everyone else's commands working but treats them as if
`!loc` were off, so anyone can `!ping` you and only favourites learn where you
are. A direct message's sender is verified, so this one is a firm check.

**Quiet hours** stop the bot at night, except commands in a direct message:
those are asked for and go to one person. **Reply gap** (Other tab) is how long
it waits between replies on the channel and in the room, 10 seconds to 5
minutes: each one goes out to everyone in range, and it keeps two bots from
answering each other forever. Direct messages wait 10 seconds per contact. The
room bot uses the room's saved login.

## Auto-advert

**Tools › Auto-advert** sends your advert every 30 seconds to 1 hour (with
your position if Settings › Privacy › **Advert pos** is on), so others see you in their Nearby list. With it on at both ends
and Settings › Sound › **AD sound** set, each device beeps when it hears the
other: a hands-free "still in range".

## Repeater

**Tools › Repeater** makes the device relay other people's packets while it
keeps working as a companion. By default it relays on a separate repeater
profile (the community's repeater frequency) and switches back when you turn
it off; **Network = Current** relays on your own frequency instead.

Optional filters keep a mobile repeater from adding noise: skip adverts, a
maximum hop count, **Yield** (let fixed repeaters go first), a minimum SNR,
drop duplicates already relayed by someone else, and relay only your
**scopes**. The Status screen's **Mesh** tab shows how much it forwards.

## Ringtones

**Tools › Ringtone** composes two melodies of up to 32 notes (pitch, octave,
length, tempo). Use them for notifications in Settings › Sound, or for a
single contact or channel from its options.

> [!NOTE]
> **Wio Tracker L2:** the melody editor is under Settings › Sound.

## Flashlight

**Tools › System › Flashlight** lights the whole screen white at full
brightness and keeps it on until you press a key; the brightness goes back
when you leave. Not on e-ink, which gives no light.

> [!NOTE]
> **Wio Tracker L2:** the Flashlight app. A tap, the side button or the top
> button puts it away.

## Gallery

> [!NOTE]
> **Wio Tracker L2 only.** Two tabs. **Screenshots** shows the ones on the SD
> card (side + top button takes one), newest first, as thumbnails, 30 to a
> page. Tap one to see it on the whole screen: swipe for the next or the
> previous, tap for its name, **Delete** and close. **Trails** lists the saved
> trails; tap one to open it on the map with Load, GPX and Delete.

## Status

The **Status** home page sums up the radio, GPS, battery and mesh in four
tiles; **Enter** opens the Status screen, one tab per subject (**Left /
Right** between them, **Up / Down** scrolls):

| Tab | Shows |
| --- | ----- |
| **Radio** | Frequency, SF / BW / CR, TX power, the noise floor over the last half hour, the last packet's signal |
| **GPS** | On / off (**Enter** switches it), fix, satellites, position, altitude, speed, precision, time to first fix |
| **Sky** | The satellites on a sky plot, or each one's signal as bars (**Enter** swaps them); Wio Tracker L1 |
| **Power** | Battery level, voltage, source and trend, then the readings of any sensors |
| **Mesh** | Packets received and sent with a traffic line, forwarded, nodes heard, contacts; **hold Enter** resets the counters |
| **System** | Uptime, firmware, device, memory, the send queue, errors, and a sample of every script the font covers; **Enter** shows your contact as a QR code, which the phone app scans to add you, and **Enter** again a QR code that opens this manual; any other key closes it |

**GPS pwr** in Settings › System turns the GPS off between fixes to save
battery; anything that needs your position (the trail, live share, the
locator) keeps it on.

> [!NOTE]
> **Wio Tracker L2:** the Diagnostics app, with a **Noise** tab that
> measures the noise floor over time and history charts you can touch for the
> value at that moment, and the **GPS** app, on three tabs: **Sky** (a plot
> of the satellites and the fix), **Signal** (a bar per satellite with its
> strength, and the constellations) and **Details** (position, precision,
> time to first fix and what the receiver reports).
> Your contact's QR code and the manual's are in Settings › About.

## GPIO

*Wio Tracker L1 only.* **Tools › GPIO** sets four spare pins as input,
output or (GPIO1–2) analog input, shows their level and switches outputs. The
bot's `!gpio1`…`!gpio4` commands read and set the same pins.
