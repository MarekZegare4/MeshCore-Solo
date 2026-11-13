#pragma once
// Planned routes over the companion protocol (FEAT_ROUTES): CMD_ROUTE, Solo's.
// Not part of upstream MyMesh.cpp: included at its bottom, after all class
// definitions (as MyMeshScreenshot.h). The files: PlannedRoute.h.
//
// The site's Tools page (meshcore-solo-site assets/tools.js) is the client: it
// reads the GPX, simplifies it to max_points, and sends it here. All values
// little-endian; a reply is RESP_CODE_ROUTE with the sub-command echoed, or
// RESP_CODE_OK / RESP_CODE_ERR.
//
//   0 INFO                      -> [sub] [ver] [slots] [max_points:2] [used_mask:4] [per_frame]
//   1 GET   [slot]              -> [sub] [slot] [count:2] [length_m:4] [flags] [name...]
//   2 BEGIN [slot|0xFF] [count:2] [flags] [name...]  -> [sub] [slot]   (0xFF: the first free)
//   3 DATA  [start:2] [n] n * ([lat_1e6:4] [lon_1e6:4] [ele_m:2])  -> OK
//   4 END                       -> [sub] [slot] [length_m:4]
//   5 DELETE [slot]             -> OK
//   6 ABORT                     -> OK
//
// DATA comes in order (start == the points written so far); anything else ends
// the upload, so a dropped frame can't leave a hole in a route.

#if FEAT_ROUTES
#include "PlannedRoute.h"

#define RESP_CODE_ROUTE  0x7D

namespace route_cmd {
  enum : uint8_t { INFO = 0, GET = 1, BEGIN = 2, DATA = 3, END = 4, DELETE = 5, ABORT = 6 };
  static const uint8_t PROTO_VER = 1;
  static const int     PT_BYTES = 10;
  static const int     PER_FRAME = (MAX_FRAME_SIZE - 5) / PT_BYTES;
  static PlannedRoutes upload;

  inline void put16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
  inline void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (v >> (8 * i)) & 0xFF; }
  inline uint16_t get16(const uint8_t* p) { return p[0] | (p[1] << 8); }
  inline uint32_t get32(const uint8_t* p) { return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
}

void MyMesh::handleRouteCommand(const uint8_t* frame, size_t len) {
  using namespace route_cmd;
  if (len < 2) { writeErrFrame(ERR_CODE_ILLEGAL_ARG); return; }
  const uint8_t sub = frame[1];
  int i = 0;

  if (!PlannedRoutes::fs()) { writeErrFrame(ERR_CODE_UNSUPPORTED_CMD); return; }

  if (sub == INFO) {
    out_frame[i++] = RESP_CODE_ROUTE;
    out_frame[i++] = INFO;
    out_frame[i++] = PROTO_VER;
    out_frame[i++] = PlannedRoutes::SLOTS;
    put16(&out_frame[i], PlannedRoutes::MAX_POINTS); i += 2;
    put32(&out_frame[i], PlannedRoutes::usedMask()); i += 4;
    out_frame[i++] = PER_FRAME;
    _serial->writeFrame(out_frame, i);

  } else if (sub == GET) {
    RouteInfo ri;
    if (len < 3 || frame[2] >= PlannedRoutes::SLOTS || !PlannedRoutes::info(frame[2], ri)) { writeErrFrame(ERR_CODE_NOT_FOUND); return; }
    out_frame[i++] = RESP_CODE_ROUTE;
    out_frame[i++] = GET;
    out_frame[i++] = frame[2];
    put16(&out_frame[i], ri.count); i += 2;
    put32(&out_frame[i], ri.length_m); i += 4;
    out_frame[i++] = ri.flags;
    size_t n = strlen(ri.name);
    memcpy(&out_frame[i], ri.name, n); i += n;
    _serial->writeFrame(out_frame, i);

  } else if (sub == BEGIN) {
    if (len < 6) { writeErrFrame(ERR_CODE_ILLEGAL_ARG); return; }
    int slot = frame[2] == 0xFF ? PlannedRoutes::freeSlot() : frame[2];
    if (slot < 0) { writeErrFrame(ERR_CODE_TABLE_FULL); return; }
    const uint16_t count = get16(&frame[3]);
    char name[ROUTE_NAME_LEN];
    size_t n = len - 6;
    if (n > sizeof(name) - 1) n = sizeof(name) - 1;
    memcpy(name, &frame[6], n);
    name[n] = '\0';
    if (count < 2 || count > PlannedRoutes::MAX_POINTS || slot >= PlannedRoutes::SLOTS) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG); return;
    }
    if (!upload.begin(slot, count, frame[5], name)) { writeErrFrame(ERR_CODE_FILE_IO_ERROR); return; }
    out_frame[i++] = RESP_CODE_ROUTE;
    out_frame[i++] = BEGIN;
    out_frame[i++] = (uint8_t)slot;
    _serial->writeFrame(out_frame, i);

  } else if (sub == DATA) {
    if (!upload.open()) { writeErrFrame(ERR_CODE_BAD_STATE); return; }
    if (len < 5) { upload.abort(); writeErrFrame(ERR_CODE_ILLEGAL_ARG); return; }
    const uint16_t start = get16(&frame[2]);
    const int n = frame[4];
    if (start != upload.written() || len < (size_t)(5 + n * PT_BYTES)) {
      upload.abort(); writeErrFrame(ERR_CODE_ILLEGAL_ARG); return;
    }
    const uint8_t* p = &frame[5];
    for (int k = 0; k < n; k++, p += PT_BYTES) {
      if (!upload.add((int32_t)get32(p), (int32_t)get32(p + 4), (int16_t)get16(p + 8))) {
        upload.abort(); writeErrFrame(ERR_CODE_FILE_IO_ERROR); return;
      }
    }
    writeOKFrame();

  } else if (sub == END) {
    const int slot = upload.slot();
    if (!upload.open()) { writeErrFrame(ERR_CODE_BAD_STATE); return; }
    if (!upload.finish()) { writeErrFrame(ERR_CODE_FILE_IO_ERROR); return; }
    RouteInfo ri;
    PlannedRoutes::info(slot, ri);
    out_frame[i++] = RESP_CODE_ROUTE;
    out_frame[i++] = END;
    out_frame[i++] = (uint8_t)slot;
    put32(&out_frame[i], ri.length_m); i += 4;
    _serial->writeFrame(out_frame, i);

  } else if (sub == DELETE) {
    if (len < 3 || frame[2] >= PlannedRoutes::SLOTS || !PlannedRoutes::remove(frame[2])) { writeErrFrame(ERR_CODE_NOT_FOUND); return; }
    writeOKFrame();

  } else if (sub == ABORT) {
    upload.abort();
    writeOKFrame();

  } else {
    writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
  }
}
#endif
