## Messages Screen

[Go back](../../../README.md)

### Overview

|           OLED            |           E-Ink           |
| :-----------------------: | :-----------------------: |
| ![](./overview_oled.png) | ![](./overview_eink.png) |

The Messages screen is split into three modes — **DMs**, **Channels**, and **Rooms** — selectable with UP/DOWN on the mode-select screen. Each mode shows the corresponding list of conversations with unread counters.

---

### Sending messages

|           OLED            |           E-Ink           |
| :-----------------------: | :-----------------------: |
| ![](./compose_oled.png) | ![](./compose_eink.png) |

Press **Enter** on a contact or channel to open its history, then press **Enter** again (or select the **[+ send]** button, anchored at the right edge of the history) to compose a message. Choose between:

- **Custom message** — opens the on-screen keyboard
- **Q1–Q10** — quick reply templates editable in Settings › Messages

While typing, **UP** from the top letter row enters cursor mode — LEFT/RIGHT move the insertion point, UP/DOWN jump to start/end (then to the grid on a second press), Enter/Cancel exit from anywhere — so you can edit mid-text, not just at the end. **Hold Enter** on a letter with accented variants (a, e, c, n, o, s, z…) opens a one-row accent popup instead — LEFT/RIGHT picks, Enter inserts, Cancel dismisses. Full key set (Shift, T9, Cyrillic/Greek) in the [UI framework guide](../../design/solo_ui_framework.md).

The keyboard supports placeholders that insert live data at send time:

| Placeholder | Value                | Availability                |
| ----------- | -------------------- | --------------------------- |
| `{time}`    | current time (HH:MM) | always                      |
| `{loc}`     | GPS coordinates      | always ("no GPS" if no fix) |
| `{temp}`    | temperature          | sensor connected            |
| `{hum}`     | humidity             | sensor connected            |
| `{pres}`    | barometric pressure  | sensor connected            |
| `{alt}`     | altitude             | sensor connected            |
| `{lux}`     | luminosity           | sensor connected            |
| `{co2}`     | CO₂ concentration    | sensor connected            |

Sensor placeholders appear automatically in the placeholder picker when the corresponding sensor is active. `{time}` and `{loc}` are always shown.

---

### Rooms — logging in

Posting to a **room server** needs a login handshake — the device does this on its own, no phone app needed. First **Enter** on a room opens a password prompt automatically; type it and press **✓** (empty for no-password rooms). On success the chat opens automatically, no second Enter needed.

- **Passwords are remembered across reboots.** After a successful login it's saved on the device, so picking that room again — even after a power cycle — logs back in silently.
- **A wrong or changed password self-heals.** A failed login forgets the saved password, so the next **Enter** prompts for a new one.
- **Re-login any time** with **Hold Enter** on the room → **Login…** — useful to switch passwords without waiting for a failure.
- **Log out** with **Hold Enter** → **Logout** (only offered once logged in). Forgets the saved password, so the next open prompts for one again.
- Passwords set from the **phone app** are saved on the device too, so it can post to that room standalone after a reboot.

> The on-screen keyboard's default (Latin) page is ASCII only. Typing accented or non-Latin characters — Polish, Czech, Slovak, German, French, Spanish, Portuguese or Nordic diacritics, Cyrillic, or Greek — needs Settings › Keyboard › Alphabet set to the matching language first; the keyboard's **#@/abc** key then cycles Latin → that alphabet → Symbols → Latin. A password containing characters outside whatever's currently enabled can still be set from the phone app — the device stores and replays it byte-for-byte.

---

### Message history

|           OLED            |           E-Ink           |
| :-----------------------: | :-----------------------: |
| ![](./history_oled.png) | ![](./history_eink.png) |

Messages appear as chat bubbles sized to their content — **right**-anchored for outgoing, **left** for incoming — with sender name and a compact age indicator (`3m`, `2h`, `>1d`) in the top-right corner. List runs **newest at the bottom**; opening a history starts at the latest message, scrolling up goes further back. The list wraps at both ends like every other list: **UP** at the oldest message jumps to the newest, and **DOWN** past the newest lands on the compose row and then wraps to the oldest.

