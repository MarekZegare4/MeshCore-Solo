# One button

A board with nothing but its own button runs the whole interface from it: a
bare Heltec V3 or V4 (the `_button` builds, which use the PRG button) and the
GAT562 Watch13. The screens are the same as on a joystick device
([Getting started](./getting-started.md)); only the way you move through
them changes.

What one button can't do comfortably (typing long messages, putting Home's
pages in order, writing a melody) the
[configurator](./configurator.md) does over USB.

## Presses

| Press | Does |
| ----- | ---- |
| **Click** | The next row. At the end of a list or a tab, on to the next tab or page |
| **Double click** | Back. On the home screen: the page before |
| **Hold** (about half a second) | Enter: open, select, confirm. On a value: edit it (below) |
| **Hold 2 s** | The options menu (Hold Enter on a joystick device). On the lock screen: unlock |
| **Hold 5 s** | The [SOS](./tools.md#sos) view (Hold Back on a joystick device) |

A click is only a click once the double-click window (0.3 s) has passed, so
the screen answers a moment after you let go. A held press shows when it has
become a hold; let go then.

The Watch13 has a second button: **Back**, the same as a double click, and
the way out of editing a value.

## Values

On a row with a value of more than two choices (brightness, the screen-off
time, a number) a hold edits it in place. A **-/+** box in the bottom corner
shows you are editing:

| Press | Does |
| ----- | ---- |
| **Click** | The next value (+1) |
| **Double click** | The value before (−1) |
| **Hold** | Done |

A switch (on / off) or an action takes the hold as Enter, as before.

## Digits

Numbers set digit by digit (a frequency, the alarm time, the timer, a
waypoint's coordinates):

| Press | Does |
| ----- | ---- |
| **Click** | The digit +1 |
| **Double click** | The digit −1 |
| **Hold** | The next digit; past the last one, done |
| **Hold 2 s** | Cancel |

## Text

The on-screen keyboard becomes a strip along the bottom of the screen, one
item at a time in the box, with its neighbours either side:

`abc def ghi jkl mno pqrs tuv wxyz spc .,?!'- 0-9 Aa del OK`

- **Click** moves along the strip, **double click** back.
- **Hold** on a group (`abc`) opens it: its letters are on the strip now.
  Click to the one you want and **hold** to type it; the group closes again.
- **Hold 2 s** inside a group goes back to the groups; outside one, it
  cancels the text.
- `Aa` makes the next letter a capital, `spc` is a space, `del` deletes the
  last letter, `OK` is done.

For anything longer than a few words, set up
[quick messages](./messages.md#writing) in the configurator: a
conversation's **+ Send** offers them first.

## PIN

A PIN (the lock screen, a new PIN in Settings) is a combination lock: the
strip is `0`–`9`, `del` and `OK`. Click to the digit, hold to enter it, hold
on `OK` when done.

## Screen by screen

- **Home**: a click walks the rows of a quick panel, the Favourites tiles one
  by one, then on to the next page; a double click is the page before. Their
  order: the configurator's **Home screen** tab.
- **Lists with tabs or filters** (Nearby, the Bot, remote admin, the trail):
  a click walks the list, then the next tab.
- **A message opened full screen**: a click scrolls it, then on to the next
  message.
- **Ringtone editor**: a click moves to the next note (round to the first);
  a hold edits it: click a step up the scale (a rest, then C4 to B6), double
  click a step down, hold done. A hold on the `+` adds a note. A double click
  opens the menu, with the length, the tempo, **Save & Exit** and
  **Discard**; a hold on Duration or BPM changes it. Or write the melody in
  the configurator.
- **Clock tools**: the alarm and the timer are digit editors (above); the
  stopwatch starts and stops with a hold, a click resets it.
- **Lock screen**: any press wakes the screen; **hold 2 s** unlocks (with a
  PIN set, the PIN strip comes up).

> [!NOTE]
> Plug in a [CardKB](./hardware.md#cardkb) and its arrows, Enter and Esc
> drive the device as on a joystick device, with the normal keyboard; the
> button keeps its presses.
