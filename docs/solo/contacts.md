# Contacts

## Nearby nodes

**Tools › Nodes** lists the nodes the device has heard: their type, how long
ago, and, when they have a position, distance and bearing. **Left / Right**
filters by type (All, Fav, Companions, Repeaters, Rooms, Sensors); the sort,
by distance or by last heard, is in the options. A ★ marks a favourite, a ♦
someone sharing their position live.

> [!NOTE]
> **Wio Tracker L2:** the line over the list holds the count and two choices,
> which nodes to show and in what order; **Scan** and the map are in the
> title bar.

**Enter** shows a node's details. **Hold Enter**, in the list or the details,
for what you can do with it:

| Option | Does |
| ------ | ---- |
| Map | Every node with a position on a map (below) |
| Navigate | Distance and bearing to the node; follows it if it shares its position live |
| Ping | Sends a ping and shows the round trip time and SNR |
| Save waypoint, Set as target | Its position as a waypoint, or as the [Locator](./navigation.md#locator) target |
| Fav, Pin to dial | Favourite, or a place on the Favourites page |
| Telem | What this contact may ask for: None, Status, Stat+loc, Stat+sens, All. Shown when a [Privacy](./settings.md#privacy) setting is **Allowed** |
| Path / trace | The route to the node: edit it, trace it, save it (below) |
| Reset path | Forgets the learned route; the next message floods and finds a new one. Shown when there is a route |
| Share nearby | Sends the contact's last advert to the nodes in direct range, so they can add it |
| Admin | Remote admin, for a repeater or room (below) |
| Discover scan | Asks the repeaters, rooms and sensors in direct range to answer, with their signal |

**Map** shows the nodes of the current filter that have a position, with you
and the target, fitted to the screen, north up, with a scale. **Flow** in its
options draws what the radio hears: each packet as a line from the node that
sent it, through the repeaters it passed, to you, for a minute; a packet you
send is a ring around you. Only nodes with a position can be placed, so a
line may skip a hop. Nodes are dots
(a diamond for a live share); the selected one is boxed, and the line under
the map gives its name and distance, with **+n** when others sit on the same
spot. **Left / Right** selects the next node, **Up / Down** zooms in on it and
back out, **Enter** opens its details and **Hold Enter** its options.

On e-ink the map also writes the nodes' names beside their dots (where they
don't cover each other) and draws your trail, dotted, and your waypoints. On
the 4.2" and 4.26" panels the selected node sits beside the map: its
distance and direction, type, when it was last heard, its path and a compass
pointing at it. Under it, with Flow on, a legend counts each kind of packet
heard in the last minute, and the kinds are told apart by their lines:
adverts solid, direct messages long dashes, channels short dashes, the rest
dash and dot.

> [!NOTE]
> **Wio Tracker L2:** **Packet flow** is in the map's options (On the map).
> Every packet heard shows. One that can be placed runs along its line as a
> dot and fades. One that can't (its sender or repeaters have no position)
> is a ring around you. Colours by kind: adverts blue, direct messages
> green, channels amber, requests violet, acks and paths teal, the rest
> pink, your own white. The pill at the top counts each kind over the last
> minute. A tap on it opens **Recent packets**: the last 10 that made a line,
> with what they were, who sent them, when, and how many hops they took. A tap
> on one plays it again on the map, every way it came, and moves the map to
> show it. **Play all** plays them one after another. The list is kept until a
> restart.

> [!NOTE]
> **Wio Tracker L2:** the **Nodes** app. Filter chips at the top, the sort in
> the header, and a map button that shows every node with a position. A
> node's card has buttons for the same actions, plus **Add** for a node that
> isn't a contact yet and **Delete**. The telemetry permissions are three
> switches under **Can ask for**.

## Path and trace

**Path / trace** in a contact's options shows the repeaters its messages go
through, in order, and the node at the end. **+ Add repeater** picks one from
the Nodes list, **Enter** on a hop moves or removes it, and **Left / Right**
moves it up or down.

**Trace** sends a trace along the route and back the same way. Each node
answers with how well it heard the one before it, so every hop shows two
numbers in dB: the way out and the way back. Only a node that repeats can
answer: repeaters do, companions only with repeat on. **To node** off traces
just the repeaters, which still tells you whether the way to the node is good.

With no reply, **Find break** traces to the first repeater, then the second,
and so on, and shows where it stops. If every repeater answers, the node
itself didn't.

**Discover** finds the route for you. It asks the node by flood, and the
reply says which repeaters the flood went through to reach it, and which the
reply took back. These come from the mesh itself, not from anyone's position
on the map. On the **Discover** row, **Left / Right** sets 1, 3 or 5 tries
and **Enter** starts it (or stops it). Each try waits for its reply or its
timeout, then 20 seconds more, so the floods don't come all at once. Each
route found is listed under it with how many times it came; **Enter** on one
puts it in the list, ready to **Trace** and save. When the reply came back
another way, a **Back** row follows it: that way turned round, as a route to
the node, so a trace checks it too (a trace goes there and back on the same
repeaters). A repeater you don't have
as a contact shows as its hash (`?8D`). Nothing is saved until you save it.

Only a node that answers requests can be asked: a companion whose
Settings › Privacy › **Ask status** lets you (**Everyone**, or **Allowed**
with Telem given to you), or a repeater or room you're logged in to (a guest
login is enough). Your own Ask status decides the same for others finding a
route to you. Not to be confused with **Discover scan** in the node options,
which looks for nodes in direct range.

**Save path** makes the list the contact's route; saved empty, messages go
straight to the node. The route a node sends back usually replaces it again;
**Keep path** stops that, and keeps the route after failed sends too.
**Reset path** forgets it, keep included.

> [!NOTE]
> **Wio Tracker L2:** **Route** in a node's card. Each repeater and the node
> take one line, with the link's out / back dB on the right: green when good,
> amber when fair, red when weak. Tap a repeater to move or remove it, tap
> the node to trace to it or stop at the last repeater. **Keep** is in the
> header, **Find where** appears next to a "No reply". The repeaters can also
> be tapped on the map, in order (**Map**); **Undo** takes the last one out.
> **Discover** is under the route, in **Find the route**, with the number of
> tries beside it and where it is (the try waiting for its reply, or the
> seconds to the next one). Tap a route found to use it; the way back is under
> it. When the way back is another one, **There** and **Back** put either in
> the list to trace.

## Your own contact

**Status › System**, **Enter** shows your contact as a QR code: the phone
app's scanner adds you from it, with your name and key. On the Wio Tracker L2
it is in **Settings › About**.

## Favourites

The **Favourites** home page holds six slots for the conversations you open
most: a contact, a room or a channel, each with its unread count. **Enter** on
an empty slot picks one from Messages; **hold Enter** on a filled slot to
replace or remove it. **Pin to dial** in any contact, channel or node menu
does the same.

A pinned slot is not the same as a favourite. **Fav** is the star shared with
the phone app: it sorts contacts to the top and drives the "favourites only"
filters in Settings › Contacts.

> [!NOTE]
> **Wio Tracker L2:** the first home page. Tap a slot to open it, hold it to
> change it.

## Cleaning up

Settings › Contacts › **Expire** (7, 30 or 90 days) sets when a contact you
haven't heard from counts as inactive, and **Prune now** removes those after
showing how many it will delete. Favourites are always kept, and nothing is
removed without **Prune now**.

## Remote admin

**Tools › Admin** manages a repeater or room server you are an admin on, the
same way the phone app does. Pick the node, type its admin password (saved for
next time), then choose a field:

- **System**: name, owner info, admin password.
- **Radio**: frequency, bandwidth, spreading factor, coding rate, TX power.
- **Routing**: repeat on/off, advert intervals, max hops.
- **Actions**: send an advert, sync its clock, reboot, start OTA, or any
  [CLI command](../cli_commands.md).

Name and owner info are read from the node first, so you edit the current
value. Reboot and OTA ask before sending.

> [!WARNING]
> Admin commands change the remote node. Check the node and the value before
> sending.
