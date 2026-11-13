#pragma once
// Tools > Trail > Routes: the planned routes on the device (../PlannedRoute.h)
// and following one (ui-core/RouteGuide.h). A component of TrailScreen, as
// WaypointsView is: it owns the screen while active().
//
// List: a row per route, its length at the right; the one being followed
// marked. Enter: Follow / Follow reversed / Delete, or, on the one followed,
// Guide / Stop. The guide view is NavView with the guide's target, and how far
// is left (or how far off) as its header. Leaving it keeps the guide going:
// the alerts come wherever you are, and the Trail map draws the route.
//
// Routes come from the site's Tools page over USB (MyMeshRoutes.h).

#if FEAT_ROUTES
#include "../PlannedRoute.h"
#include "PopupMenu.h"
#include "NavView.h"

class RoutesView {
  enum Mode : uint8_t { OFF, LIST, GUIDE };

  struct Row {
    int8_t   slot;
    uint32_t length_m;
    char     name[ROUTE_NAME_LEN];
  };

  UITask*   _task;
  Mode      _mode = OFF;
  Row       _rows[PlannedRoutes::SLOTS];
  int       _n = 0;
  uint16_t  _gen = 0xFFFF;
  int       _sel = 0, _scroll = 0;
  PopupMenu _ctx;
  enum Ctx : uint8_t { CTX_NONE, CTX_ROUTE, CTX_DELETE };
  uint8_t   _ctx_kind = CTX_NONE;
  navview::EtaTracker _eta;

  RouteGuide& guide() { return _task->core().guide; }
  bool useImperial() const { return _task && _task->useImperial(); }

  void reload() {
    _gen = PlannedRoutes::generation();
    _n = 0;
    RouteInfo ri;
    for (int i = 0; i < PlannedRoutes::SLOTS; i++) {
      if (!PlannedRoutes::info(i, ri)) continue;
      Row& r = _rows[_n++];
      r.slot = (int8_t)i;
      r.length_m = ri.length_m;
      memcpy(r.name, ri.name, ROUTE_NAME_LEN);
      if (!r.name[0]) snprintf(r.name, sizeof(r.name), "Route %d", i + 1);
    }
    if (_sel >= _n) _sel = _n > 0 ? _n - 1 : 0;
  }

  int renderList(DisplayDriver& display) {
    if (_gen != PlannedRoutes::generation()) reload();
    char title[24];
    snprintf(title, sizeof(title), "Routes %d/%d", _n, PlannedRoutes::SLOTS);
    drawScreenHeader(display, title);
    if (_n == 0) {
      const int y = display.listStart() + display.lineStep();
      display.drawTextCentered(display.width() / 2, y, "No routes");
      display.drawTextCentered(display.width() / 2, y + display.lineStep(), "Load a GPX from");
      display.drawTextCentered(display.width() / 2, y + 2 * display.lineStep(), "the site over USB");
      return 5000;
    }
    int mq_delay = 0;
    const int followed = guide().slot();
    drawList(display, _n, _sel, _scroll, [&](int row, int y, bool sel, int reserve) {
      drawRowSelection(display, y, sel, reserve);
      const Row& r = _rows[row];
      char dist[12];
      geo::fmtDist(dist, sizeof(dist), r.length_m / 1000.0f, useImperial());
      const int bw = display.getTextWidth(dist) + 2;
      char nm[ROUTE_NAME_LEN + 2];
      char shown[ROUTE_NAME_LEN + 4];
      snprintf(shown, sizeof(shown), "%s%s", r.slot == followed ? "> " : "", r.name);
      display.translateUTF8ToBlocks(nm, shown, sizeof(nm));
      int mqr = display.drawTextEllipsized(2, y, display.width() - 2 - bw - reserve, nm, sel);
      if (sel && mqr > 0) mq_delay = mqr;
      display.setCursor(display.width() - bw + 1 - reserve, y);
      display.print(dist);
      display.setColor(UIColor::primary_txt);
    });
    return (mq_delay > 0 && mq_delay < 1000) ? mq_delay : 1000;
  }

