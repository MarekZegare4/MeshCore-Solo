#pragma once

#include <stdint.h>

// A QR code for the Solo site, the same on every board (Status > System on
// ui-new, Settings > About on the L2). Precomputed, so no QR library: the
// URL in upper case fits QR's alphanumeric mode, which keeps it at version 2
// (25 x 25 modules, error correction M) -- large modules on a small screen.
// Regenerate with the Python qrcode package: QRCode(error_correction=M,
// border=0), add_data() the string below; module x of row y is bit 31 - x.
namespace siteqr {

static const char* const URL = "solo.marekzegarek.com";
static const int N = 25;       // modules per side
static const int QUIET = 2;    // light modules around it; 2 is plenty on a screen

static const uint32_t ROWS[N] = {   // "HTTPS://SOLO.MAREKZEGAREK.COM"
  0xFEC2BF80u, 0x824FA080u, 0xBAC6AE80u, 0xBA2B2E80u, 0xBA362E80u,
  0x82D62080u, 0xFEAABF80u, 0x007D8000u, 0xA3679280u, 0xCDA53C80u,
  0xFE9AEA80u, 0xB02B9C80u, 0x578F0700u, 0x69892480u, 0xE34E8D00u,
  0x19B92900u, 0xD7C6F980u, 0x00E48F80u, 0xFEB0AB00u, 0x82658E80u,
  0xBA3DFA00u, 0xBA083C00u, 0xBACE4B80u, 0x821AA080u, 0xFEC76F80u,
};

static inline bool dark(int x, int y) { return (ROWS[y] >> (31 - x)) & 1; }

}  // namespace siteqr
