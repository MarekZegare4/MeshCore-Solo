# Location messages

[Go back](README.md)

How Solo puts a position into an ordinary MeshCore text message, so that
another firmware or app can send and read the same. There is no new packet
type: everything below is plain text in a direct message or a channel post,
readable as it is on a device or app that knows none of it.

## Coordinates

Decimal degrees, latitude then longitude, a comma between them, no space:

```
50.06143,19.93658
```

- **Sent** with 5 decimals (about 1 m), `-` for south and west, no `+`.
- **Read** with any number of decimals, but at least one decimal point (so
  "5,6" in ordinary text is not a position), a space allowed on either side of
  the comma, latitude within ±90 and longitude within ±180. The first such pair
  in the text counts.

## The types

| Tag | Meaning | Sent as | Where the tag may be |
| --- | --- | --- | --- |
| `[WAY]` | A place to save: a waypoint shared from one device to another | `[WAY]50.06143,19.93658 Hut` | anywhere in the text |
| `[LOC]` | The sender's own position now, for following them on a map | `[LOC]50.06143,19.93658` | anywhere in the text |
| `[SOS]` | A call for help with the sender's position | `[SOS] 50.06143,19.93658 212m 14:32` | at the very start only |

A coordinate pair with no tag (as the `{loc}` placeholder writes it into any
message) works too: on the receiving device the message's menu offers Save
waypoint and Navigate, as for `[WAY]`, only without a label.

### `[WAY]` — a waypoint

```
[WAY]<lat>,<lon> <label>
```

The label is the rest of the text after the coordinates, trailing spaces
trimmed; it may be empty. Solo stores up to 11 bytes of it (UTF-8, cut on a
whole character). The receiver offers to save the point; nothing moves on its
own.

### `[LOC]` — live position

```
[LOC]<lat>,<lon>             in a channel post
[LOC]<lat>,<lon> <name>      in a direct message
```

A channel post already starts with the sender's name ("Anna: [LOC]..."), as
MeshCore puts it there; a direct message has no name in its text, so the
sender adds its own after the coordinates. Only an explicit `[LOC]` moves the
sender's pin: a bare pair of numbers does not.

What Solo does with them:

- **Sending (Live share):** to one channel or one contact. The first position
  goes out at once; after that one goes when the sender has moved at least
  50 / 100 / 250 / 500 m *and* at least 30 s / 1 / 2 / 5 min have passed since
  the last one, plus an optional heartbeat every 5 or 15 min while standing
  still. A share always ends by itself after 1 / 2 / 4 / 8 / 12 h. (All of
  these are settings.)
- **Receiving:** up to 16 people followed at once, on the map, in the Live
  list and as a target to navigate to. A position is dropped 20 minutes after
  its last update. One from a contact (a direct message, the sender known by
  its key) counts as verified; one read from a channel post is matched by the
  name in front of it only.

### `[SOS]` — a call for help

```
[SOS] <the sender's text, its placeholders filled>
```

The text after the tag is the sender's own, set in its settings; Solo's
placeholders are `{loc}` (the position, "last 50.06143,19.93658 12min ago"
for an old fix, "no GPS" without one), `{alt}` ("212m"), `{time}` ("14:32")
and `{batt}` ("18%"). When the text has no `{loc}`, the position goes first,
right after the tag: an SOS is never sent without one when there is a fix.
The receiver takes the first coordinate pair after the tag as the position.

It goes to the channels and contacts chosen beforehand, again every so often
(12 sends at most) until someone replies with the agreed acknowledgement word
or the sender stops it. A receiver keeps an SOS on its map for 24 hours.

The code: `examples/companion_radio/GeoUtils.h` (formats and parsing),
`ui-core/LiveShareEngine.h` (Live share), `ui-core/SosEngine.h` (SOS).