A tiny digit icon on a bubble is its hop count: on your own messages, how many repeaters echoed them back; on a **received** DM or channel post, how many hops it took to reach you. A received message always shows a time — if its timestamp is unknown or reads slightly ahead of this device's clock (sender/receiver clock skew, or the clock isn't synced yet), the receipt time is shown instead. In a channel history the title carries the channel's scope in brackets (`name [scope]`) whenever it is set to anything but `*`.

**Short Enter** on a message opens it in fullscreen. **Hold Enter** — on a history row or in fullscreen — opens the same options menu: Reply, plus **Navigate** / **Save waypoint** / **Set as target** when the message contains a location, and **Path** / **Relayed by** when hop data is available (see Fullscreen message view). You don't need to open the message first.

---

### Fullscreen message view

|           OLED            |           E-Ink           |
| :-----------------------: | :-----------------------: |
| ![](./fullscreen_oled.png) | ![](./fullscreen_eink.png) |

Navigate between messages like pages in a book — **LEFT** goes back to the older message, **RIGHT** forward to the newer one. The `<` / `>` markers along the bottom edge show which directions still have a message. Long messages scroll with **UP/DOWN**.

If the message is a reply addressed to someone (`@[nick]`), a **To: nick** bar is shown below the sender name and the body is displayed without the address prefix.

|           OLED            |           E-Ink           |
| :-----------------------: | :-----------------------: |
| ![](./fullscreen_menu_oled.png) | ![](./fullscreen_menu_eink.png) |

**Hold Enter** in fullscreen opens the options menu. It always offers **Reply** for an incoming message, and when the message contains a **location** it adds three more:

- **Navigate** — opens the bearing/distance view to those coordinates (the same two-bearing screen as Waypoints and Nearby; **Back** returns to the message).
- **Save waypoint** — stores the location as a waypoint (visible on the trail map and in the Waypoints list).
- **Set as target** — pins those coordinates as the active **Locator/Nav target** in one step, the same row Nodes and Waypoints offer (see Tools › Locator).

A location is any `lat,lon` pair in the text — exactly what the `{loc}` placeholder inserts — so you can navigate to anything a contact shares. A `[WAY]lat,lon label` share also carries a name, used as the waypoint label. This works on DMs and channel messages, incoming or outgoing.

When the entry has hop data recorded, the menu also adds one more row:

