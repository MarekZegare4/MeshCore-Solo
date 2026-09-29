# Navigation

Everything here works from the device's own GPS; no magnetometer or extra
hardware is needed. Distances and speeds follow Settings › System › Units
(metric or imperial).

## Trail

**Tools › Trail** records your route in the background while you use the rest
of the device (a blinking **G** in the status bar). Straight stretches are
stored as their two ends, so the 512 points cover a long route. **Left / Right**
switches between the **Summary** (distance, time, speed or pace), the **Map**
(your route, waypoints, people sharing their position, the target) and the
point **List**.

**Hold Enter** for the trail menu: start / stop, mark a waypoint, the waypoint
list, **Track back** (retrace the route to where it started), share your
position, the trail file, and the trail settings:

- **Min dist**: how far apart points are recorded.
- **Auto-pause**: pauses the trail after you stop for 1–5 minutes and resumes
  when you move, so breaks don't count.
- **Mark avg**: averages the GPS for 5–30 seconds when marking a waypoint.
- **Auto-save**: saves the trail when the device powers off, including a
  low-battery shutdown.

The trail lives in memory: save it (Trail file › Save) or turn on Auto-save
to keep it through a reboot.

### GPX export

Connect USB, open the [USB tools](https://solo.marekzegarek.com/#pc-tools) on the Solo site in
Chrome or Edge and click **Connect device**, then on the device choose
**Trail file › Export**. The GPX includes your waypoints. Without a browser,
`uv run tools/trail_export.py` does the same. Disconnect the phone app first if
it's connected over USB.

> [!NOTE]
> **Wio Tracker L2:** the trail holds 4096 points and is drawn on the map.
> Its controls (start / stop, save, load, track back, reset) are in the map's
> **Map tools**, the settings in Settings › Map. **GPX** writes the trail to
> `trails/` on the SD card; take it off with the card as a USB drive
> ([Hardware](./hardware.md)) and open it in the USB tools for a preview.

## Waypoints

A waypoint is a saved spot (the car, a camp, water) with a short label, up to
16 of them, kept through reboots. Add one with **Mark here** at your position,
or **+ Add by coords** to type coordinates. The list starts with **Trail
start**, so you can always go back to where the trail began.

**Hold Enter** on a waypoint: Rename, Delete, **Send** (as a message to a
contact or channel) and **Set as target**.

> [!NOTE]
> **Wio Tracker L2:** up to 64 waypoints. The pin button on the map opens the
> list; hold a spot on the map to make it a waypoint or the target.

## Navigating to something

**Navigate** on a waypoint, a node in Nearby or a location in a message opens
the navigation view: the distance, the bearing **To** the target and your own
heading (**Hdg**), with the time to arrival once you're getting closer. Turn
until the two bearings match. The heading comes from your movement, so it
shows `--` while you stand still.

**Tools › Compass** shows the same heading as a scrolling tape with the
degrees and direction.

> [!NOTE]
> **Wio Tracker L2:** the target is drawn on the map with a line to it, and a
> bar with distance, bearing, heading and time to arrival. The Compass app is
> a turning dial.

## Sharing locations

- **Once**: **Share my pos** in the trail menu sends your position to a
  contact or channel you pick.
- **A waypoint**: **Send** in the waypoint menu.
- **Live**: **Tools › Live Share** sends your position while you move, to one
  contact or channel, for 1 to 12 hours. It only sends after you've moved
  (50–500 m) and never more often than you set; an optional heartbeat repeats
  it while you stand still.

With **Track loc** on in Live Share, other people's shares show on the map
and in Nearby, with live distance and bearing.

Locations travel as ordinary text (`[LOC]lat,lon`, or `[WAY]lat,lon label`
for a waypoint), readable in the phone app and on other firmware. On the
receiving side, **hold Enter** on the message to navigate to it, save it or
set it as the target.

## Locator

**Tools › Locator** alerts you when you cross a circle around a target:
arriving at a waypoint, leaving it, or a person coming near or moving away.

- **Target**: a waypoint (a fixed place) or a contact (follows their live
  share, else their last known position).
- **Radius**: 50 m to 1 km.
- **Mode**: alert on arriving, leaving, or both.
- **Beeper**: ticks faster as you get closer, even when the sound is muted.

**Set as target** in Nearby, the waypoint list or a message sets the target
in one step. The target shows as a flag on the map.

## Map

The **Map** home page shows your position, the trail, waypoints, people
sharing their position and the target. **Enter** opens the trail map;
**hold Enter** shares your position.

> [!NOTE]
> **Wio Tracker L2:** a real map with offline tiles on the SD card. Drag to
> pan, +/− to zoom, the crosshair follows your GPS again.
>
> - **Download**: frame an area on the map and download its tiles over WiFi.
>   Downloaded areas can be renamed, refreshed or deleted.
> - **Live tiles**: with WiFi on, tiles for where you look are fetched and
>   cached, up to the size set in Settings › Storage.
> - **Vector regions**: whole regions as packs made with
>   `tools/maps/osm_vector.py`, copied to the card.
>
> The minimap on the home screen shows your surroundings at a glance; tap it
> for the full map. The **Nodes** map shows every node with a position.
