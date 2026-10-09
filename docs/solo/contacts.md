# Contacts

## Nearby nodes

**Tools › Nodes** lists the nodes the device has heard: their type, how long
ago, and, when they have a position, distance and bearing. **Left / Right**
filters by type (All, Fav, Companions, Repeaters, Rooms, Sensors); the sort,
by distance or by last heard, is in the options. A ★ marks a favourite, a ♦
someone sharing their position live.

**Enter** shows a node's details. **Hold Enter**, in the list or the details,
for what you can do with it:

| Option | Does |
| ------ | ---- |
| Map | Every node with a position on a map (below) |
| Navigate | Distance and bearing to the node; follows it if it shares its position live |
| Ping | Sends a ping and shows the round trip time and SNR |
| Save waypoint, Set as target | Its position as a waypoint, or as the [Locator](./navigation.md#locator) target |
| Fav, Pin to dial | Favourite, or a place on the Favourites page |
| Admin | Remote admin, for a repeater or room (below) |
| Discover scan | Asks the repeaters, rooms and sensors in direct range to answer, with their signal |

**Map** shows the nodes of the current filter that have a position, with you
and the target, fitted to the screen, north up, with a scale. Nodes are dots
(a diamond for a live share); the selected one is boxed, and the line under
the map gives its name and distance, with **+n** when others sit on the same
spot. **Left / Right** selects the next node, **Up / Down** zooms in on it and
back out, **Enter** opens its details and **Hold Enter** its options.

> [!NOTE]
> **Wio Tracker L2:** the **Nodes** app. Filter chips at the top, the sort in
> the header, and a map button that shows every node with a position. A
> node's card has buttons for the same actions, plus **Add** for a node that
> isn't a contact yet and **Delete**.

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
