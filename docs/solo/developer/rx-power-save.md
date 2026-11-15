# RX power save

[Go back](../README.md)

**Power save** (Settings > Radio; `rx_powersave` in the prefs) lets the
LoRa radio sleep between short listens instead of listening all the time.
Listening is most of what a companion's radio does, so this is where its
current goes when nobody transmits. This page explains how it works, why it
was switched off in June 2026, what changed, and the tests behind turning it
back on. SX1262 radios only: the Wio Tracker L1 and L2, the Heltec V3/V4 and
others. Other radios keep listening all the time, whatever the setting.

## How it works

The SX1262 can run a receive duty cycle on its own (SetRxDutyCycle,
datasheet 13.1.7). It listens for a while, sleeps for a while, and repeats.
When it hears a preamble it stays awake for the packet, which then arrives
the normal way. The microcontroller does nothing in between.

Every LoRa packet starts with a preamble, a run of identical symbols the
receiver locks on to. The sleep has to be short enough that a preamble
starting just as a listen ends is still running when the next listen
begins, with enough of it left to latch. So the length of the sleep follows
from the shortest preamble the senders around you use:

| Firmware of the sender | Preamble at SF8 and below | SF9 and above |
| --- | --- | --- |
| companion v1.16 (March 2026) and later, Solo | 32 symbols | 16 symbols |
| before v1.16 | 16 symbols | 16 symbols |

That gives the two settings:

- **On** assumes 16 symbols, so it hears every node, old or new.
- **Max** assumes what v1.16 and later send (`preambleLengthForSF()`). It
  sleeps about twice as long at SF8 and below, and misses nodes on older
  firmware. At SF9 and above it is the same as On.

Power save is forced off while the device repeats, because a repeater has
to hear everything.

## The timing

