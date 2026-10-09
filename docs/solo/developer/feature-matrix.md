## Feature matrix

[Go back](../README.md)

What each kind of screen gets. Solo has two UIs: **ui-new** (buttons, every
board but the L2) and **ui-lvgl** (touch, the Wio Tracker L2). Within ui-new,
some content depends on the screen's size and kind, the rest on the hardware
(build flags).

Keep this up to date: a change that adds a feature to only one UI, or only
to some screens, adds or corrects its row here in the same commit.

### Screen groups

| Column | Boards | Logical size, font | Notes |
| --- | --- | --- | --- |
| **Small** | Wio Tracker L1 (OLED), Heltec V3/V4, GAT562 30S and Watch13, ProMicro, Cardputer ADV (TFT), T-Echo Lite (e-ink) | 128 × 64, 6 × 9 | The T-Echo Lite is e-ink, so it also gets what's marked *e-ink* |
| **E-ink L** | Wio Tracker L1 e-ink, landscape | 250 × 122, 8 × 13 | Larger icons |
| **E-ink P** | Wio Tracker L1 e-ink, portrait | 122 × 250, 6 × 9 | |
| **E-ink 4.2"** | Wio Tracker L1 with the 4.2" panel (parked) | 400 × 300, 9 × 15 | Its own icon set |
| **L2** | Wio Tracker L2 | 320 × 240 touch, ui-lvgl | |

What decides it in ui-new (look for these when adding a screen):

| Test | Where it's true |
| --- | --- |
| `Features::IS_EINK`, `display.isEink()` | E-ink L, E-ink P, E-ink 4.2", T-Echo Lite |
| `expandHistory()`: 9 or more lines | E-ink L, E-ink P, E-ink 4.2" |
| `height() >= 18 * lineHeight` | E-ink P, E-ink 4.2" |
| `width() >= 40 * charWidth` ("wide") | E-ink 4.2" |
| `isLandscape()` | Small, E-ink L, E-ink 4.2" |

Cells: **yes**, **—** (not there), a short note where it differs, or the
build flag it needs.

### Messages

| Feature | Small | E-ink L | E-ink P | E-ink 4.2" | L2 |
| --- | --- | --- | --- | --- | --- |
| Channels, direct, rooms | yes | yes | yes | yes | yes, one screen with sections |
| Opens on the first unread, "New" line | yes | yes | yes | yes | yes |
| Bubbles | 2-line boxes | whole text | whole text | whole text | whole text |
| History kept | 48 ch / 32 DM | same | same | same | 256 / 128 in memory, all on SD (`HIST_ARCHIVE`) |
| Path of a message | Path list | same | same | same | path diagram on the card |
| Quick messages, placeholders | yes | yes | yes | yes | yes |
| Bytes left while typing | yes | yes | yes | yes | yes |
| Keyboard | letter grid or T9 | same | same, taller keys | same, taller keys | phone-style touch |
| Cursor mode | yes | yes | yes | yes | — (touch places the cursor) |
| Accents | Hold Enter on the letter | same | same | same | hold the key |
| Message alert setting (banner / corner / off) | yes, no blink on e-ink | yes, no blink | yes, no blink | yes, no blink | banner on / off, tap to open |

### Contacts and home

| Feature | Small | E-ink L | E-ink P | E-ink 4.2" | L2 |
| --- | --- | --- | --- | --- | --- |
| Home | pages, Left / Right | same | same | same | swiped pages, apps six to a page |
| Favourites | 6 slots, 3 columns | 3 columns | 2 columns | 3 columns | favourite chats page |
| Quick panel | per-page quick rows | same | same | same | pull-down panel |
| Nearby nodes | list | list, direction arrows | list, direction arrows | list + detail pane | Nodes app |
| Map of nodes | yes (Nearby › Map) | yes | yes | yes | yes, on the tile map |
| Contact details | yes | + compass rose, path hops | + compass rose, path hops | + compass rose, path hops | card with actions |
| Contact expiry / prune | yes | yes | yes | yes | yes |
| Remote admin | yes | yes | yes | yes | yes |

