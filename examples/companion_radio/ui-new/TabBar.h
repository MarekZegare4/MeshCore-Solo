#pragma once
// Shared circular tab-bar header, extracted after NearbyScreen and BotScreen
// had each independently grown byte-identical rendering code for the same
// "active tab centred as a pill, neighbours fan out either side and wrap
// around" layout (see NearbyScreen::drawFilterTabs / BotScreen::drawTabBar,
// pre-extraction). A third screen (AdminScreen) needing the same thing was
// the rule-of-three trigger to finally share it.
//
// Tab-switching itself (LEFT/RIGHT cycling the active index) is NOT part of
// this helper -- each screen's cycle has different side effects (refresh a
// list, reset a row selection, ...), so that one-liner stays inline per screen.

namespace tabbar {

// One tab: short label; the active one is an inverted pill (same look as a
// selected row) so it reads as "you are here" in the tab strip.
inline void drawPill(DisplayDriver& display, int x, int w, const char* label, bool active) {
  int lh = display.getLineHeight();
  if (active) {
    display.setColor(DisplayDriver::LIGHT);
    display.fillSoftRect(x, 0, w, lh + 1);
    display.setColor(DisplayDriver::DARK);
  } else {
    display.setColor(DisplayDriver::LIGHT);
  }
  display.drawTextCentered(x + w / 2, 0, label);
  display.setColor(DisplayDriver::LIGHT);
}

// Small solid triangle pointing left (dir < 0) or right, 3 px wide and 5
// tall, centred on row y_mid: "more tabs this way".
inline void drawEdgeArrow(DisplayDriver& display, int x, int y_mid, int dir) {
  display.setColor(DisplayDriver::LIGHT);
  for (int c = 0; c < 3; c++) {
    int h = dir < 0 ? c * 2 + 1 : 5 - c * 2;
    display.fillRect(x + c, y_mid - h / 2, 1, h);
  }
}

// Header as a circular tab bar: the active tab sits centred as a soft pill and
// the neighbours that fit whole fan out either side. When some tabs don't fit,
// a small arrow at each edge says there are more (the bar wraps around, so
// both ways lead to them) instead of squeezing in cut-off "Sn..." labels.
// `right_reserve` keeps any trailing decoration the caller draws afterwards
// (a context-menu hint, a counter) clear. Also draws the header separator.
inline void draw(DisplayDriver& display, const char* const* labels, int count, int active, int right_reserve = 0) {
  const int pad = 3, gap = 2;
  const int aw = display.getTextWidth(labels[active]) + pad * 2;
  const int ax = display.width() / 2 - aw / 2;
  const int rx_limit = display.width() - right_reserve;

  // Lays the neighbours out (and draws them when `paint`), keeping `edge` px
  // free at both ends; returns how many tabs are on screen and the free space
  // left at each end.
  struct Fit { int shown, left_free, right_free; };
  auto layout = [&](int edge, bool paint) {
    int lx = ax - gap, rx = ax + aw + gap, shown = 1;
    bool lfit = true, rfit = true;
    for (int k = 1; k <= count / 2 && (lfit || rfit); k++) {
      int li = (active - k + count) % count;
      int ri = (active + k) % count;
      if (lfit) {
        int w = display.getTextWidth(labels[li]) + pad * 2;
        if (lx - w >= edge) { if (paint) drawPill(display, lx - w, w, labels[li], false); lx -= w + gap; shown++; }
        else lfit = false;
      }
      // Skip the right side when it lands on the same tab as the left (the
      // single opposite tab on an even count) so it isn't drawn twice.
      if (rfit && ri != li) {
        int w = display.getTextWidth(labels[ri]) + pad * 2;
        if (rx + w <= rx_limit - edge) { if (paint) drawPill(display, rx, w, labels[ri], false); rx += w + gap; shown++; }
        else rfit = false;
      }
    }
    return Fit{ shown, lx + gap, rx_limit - rx + gap };
  };

  drawPill(display, ax, aw, labels[active], true);
  Fit f = layout(0, false);
  if (f.shown < count && f.left_free < 4 && f.right_free < 4)
    f = layout(5, true);             // no room for an arrow anywhere: give up a tab for them
  else
    layout(0, true);
  if (f.shown < count) {             // more tabs off screen: an arrow wherever there's room
    int y_mid = display.getLineHeight() / 2;
    if (f.left_free >= 4)  drawEdgeArrow(display, 0, y_mid, -1);
    if (f.right_free >= 4) drawEdgeArrow(display, rx_limit - 3, y_mid, +1);
  }
  display.setColor(DisplayDriver::LIGHT);
  display.fillRect(0, display.headerH() - display.sepH(), display.width(), display.sepH());
}

}  // namespace tabbar
