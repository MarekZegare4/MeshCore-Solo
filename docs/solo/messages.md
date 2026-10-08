# Messages

Messages holds three lists: **Channels**, **Direct** messages and **Rooms**.
Unread counts show on each conversation, on the list and on the home screen.

> [!NOTE]
> **Wio Tracker L2:** one screen with all three as sections; tap a section
> title to fold it. **New** in the header starts a conversation with any
> contact.

## Reading

Open a conversation to see its history as chat bubbles: yours on the right,
received ones on the left, newest at the bottom. Each bubble shows its age
and a small hop count: on a received message, how many repeaters it came
through; on your own, how many repeaters were heard passing it on. In
channels and rooms it also names the sender.

**Enter** on a message opens it full screen; **Left / Right** there pages to the
older and newer one.

**Hold Enter** on a message for its options:

- **Reply**: starts a message addressed to the sender (`@[name]`).
- **Navigate**, **Save waypoint**, **Set as target**: when the message
  contains a location (see [Navigation](./navigation.md)).
- **Path** on a received message: the repeaters it came through.
  **Relayed by** on your own channel post: the repeaters heard repeating it.

> [!NOTE]
> **Wio Tracker L2:** hold a bubble. The card shows when it was sent, the hop
> count, the path as a diagram, and **Reply** / **Set target**.

### How much is kept

The device keeps the newest 48 channel messages and 32 direct messages, all
conversations together. When a busy conversation pushes out messages you
haven't read, its unread count gets a **+** (for example `48+`).

> [!NOTE]
> **Wio Tracker L2:** 256 channel and 128 direct messages in memory, and with
> an SD card every conversation is also saved to the card (100 to 2000
> messages each, Settings › Storage). The history survives a reboot and
> scrolls back through everything on the card.

## Writing

Open a conversation and choose **+ Send** (or Enter at the bottom of the
history). Pick a quick message or **Custom message** for the keyboard.

- **Quick messages**: ten of your own, edited in Settings › Messages.
- If a message can't be sent, you stay where you were: the keyboard keeps
  the text (its label reads **Not sent**), so you can send it again.
- **Placeholders** fill in live data when the message is sent:
  `{loc}` (your GPS position), `{time}`, `{batt}`, and the readings of any
  sensor the device has: `{temp}`, `{hum}`, `{pres}`, `{alt}`, `{lux}`,
  `{dist}`, `{co2}`.
- **How much is left:** a message holds 160 bytes, less what the send adds
  (a channel post carries your name and ": "). Within the last 40 the
  keyboard shows the bytes left, counting placeholders as they will be
  filled in. An accented or Cyrillic letter takes two bytes. The keyboard
  stops taking letters at the limit.

> [!NOTE]
> **Wio Tracker L2:** the compose bar sits under the conversation; its **+**
> opens quick messages and placeholders. Quick messages are edited in
> Settings › Messages & contacts.

### The keyboard

The on-screen keyboard is a letter grid (or a phone-style T9 keypad, Settings ›
Keyboard › Layout). Up from the top row moves the cursor through the text.

Accented letters don't need a language setting: **hold Enter** on the base
letter and pick from its variants, for example `a` → `á à â ä å ą…`, `z` →
`ź ż ž`. This covers Polish, Czech, German, French, Spanish, Nordic and the
other European languages written in Latin.

Settings › Keyboard picks two scripts, **Main** and **Additional**, from Latin,
Cyrillic and Greek; the keyboard's **#@/abc** key cycles between them and
symbols. Cyrillic includes the Ukrainian, Belarusian and Serbian letters
(under the key they sit on). Received messages in any of these scripts display
correctly whatever the keyboard is set to.

> [!NOTE]
> **Wio Tracker L2:** a phone-style keyboard. Hold a key for its accents; the
> globe key switches between the two alphabets.

## Channels

**Hold Enter** on a channel for its options:

| Option | Does |
| ------ | ---- |
| Mark all read | Clears its unread count |
| Notif, Melody | Its own notification and sound, instead of the global ones |
| Fav | Marks it as a favourite (shared with the app) |
| Scope | The region its messages are tagged with: **Default** follows the list's default, `*` sends none; the list is in Settings › Radio › Scope |
| Pin to dial | Puts it on the Favourites page |
| Edit, Delete | Renames it or changes its secret; removes it |

**+ Add channel** at the end of the list adds:

- **Public**: the default public channel, if you deleted it.
- **Hashtag**: a topic such as `#hiking`. Anyone who types the same topic
  joins the same channel.
- **Private**: a name and a secret, typed as a passphrase or as the 32-digit
  hex key (the format of channel QR codes).

> [!NOTE]
> **Wio Tracker L2:** hold a channel, or tap ⚙ in an open channel, for its
> options.

## Direct messages

**Hold Enter** on a contact: Mark as read, Notif, Melody, Fav, Pin to dial.
**Fav** is the star shared with the app and sorts favourites to the top;
**Pin to dial** only puts the contact on the Favourites page.

## Rooms

Opening a room logs in first. You type the password once (empty if the room
has none); it's saved on the device, including passwords set from the app,
so the next time it logs in without asking. A wrong password is forgotten and
you're asked again. **Hold Enter** on a room offers **Login…** and **Logout**.