The periods come from IoTThinks' PowerSaving firmware
([RXPowerSaving](https://github.com/IoTThinks/MeshCore/tree/PowerSaving-v18),
its guarded "balanced" level). They measured it on several SX1262 boards.
`RadioLibWrapper::rxPsGeometry()` computes:

- **Sleep** is the expected preamble minus 10 symbols, minus 0.2 of a
  symbol. A worst-case preamble then has 10 symbols left in the next listen,
  and the chip needs about 6 to latch. The 0.2 keeps the register values
  away from pairs IoTThinks found to latch a preamble and then lose the
  header.
- **Listen**: on a preamble the chip re-times itself to twice the listen
  plus the sleep. That has to cover the preamble, the sync word and the
  8-symbol header, plus a symbol of slack. This is the datasheet's
  condition, and it sets the listen window.
- No duty cycle is used where the sleep would come within 250 µs of the
  chip's wake-up time, which is the TCXO start-up (5 ms) plus 1 ms. Below
  that the SX1262 arms without an error and then hears nothing. The radio
  keeps listening continuously at such settings; the L2's Radio page says
  "Not at these radio settings".

What it comes to, with the sleep including the 6 ms wake-up:

| Settings | On: sleep / listen, time asleep | Max |
| --- | --- | --- |
| SF7 / 62.5 kHz | 11.9 / 27.0 ms, 31 % | 44.6 / 27.0 ms, 62 % |
| SF8 / 62.5 kHz | 23.8 / 51.0 ms, 32 % | 89.3 / 51.0 ms, 64 % |
| SF7 / 125 kHz | (too short: continuous) | 22.3 / 15.0 ms, 60 % |
| SF9 / 125 kHz | 23.8 / 51.0 ms, 32 % | as On |
| SF10 / 250 kHz | 23.8 / 51.0 ms, 32 % | as On |
| SF11 / 125 kHz | 95.0 / 195.1 ms, 33 % | as On |
| SF12 / 125 kHz | 190.0 / 387.2 ms, 33 % | as On |

## Why it was off, and what changed

The first version (June 2026) lost nearly everything in the field. It was
switched off in September with the preamble blamed. There were two causes:

1. **Talking to a sleeping radio.** A check run on every pass of the main
   loop asked the radio whether it was receiving
   (`noiseFloorCalibCheck()`, before testing its once-a-minute interval).
   Any SPI command wakes a sleeping SX1262 into standby, where it hears
   nothing until it is set to receive again. The Wio Tracker L1's loop runs
   about 1000 times a second, so the radio was deaf nearly all the time.
   This was the main cause.
2. **The preamble assumed.** The sleep was sized for 32-symbol preambles
   (RadioLib's `startReceiveDutyCycleAuto`), so nodes sending 16 were
   caught only some of the time.

Now:

- The interval is tested first.
- `isReceivingPacket()` and `getCurrentRSSI()` look at the BUSY pin, which
  is high while the chip sleeps, before asking it anything.
- A packet's SNR and RSSI are read as it is taken off the chip, so screens
  showing them no longer ask the radio.
- The watchdog re-arms a chip that has stopped cycling after 3 s instead of
  60 s.
- Changing the radio settings re-arms the duty cycle with periods for the
  new settings.

## The tests

The test used a Wio Tracker L1 and a Wio Tracker L2 with test builds, on a
desk a metre apart, at SF8 / 62.5 kHz / CR 4/8, on 15 November 2026. One
device sent direct messages, a second or so apart. The other had power save
on and counted what arrived. Older nodes were played by the sender with its
preamble cut to 16 symbols (`txpre 16`). "Asleep" is the share of a second
the receiver's BUSY pin was high, sampled every 0.1 ms.

| Receiver | Sender's preamble | Arrived | Asleep |
| --- | --- | --- | --- |
| Off | 32 | 15 / 15 | 0 % |
| On | 16 (older node) | **60 / 60** | 33 % |
| On | 32 | **45 / 45** | 31–33 % |
| Max | 32 | **60 / 60** | 63–64 % |
| Max | 16 (older node) | 19 / 30 | 63 % |

The messages went both ways: L2 to L1, then L1 to L2 with 30 per case, then
L2 to L1 again on the final firmware. On held every message from both kinds
of sender. Max held every message from v1.16 senders and lost about a third
from older ones, as expected. That last row is also the proof that the test
can see a loss. In every case the receiver's packet count rose by exactly
the number of messages, so no repeater echoed a message and hid a loss. The
watchdog stepped in once in all of it, during the deliberate loss.

The "asleep" figures match the computed ones above (32 % and 64 %).

The device test suites passed with it all built in: L1 26/26, L2 51/51 over
two rounds.

Not tested yet:

- **The current itself.** No meter was at hand. The saving is inferred
  from the time asleep, so On should cut the radio's receive current
  (about 5 mA on an SX1262 in boosted gain) by roughly a third, and Max by
  roughly two thirds.
- **Weak signals and busy channels.** The test was a metre apart with
  strong signals. A preamble heard only partly, at the edge of range, has
  less margin. This wants a field test on battery.

## Repeating the test

1. Flash both devices with test builds: `uv run tools/hwtest.py --flash <env>
   --port <port>`, once for each device.
2. Put both on the same radio settings.
3. Run `uv run tools/rxps_trial.py <sender port> <receiver port> [N]
   [cases]`.

The test hooks it drives (`ui-core/TestHooks.h`):

- `rxps [0|1|2]` sets the mode without saving it. Alone it reports the
  mode, whether the radio is armed, the time asleep, the watchdog counts
  and the packets received.
- `txpre <symbols>` sends with that preamble until the radio is set again.

## Possible next steps

- **TCXO start-up.** RadioLib waits 5 ms for the TCXO on every wake;
  IoTThinks measured 150–400 µs to be enough on the boards they tried. A
  shorter wait would cut the time the radio spends waking. It would also
  let On work at SF7 / 125 kHz, where the sleep is now too short to use.
