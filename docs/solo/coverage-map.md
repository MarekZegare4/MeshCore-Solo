# Coverage map (MatterMap)

> [!NOTE]
> **Wio Tracker L2 only** (it needs the SD card and WiFi). Voluntary: it is
> off until you turn it on, and nothing is recorded or sent before that.

[MatterMap](https://mattermeshos.com/coverage/) is a worldwide map of where
MeshCore can be heard, drawn from what people's devices hear while they move.
It is made by [MatterMesh OS](https://mattermeshos.com), another MeshCore
firmware; Solo can add to it. The map is still in closed beta.

## Turning it on

**Settings › Coverage map › Record coverage.** The page explains what is
recorded and what is sent; read it before you turn it on.

## What is recorded

While it is on, the device **only listens**: it sends nothing over the mesh
for this. It writes a small file to the SD card (`meshcore/coverage/`):

- for every packet its own radio hears: the time, your position, the signal
  (SNR and RSSI), and the repeater that passed it on (the last hop of a
  flood packet's path);
- every 30 seconds with nothing heard, a "nothing heard" point, so quiet
  areas show too.

Nothing is written:

- without a GPS fix, or with one more than 10 seconds old;
- with the clock not set;
- inside the **home zone**: a radius (250, 500 or 1000 m, or Off) around a
  point you pick under **Home**: where you are, or a waypoint. 500 m by
  default; until you pick a point there is no home zone. Picking it later,
  moving it or making it larger also takes out the points already recorded
  inside it, from the file being written and from those waiting to go, and
  each file is checked again just before it's uploaded. Points already sent
  stay on the map.

A file is finished when you turn recording off, when it reaches 1.5 MB,
when you come into the home zone (so it goes up on its own once you're
home) and when you tap **Upload now** (recording goes on in a new file).
Turned off and on again, or after a restart, recording goes on in the same
file. When a file is finished, the repeaters it heard that have a position in
their adverts are added at its end (their name, position and the start of
their key), so the map can show them.

## What is sent

Finished files go to `mattermeshos.com` over your saved WiFi (Settings ›
WiFi):

- **Upload automatically** (on by default): when files are waiting, the
  device joins the WiFi for the upload and lets it go afterwards; after a
  failed try it waits half an hour.
- **Upload now** tries at once.

With each file go: the board ("wio-l2"), the firmware version and a device
number made from a hash of the WiFi address. It isn't your node's key or
address; the server uses it only to resume an upload and to limit how many
uploads come from one device. A file that was sent is moved to
`meshcore/coverage/sent/`; one the server refused, to `failed/` (the page
says why).

**Delete recorded points** (red, at the end of the page; tap twice) deletes
every coverage file on the SD card: the one being written (recording goes on
in a new one), those waiting, and the copies in `sent/` and `failed/`. What
the map already has stays there.

## What the map shows

Only areas of about 200 m: never who measured them, never a route. The
server also drops 300 m around the first and last point of every file, keeps
the raw files for 7 days and then deletes them. MatterMesh OS's
[developer page](https://mattermeshos.com/coverage/developers/) describes the
format and the rules.

## Credits

MatterMap and its format are by Andrea Quartararo,
[MatterMesh OS](https://mattermeshos.com). Solo's location messages
([`[WAY]`, `[LOC]`, `[SOS]`](./location-messages.md)) are compatible with
MatterMesh OS the other way round.