### Navigation (`ENV_INCLUDE_GPS`)

| Feature | Small | E-ink L | E-ink P | E-ink 4.2" | L2 |
| --- | --- | --- | --- | --- | --- |
| Trail | 512 points, simplified | same | same | same | 32 768 points, copy on SD |
| Saved trail view | yes | yes | + map and elevation profile | + map and elevation profile | on the map, profile view |
| GPX | over USB (USB tools) | same | same | same | to `trails/` on SD |
| Waypoints, averaging | yes | yes | yes | yes | yes, 64 |
| Target, navigation view | yes | yes | yes | wide layout | on the map + Compass app |
| Live sharing, locator | yes | yes | yes | yes | yes |
| Map | position, trail, target | same | same | same | offline tiles on SD, download over WiFi |

### Tools and status

| Feature | Small | E-ink L | E-ink P | E-ink 4.2" | L2 |
| --- | --- | --- | --- | --- | --- |
| Clock tools (alarm, timer, stopwatch) | yes, tenths on the stopwatch | no tenths | no tenths | wide layout | Clock app |
| Calendar | Clock tools (tight grid) | Clock tools (tight grid) | + under the clock, on the lock screen | + under the clock | in the Clock app |
| Remote bot, repeater, auto-advert | yes | yes | yes | yes | yes |
| Melodies (`PIN_BUZZER`) | Ringtones tool | same | same | wide editor | Settings › Sound |
| Status: Radio, GPS, Power, Mesh | yes, 32 min history | yes, 8 h charts | yes, 8 h charts | tiles with charts | Diagnostics app (+ noise chart) |
| Sky view (`GPS_SKYVIEW`) | Status › Sky | same | same | + side panel | GPS app: Sky, Signal, Details |
| Battery curve | Settings | same | same | same | Settings › Power, draggable chart |
| QR code of the manual | Status › System, Enter | same | same | same | Settings › About |

### Device

| Feature | Small | E-ink L | E-ink P | E-ink 4.2" | L2 |
| --- | --- | --- | --- | --- | --- |
| Screen lock, PIN | yes | yes | yes | yes | yes, slide to unlock |
| Magnetic cover (`PIN_HALL_SENSOR`) | yes | yes | yes | yes | — |
| Brightness | yes (OLED, TFT) | — | — | — | yes |
| Rotation, joystick rotation, full refresh | — | yes | yes | yes | — |
| Power off | Shutdown page (hibernate) | same | same | same | Settings › System |
| Firmware update | over USB | same | same | same | over WiFi from GitHub |
| Screenshot (`ENABLE_SCREENSHOT`) | over USB | same | same | same | side + top button to SD, or over USB |
| SD card, USB drive | — | — | — | — | yes |

### Hardware add-ons (ui-new only)

| Feature | Flag | Boards |
| --- | --- | --- |
| CardKB | `CARDKB_I2C` or `ENV_PIN_SDA` / `ENV_PIN_SCL` | any ui-new board with free I2C |
| GPIO tool, `!gpio` | `PIN_GPIO1`..`PIN_GPIO4` | any ui-new board with free pins |
| Wired joystick | `UI_HAS_JOYSTICK` | Heltec V3/V4 (and any board wired for it) |
| Vibration | `PIN_VIBRATION` | GAT562 Watch13 |

### Gaps

Things one side has that the other could:

- **L2 → ui-new**: history on an SD card (no card on the L1); a base map
  under the trail and the nodes (no card either, but the L1 has 2 MB of QSPI
  flash: a lite vector pack is being measured).
- **ui-new → L2**: T9 layout; CardKB; GPIO on the Grove port; the magnetic
  cover (no sensor).
- **Small screens**: the e-ink charts (RAM: 8 h of history).