- **Path (N hops)** — on a received message (DM or channel), lists every repeater the message actually travelled through to reach you, oldest hop first.
- **Relayed by (N)** — on your own channel post instead, lists every distinct repeater heard rebroadcasting it back into the mesh (order isn't meaningful here — each one heard it independently, not as a chain).

Selecting the row opens a read-only list of the resolved hops — each shown as the matching contact's name where one is known, or a short `?AABB`-style hex tag for an unrecognised repeater. Only repeaters within range of the message's actual travel — or, for **Relayed by**, within your own device's radio range — can ever be identified this way; a message with no recorded path (e.g. a zero-hop send, or one sent before any repeater relayed or echoed it) doesn't show this row at all.

---

### Context menu — contact list

**Hold Enter** on a contact entry opens a context menu:

> Rows that show a value (`Notif:`, `Melody:`, `Fav:`) are changed in place — **LEFT/RIGHT** steps the value and **Enter** advances it, with the menu staying open. Only **Back** closes the menu. The same rule holds in every context menu on the device.

|           OLED            |           E-Ink           |
| :-----------------------: | :-----------------------: |
| ![](./ctx_contact_oled.png) | ![](./ctx_contact_eink.png) |

| Item                         | Action                                                                         |
| ---------------------------- | ------------------------------------------------------------------------------ |
| Mark as read                 | Clears unread counter for this contact                                         |
| Notif: Default / OFF / ON    | Per-contact notification override — **LEFT/RIGHT** or **Enter** to cycle                    |
| Melody: Global / M1 / M2     | Per-contact melody override — **LEFT/RIGHT** or **Enter** to cycle                          |
| Fav: ON / OFF                | Mark this contact as a favourite — **LEFT/RIGHT** or **Enter** to toggle                    |
| Pin to dial / Unpin (slot N) | Pin this contact to a Favourites Dial slot; if already pinned shows which slot |

**Fav** and **Pin to dial** are separate. **Fav** is the starred flag shared with the companion app: it marks the row with a ★, sorts it above the rest of the list (unless **Settings › Contacts › Favs top** is off), and drives the `DMs = Fav` list filter. **Pin to dial** puts the contact on the [Favourites Dial](../favourites_dial/favourites_dial.md) page and changes nothing about the list.

When **Pin to dial** is selected, a slot picker opens (Slot 1–6 showing current occupant name or "empty"). Choosing a slot that already holds another contact moves the new contact there.

In the **Rooms** list the context menu instead offers:

| Item          | Action                                                                       |
| ------------- | ---------------------------------------------------------------------------- |
| Login…        | Opens the password prompt to (re-)log in to this room (see Rooms — logging in) |
| Logout        | Only shown once logged in. Forgets the saved password so the next open prompts for one again |
| Fav: ON / OFF | Mark this room as a favourite — **LEFT/RIGHT** or **Enter** to toggle; drives the **Rooms = Fav** list filter |
| Pin to dial / Unpin (slot N) | Pin this room to a [Favourites Dial](../favourites_dial/favourites_dial.md) slot |

---

### Context menu — channel list

|           OLED            |           E-Ink           |
| :-----------------------: | :-----------------------: |
| ![](./ctx_channel_oled.png) | ![](./ctx_channel_eink.png) |

**Hold Enter** on a channel entry opens a context menu:

| Item                      | Action                                                                |
| ------------------------- | --------------------------------------------------------------------- |
| Mark all read             | Clears all unread for this channel                                    |
| Notif: Default / OFF / ON | Per-channel notification override — **LEFT/RIGHT** or **Enter** to cycle           |
| Melody: Global / M1 / M2  | Per-channel melody override — **LEFT/RIGHT** or **Enter** to cycle                 |
| Fav: ON / OFF             | Add or remove this channel from favourites — **LEFT/RIGHT** or **Enter** to toggle |
| Scope: <name>             | **Enter** opens a picker over the shared scope list (Settings › Radio › Scope) — `*` sends this channel unscoped, any named scope tags its flood traffic with that region. Each channel keeps its own pick, matching the phone app's per-channel region picker. |
| Pin to dial / Unpin (slot N) | Pin this channel to a [Favourites Dial](../favourites_dial/favourites_dial.md) slot |
| Edit                      | Opens the Add/Edit form below, pre-filled with the channel's name    |
| Delete                    | Removes the channel — confirms first (defaults to Cancel)             |

---

### Adding / editing a channel

Joining or creating a community channel no longer needs the phone app. The **Channels** list ends with **"+ Add channel"** — **Enter** picks a channel type; **Edit** from the context menu changes an existing channel's name/secret directly, skipping the type picker.

**+ Add channel** first asks which type to create — the same three the phone app offers:

- **Public** — instantly re-adds the well-known default public channel, no fields needed. Useful if it was deleted and you don't remember its key.
- **Hashtag** — type a topic (e.g. `test`); name (`#test`) and secret (first 16 bytes of `sha256("#test")`) are both derived from it. A topic-based public chat — anyone typing the same topic elsewhere lands on the same channel, separate from the default Public one.
- **Private** — the manual Name + Secret form:

  | Field  | Notes                                                                                        |
  | ------ | ---------------------------------------------------------------------------------------------- |
  | Name   | Up to 31 characters                                                                            |
  | Secret | **LEFT/RIGHT** toggles between two entry modes; **Enter** opens the keyboard for whichever is selected |

  - **Passphrase** (default) — type any text; the device hashes it to the channel's 16-byte secret. Easiest to agree on verbally — same idea as a room password.
  - **Hex key** — the exact 32-hex-character secret (channel QR code format, see [QR Codes](../../qr_codes.md)), for joining with a secret you were given rather than a new passphrase. An all-zero secret (`00…0`) is rejected — reserved internally for an empty slot.

  Select **[Save]** to commit. The secret can't be redisplayed once saved (only the derived key is kept) — editing later means typing a new one, same as re-logging into a room.

---

### Mark all read

**Hold Enter** on the DM / Channels / Rooms mode-select screen to clear all unread counters for the highlighted category at once.