  void renderGuide(DisplayDriver& display) {
    RouteGuide& g = guide();
    char label[24];
    g.label(label, sizeof(label), useImperial());
    int32_t tlat, tlon;
    if (g.state() == RouteGuide::FINISHED || !g.target(tlat, tlon)) {
      drawScreenHeader(display, label);
      const char* what = g.state() == RouteGuide::FINISHED ? g.name()
                       : g.active() ? "Waiting for GPS fix" : "Not following";
      char nm[ROUTE_NAME_LEN + 2];
      display.translateUTF8ToBlocks(nm, what, sizeof(nm));
      display.drawTextCentered(display.width() / 2, display.listStart() + display.lineStep(), nm);
      return;
    }
    int32_t mylat, mylon;
    bool have = _task->currentLocation(mylat, mylon);
    int cog;
    bool cogv = _task->currentCourse(cog);
    // The ETA to the route's end (the target is only the next bend); none off it.
    navview::draw(display, have, mylat, mylon, tlat, tlon, label, cogv, cog, useImperial(),
                  g.offRoute() ? nullptr : &_eta, g.remainingM() / 1000.0f);
  }

  void openRouteMenu() {
    const Row& r = _rows[_sel];
    _ctx_kind = CTX_ROUTE;
    if (r.slot == guide().slot()) {
      _ctx.begin("Route", 2);
      _ctx.addItem("Guide");
      _ctx.addItem("Stop");
    } else {
      _ctx.begin("Route", 3);
      _ctx.addItem("Follow");
      _ctx.addItem("Follow reversed");
      _ctx.addItem("Delete");
    }
  }

  void follow(int slot, bool reversed) {
    if (!guide().start(slot, reversed)) { _task->showAlert("Route unreadable", 1200); return; }
    enterGuide();
  }
  void enterGuide() {
    if (_task->hasGPS() && !_task->getGPSState()) _task->toggleGPS(false);   // a guide without GPS is no guide
    _eta.reset();
    _mode = GUIDE;
  }

public:
  explicit RoutesView(UITask* task) : _task(task) {}

  void reset() { _mode = OFF; _ctx.active = false; _ctx_kind = CTX_NONE; }
  bool active() const { return _mode != OFF; }
  bool guiding() const { return _mode == GUIDE; }

  void openList() { _mode = LIST; _gen = 0xFFFF; _scroll = 0; }
  // Track back (UiCore::startTrackBack): the trail backwards, in the guide view.
  void followTrail() {
    if (!_task->core().startTrackBack()) { _task->showAlert("Can't save the trail", 1200); return; }
    enterGuide();
  }
  void openGuide() { _mode = GUIDE; _eta.reset(); }

  int render(DisplayDriver& display) {
    display.setTextSize(1);
    display.setColor(UIColor::primary_txt);
    if (_mode == GUIDE) { renderGuide(display); return 1000; }
    int delay = renderList(display);
    if (_ctx.active) _ctx.render(display);
    return delay;
  }

  bool handleInput(char c) {
    if (_ctx.active) {
      auto res = _ctx.handleInput(c);
      if (res != PopupMenu::SELECTED) return true;
      const int pick = _ctx.selectedIndex();
      if (_ctx_kind == CTX_DELETE) {
        if (pick == 0 && _sel < _n) {
          if (_rows[_sel].slot == guide().slot()) guide().stop();
          PlannedRoutes::remove(_rows[_sel].slot);
          reload();
        }
        _ctx_kind = CTX_NONE;
        return true;
      }
      if (_sel >= _n) return true;
      const Row& r = _rows[_sel];
      if (r.slot == guide().slot()) {
        if (pick == 0) openGuide();
        else           { guide().stop(); _task->showAlert("Route stopped", 1000); }
      } else if (pick == 0 || pick == 1) {
        follow(r.slot, pick == 1);
      } else {
        _ctx_kind = CTX_DELETE;
        _ctx.beginConfirm("Delete route?", "Delete");
      }
      return true;
    }

    if (_mode == GUIDE) {
      if (c == KEY_CANCEL) _mode = OFF;   // the guide keeps going
      return true;
    }

    if (c == KEY_CANCEL) { _mode = OFF; return true; }
    if (_n == 0) return true;
    // Next / Prev too: a one-button board's click and double click.
    if (c == KEY_UP || keyIsPrev(c))   { _sel = (_sel > 0) ? _sel - 1 : _n - 1; return true; }
    if (c == KEY_DOWN || keyIsNext(c)) { _sel = (_sel < _n - 1) ? _sel + 1 : 0; return true; }
    if (c == KEY_ENTER || c == KEY_CONTEXT_MENU) { openRouteMenu(); return true; }
    return true;
  }
};
#endif
