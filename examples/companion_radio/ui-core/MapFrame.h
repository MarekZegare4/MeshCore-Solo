#pragma once
// What a map frames, the same on every map: ui-new's Trail map, Nodes map and
// Home mini map, the L2's Navigation map.
//
// A map is either fitted to a view (NodePrefs::map_view, kept) or at a fixed
// scale (Manual: chosen with Up/Down or +/-, forgotten on leaving the map).
//   All        the trail, waypoints, people sharing their position, the route
//              followed, the target, you
//   Trail      the trail and the route followed, you
//   People     the people sharing their position and the target, you
//   Waypoints  the waypoints and the target, you
// A view with nothing of its own to show (People with nobody sharing) falls
// back to All. Manual centres on you (the Nodes map: on the node selected).
//
// Standalone header: no MeshCore or display dependencies.

#include <stdint.h>

namespace mapframe {

enum View : uint8_t { ALL, TRAIL, PEOPLE, WAYPOINTS, VIEW_COUNT };

inline const char* viewName(uint8_t v) {
  static const char* N[VIEW_COUNT] = { "All", "Trail", "People", "Waypoints" };
  return N[v < VIEW_COUNT ? v : uint8_t(ALL)];
}

// What goes into the box (your own position always does).
struct Fit { bool trail, waypoints, people, route, target; };

inline Fit fit(uint8_t view, bool any_trail, bool any_waypoints, bool any_people) {
  switch (view) {
    case TRAIL:     if (any_trail)     return { true,  false, false, true,  false }; break;
    case PEOPLE:    if (any_people)    return { false, false, true,  false, true  }; break;
    case WAYPOINTS: if (any_waypoints) return { false, true,  false, false, true  }; break;
    default: break;
  }
  return { true, true, true, true, true };
}

// The Manual scales: the map's width, in metres (imperial: round feet / miles).
static const int SCALE_COUNT = 9;

inline float scaleM(int i, bool imperial) {
  static const float M[SCALE_COUNT] = { 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000 };
  static const float I[SCALE_COUNT] = { 91.44f, 182.88f, 402.34f, 804.67f, 1609.34f,
                                        3218.69f, 8046.72f, 16093.44f, 32186.88f };
  if (i < 0) i = 0;
  if (i >= SCALE_COUNT) i = SCALE_COUNT - 1;
  return imperial ? I[i] : M[i];
}

// Up from a fitted map `width_m` wide: the first scale closer than it.
inline int zoomInFrom(float width_m, bool imperial) {
  for (int i = SCALE_COUNT - 1; i >= 0; i--)
    if (scaleM(i, imperial) < width_m * 0.9f) return i;
  return 0;
}

// Down from a fitted map: the first scale wider than it, or -1 (stay fitted).
inline int zoomOutFrom(float width_m, bool imperial) {
  for (int i = 0; i < SCALE_COUNT; i++)
    if (scaleM(i, imperial) > width_m * 1.1f) return i;
  return -1;
}

// Up / Down at Manual scale `i`: the next one, or -1 past the widest (back to
// the fitted view).
inline int zoomIn(int i)  { return i > 0 ? i - 1 : 0; }
inline int zoomOut(int i) { return i + 1 < SCALE_COUNT ? i + 1 : -1; }

}  // namespace mapframe
