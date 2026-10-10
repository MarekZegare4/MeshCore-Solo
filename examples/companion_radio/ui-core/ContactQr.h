#pragma once
// Your own contact as a QR code: the link the phone app scans to add you,
// meshcore://contact/add?name=<name>&public_key=<hex>&type=1 (docs/faq.md).
// Made when it is shown (the name can change); both frontends draw it.

#include "QrCodeGen.h"

namespace contactqr {

// Version 10 (57 modules) holds the link with the longest name, low error
// correction; a short name gives a smaller code.
static const int MAX_VERSION = 10;

struct Code {
  uint8_t buf[qrcodegen_BUFFER_LEN_FOR_VERSION(MAX_VERSION)];
  int n = 0;   // modules a side, 0: none
  bool dark(int x, int y) const { return qrcodegen_getModule(buf, x, y); }
};

// The link, the name percent-encoded.
inline void link(char* out, size_t n) {
  static const char DIGITS[] = "0123456789abcdef";
  size_t o = snprintf(out, n, "meshcore://contact/add?name=");
  for (const char* p = the_mesh.getNodeName(); *p && o + 4 < n; p++) {
    unsigned char ch = (unsigned char)*p;
    if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') out[o++] = ch;
    else { out[o++] = '%'; out[o++] = DIGITS[ch >> 4]; out[o++] = DIGITS[ch & 15]; }
  }
  o += snprintf(out + o, n - o, "&public_key=");
  for (int i = 0; i < PUB_KEY_SIZE && o + 3 < n; i++) {
    out[o++] = DIGITS[the_mesh.self_id.pub_key[i] >> 4];
    out[o++] = DIGITS[the_mesh.self_id.pub_key[i] & 15];
  }
  snprintf(out + o, n - o, "&type=%d", ADV_TYPE_CHAT);
}

inline bool make(Code& c) {
  char text[224];
  link(text, sizeof(text));
  uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(MAX_VERSION)];
  bool ok = qrcodegen_encodeText(text, tmp, c.buf, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, MAX_VERSION,
                                 qrcodegen_Mask_AUTO, true);
  c.n = ok ? qrcodegen_getSize(c.buf) : 0;
  return ok;
}

}  // namespace contactqr
