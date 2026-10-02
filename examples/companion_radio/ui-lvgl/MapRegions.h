#pragma once
// Map tools > Vector regions: the region packs on the card (/vmap/*.vpk,
// map/VectorPacks.h, made with tools/maps/osm_vector.py --pack): show one on
// the map, delete it.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after MapAreas.h.

namespace regions {

static int s_sel = -1;                // the pack a popup is about
static lv_obj_t* s_del_lbl = nullptr;

enum : uint8_t { A_SHOW, A_DELETE };

static void onDeleted(lv_event_t* e) { if (lv_event_get_target(e) == s_del_lbl) s_del_lbl = nullptr; }
static void onPack(lv_event_t* e) { s_ui->mapRegionPopup((int)(intptr_t)lv_event_get_user_data(e)); }
static void onAct(lv_event_t* e)  { s_ui->mapRegionAction((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }

static lv_obj_t* actButton(lv_obj_t* bar, const char* text, uint8_t act, bool accent) {
  return lv_obj_get_child(barButton(bar, text, onAct, act, accent), 0);
}

static void fmtMB(char* out, size_t n, uint32_t bytes) {
  if (bytes >= 1024 * 1024) snprintf(out, n, "%.1f MB", bytes / 1048576.0);
  else snprintf(out, n, "%lu KB", (unsigned long)((bytes + 1023) / 1024));
}

}  // namespace regions

void UITask::mapRegionsPopup() {
  using namespace regions;
  mapview::s_vpacks.scan();
  lv_obj_t* panel = navPopupPanel("Vector regions", true);
  lv_obj_t* list = scrollList(panel);
  int np = mapview::s_vpacks.count();
  if (np) {
    lv_obj_t* g = group(list, nullptr);
    for (int i = 0; i < np; i++) {
      const mapview::VectorPacks::Pack& p = mapview::s_vpacks.at(i);
      char sub[48], mb[16];
      fmtMB(mb, sizeof(mb), p.size);
      snprintf(sub, sizeof(sub), "%s%s", mb, (p.flags & mapview::VectorPacks::F_CONTOURS) ? "  -  contours" : "");
      listRow(g, p.name[0] ? p.name : p.file, sub, onPack, (void*)(intptr_t)i);
    }
  }
  noteLabel(list, np ? "Packs in /vmap on the SD card, made with tools/maps/osm_vector.py --pack."
            : "No regions yet. Make a pack with tools/maps/osm_vector.py --pack and copy it to /vmap on the SD card.");
}

void UITask::mapRegionPopup(int idx) {
  using namespace regions;
  if (idx < 0 || idx >= mapview::s_vpacks.count()) return;
  s_sel = idx;
  const mapview::VectorPacks::Pack& p = mapview::s_vpacks.at(idx);
  lv_obj_t* panel = navPopupPanel(p.name[0] ? p.name : p.file, false);
  char info[128], mb[16];
  fmtMB(mb, sizeof(mb), p.size);
  snprintf(info, sizeof(info), "%s, zoom %d-18%s\n%s", mb, p.zmin, (p.flags & mapview::VectorPacks::F_CONTOURS) ? ", contours" : "", p.file);
  noteLabel(panel, info, THEME_FONT_SMALL, theme::TEXT);
  lv_obj_t* r = buttonBar(panel);
  actButton(r, UI_SYMBOL_MAP " Show", A_SHOW, true);
  s_del_lbl = actButton(r, LV_SYMBOL_TRASH " Delete", A_DELETE, false);
  lv_obj_add_event_cb(s_del_lbl, onDeleted, LV_EVENT_DELETE, NULL);
}

void UITask::mapRegionAction(uint8_t act) {
  using namespace regions;
  if (s_sel < 0 || s_sel >= mapview::s_vpacks.count()) return;
  const mapview::VectorPacks::Pack& p = mapview::s_vpacks.at(s_sel);
  switch (act) {
    case A_SHOW: {
      navClosePopup();
      double x0 = mapview::lonToTileX(p.box[0] / 1e6, 0), x1 = mapview::lonToTileX(p.box[2] / 1e6, 0);
      double y0 = mapview::latToTileY(p.box[3] / 1e6, 0), y1 = mapview::latToTileY(p.box[1] / 1e6, 0);
      int w = _map_area ? lv_obj_get_width(_map_area) : 320, h = _map_area ? lv_obj_get_height(_map_area) : 218;
      _map_z = areas::fitZoom(x1 - x0, y1 - y0, w - 40, h - 60);
      double s = (double)(1 << _map_z);
      _map_cx = (x0 + x1) / 2 * s;
      _map_cy = (y0 + y1) / 2 * s;
      _map_follow = false;
      layoutMap();
      return;
    }
    case A_DELETE: {
      if (!tapConfirmed(s_del_lbl, "Delete?")) return;
      char path[80];
      snprintf(path, sizeof(path), "%s/%s", mapview::VECTOR_ROOT, p.file);
      mapview::s_vpacks.closeOpen();
      remove(path);
      mapview::s_vector.available();   // lists the packs again, forgets the data read
      mapview::labels::flush();
      mapview::s_cache.invalidate();
      mapRegionsPopup();
      return;
    }
  }
}
