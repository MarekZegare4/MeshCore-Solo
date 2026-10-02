# Screen lock

The lock keeps pocket presses from doing anything. It locks the screen only:
messages still arrive, alarms still ring and the phone app still connects.

## Locking and unlocking

**Hold Back and press Enter three times** within 3 seconds; the same locks
and unlocks. With a CardKB, **Fn+Esc** does it in one press.

**Settings › Display › Lock screen** locks the device whenever the screen
turns off by itself.

The lock screen has the status bar on top and one of two looks, set in
Settings › Display › **Lock clock**:

- **Big**: the time in large digits, the date, and how many messages are
  unread.
- **Compact**: a smaller time with the date beside it, and the Clock page's
  fields as rows below (unread messages too, if no field shows them).

The key that wakes the screen does nothing else, so a glance shows the data.
A key pressed on the lit lock screen brings up the unlock hint at the bottom
for a few seconds; it counts the presses. On e-ink the hint stays up.

> [!NOTE]
> **Wio Tracker L2:** with **Lock screen** on (Settings › Display), waking
> the screen shows a clock card with **slide to unlock**. Settings › Display
> › **Tap to wake** decides whether a tap wakes it or only the top button.

## PIN

**Settings › Display › Lock PIN** adds a PIN to unlocking, asked also right
after power-up.

- Enter on the row opens a number pad. Type the PIN (at least 4 characters),
  confirm, then type it again. The pad's keyboard key switches to letters.
- A wrong PIN shows how many tries are left; after 5 in a row, entry pauses
  for 30 seconds.
- Enter on the row again removes the PIN.

The PIN is stored as a salted hash, never as the PIN itself.

> [!NOTE]
> **Wio Tracker L2:** **Settings › Display › Screen PIN**, digits only. With a PIN, the lock card comes up every time the screen wakes, and
> the SD card isn't offered as a USB drive until it's unlocked.

## Magnetic cover

A Hall or reed sensor wired to a free pin (`PIN_HALL_SENSOR`, see
[Build flags](./developer/build-flags.md)) locks and blanks the screen when a
magnetic cover closes and unlocks it when it opens (asking for the PIN, if
one is set).
