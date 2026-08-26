## Screen Lock

[Go back](../../../README.md)

### Overview

|           OLED            |           E-Ink           |
| :-----------------------: | :-----------------------: |
| ![](./overview_oled.png) | ![](./overview_eink.png) |

Screen lock prevents accidental keypresses. While locked the display turns off and all input is ignored.

---

### Locking and unlocking

**Hold Back** and press **Enter** three times within 3 seconds. The sequence works in both directions — the same combination locks and unlocks.

On boards with a CardKB attached, **Fn+Esc** does the same thing in one press. Esc rather than the adjacent Backspace — those two keys sit next to each other on CardKB's layout and would be too easy to hit by accident.

If the display is off when the sequence begins, it turns on automatically so the hint is visible. Each press in the physical sequence extends the display-on timer by 5 seconds.

The hint popup at the bottom of the lock screen guides through the physical sequence:

| Step           | Hint                                                        |
| -------------- | ------------------------------------------------------------ |
| Not started    | _Hold Back + 3×Enter_ (_Back+3xEnter/Fn+Esc_ with CardKB attached) |
| 1 press done   | _Enter ×2 more…_                                              |
| 2 presses done | _Enter ×1 more…_                                              |

If no press is made for 3 seconds, the counter resets.

---

### Lock screen

|           OLED            |           E-Ink           |
| :-----------------------: | :-----------------------: |
| ![](./screen_oled.png) | ![](./screen_eink.png) |

A brief press of any button wakes the display and shows the lock screen. It displays:

- **Time** — large, same format as the Clock page (24 h / 12 h from Settings)
- **Date** — day-of-week, day, month
- **Two sensor values** — the first two Dashboard Config fields (same values configured for the Clock page); shown side by side if both are set

The display turns off again automatically after 5 seconds of inactivity (or 2 seconds immediately after locking).

---

### Auto-lock

Enable **Auto-lock** in **Settings › Display** to lock the device automatically whenever the display turns off due to auto-off timeout.

---

### Magnetic cover (Hall sensor)

Optional, user-supplied hardware — no board in this repo has one built in. Wire a Hall-effect or reed sensor to any free GPIO, then set `PIN_HALL_SENSOR` (and `HALL_ACTIVE_HIGH=1`, if your module pulls the pin high rather than low when the magnet is present) as `build_flags` in your own env. No-op entirely unless `PIN_HALL_SENSOR` is defined.

Fully autonomous, independent of Auto-lock and of any key combo:

- **Magnet near (cover closed)** — locks and blanks the display immediately, no wake grace.
- **Magnet away (cover opened)** — unlocks and wakes the display right away.
