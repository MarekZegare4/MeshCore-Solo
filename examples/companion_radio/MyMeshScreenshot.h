#pragma once
// Screenshots over the companion protocol (ENABLE_SCREENSHOT).
// Not part of upstream MyMesh.cpp: included at its bottom, after all class
// definitions (as MyMeshBot.h and MyMeshCli.h).

#ifdef ENABLE_SCREENSHOT
#if defined(DISPLAY_CLASS) && defined(UI_SCREENSHOT_RGB565)
// A colour UI's frame (RGB565, display type 2) goes out band by band, in the
// same frames sendScreenshotResponse() would cut a whole buffer into.
// Asked for RLE (type 3), each band is coded first: a byte c, then for c < 128
// that many + 1 pixels as they are, for c >= 128 one pixel that is there
// c - 127 times. A UI's flat colours shrink ~6x. Its size isn't known until
// the end, so the chunks say 0xFFFF for their count but the last, which says it.
// The live view's (RLE) frames come from the panel's copy (UITask::shotMirror)
// a band a loop() (shotPump): the loop isn't held up, touch is read as ever
// (a band from a moment later than the one above it: a screen in motion may
// show a seam). Anything else draws the screen afresh and sends it at once.
static const int SHOT_HDR = 11;
static const uint32_t SHOT_DATA = MAX_FRAME_SIZE - SHOT_HDR;
struct ShotTx {
    BaseSerialInterface* ser;
    int w, h, y;                 // y: the next band (shotPump)
    uint32_t total, done;
    uint16_t chunk, chunks;
    uint32_t fill;
    bool cut, rle, pumping;
    const uint16_t* mirror;
    uint8_t f[MAX_FRAME_SIZE];   // its own: other replies go out between bands
};
static ShotTx s_tx;
// The chunk filled so far, out; `last` gives the count (RLE).
static void shotOut(ShotTx& t, bool last) {
    uint8_t* o = t.f;
    const uint16_t n = t.rle ? (last ? t.chunk + 1 : 0xFFFF) : t.chunks;
    o[0] = RESP_CODE_SCREENSHOT;
    o[1] = t.rle ? SCREENSHOT_TYPE_RGB565_RLE : SCREENSHOT_TYPE_RGB565;
    o[2] = 0;
    o[3] = t.w & 0xFF; o[4] = t.w >> 8;
    o[5] = t.h & 0xFF; o[6] = t.h >> 8;
    o[7] = t.chunk & 0xFF; o[8] = t.chunk >> 8;
    o[9] = n & 0xFF; o[10] = n >> 8;
    if (t.ser->writeFrame(o, SHOT_HDR + t.fill) < SHOT_HDR + t.fill) { t.cut = true; return; }   // the host asks again
    t.chunk++;
    t.fill = 0;
}
// Bytes into the chunk, each full one out.
static void shotPut(ShotTx& t, const void* src, uint32_t left) {
    const uint8_t* b = (const uint8_t*)src;
    while (left && !t.cut) {
        const uint32_t k = min(left, SHOT_DATA - t.fill);
        memcpy(&t.f[SHOT_HDR + t.fill], b, k);
        t.fill += k; b += k; left -= k; t.done += k;
        if (t.fill == SHOT_DATA || (!t.rle && t.done == t.total)) shotOut(t, false);
    }
}
static void shotBand(void* ctx, int y0, int rows, const uint16_t* px) {
    (void)y0;
    ShotTx& t = *(ShotTx*)ctx;
    if (t.cut) return;
    if (!t.total) {
        t.total = (uint32_t)t.w * t.h * 2;
        t.chunks = (uint16_t)((t.total + SHOT_DATA - 1) / SHOT_DATA);
    }
    const int n = t.w * rows;
    if (!t.rle) { shotPut(t, px, (uint32_t)n * 2); return; }
    for (int i = 0; i < n && !t.cut; ) {
        int r = 1;
        while (i + r < n && r < 128 && px[i + r] == px[i]) r++;
        if (r >= 2) {
            const uint8_t c = 127 + r;
            shotPut(t, &c, 1);
            shotPut(t, &px[i], 2);
            i += r;
            continue;
        }
        int k = i + 1;   // pixels as they are, up to the next run
        while (k < n && k - i < 128 && !(k + 1 < n && px[k + 1] == px[k])) k++;
        const uint8_t c = k - i - 1;
        shotPut(t, &c, 1);
        shotPut(t, &px[i], (uint32_t)(k - i) * 2);
        i = k;
    }
}

// From loop(): the next band of the live view's frame.
void MyMesh::shotPump() {
    ShotTx& t = s_tx;
    if (!t.pumping) return;
    const int n = min(40, t.h - t.y);
    shotBand(&t, t.y, n, t.mirror + t.y * t.w);
    t.y += n;
    if (!t.cut && t.y >= t.h) shotOut(t, true);   // the rest, and the count
    if (t.cut || t.y >= t.h) t.pumping = false;
}
#endif

void MyMesh::handleScreenshotRequest(bool rle, bool may_skip) {
    #ifdef DISPLAY_CLASS
    extern UITask ui_task;   // main.cpp -- the Listener may be the UI Core, not UITask
    #ifdef UI_SCREENSHOT_RGB565
    // The host has a frame and takes "unchanged": nothing new on the panel.
    if (may_skip && !ui_task.shotWorthIt()) {
        uint8_t* o = out_frame;
        memset(o, 0, SHOT_HDR);
        o[0] = RESP_CODE_SCREENSHOT;
        o[1] = SCREENSHOT_UNCHANGED;
        o[9] = 1;   // one chunk, no data
        _serial->writeFrame(o, SHOT_HDR);
        return;
    }
    ShotTx& t = s_tx;
    memset(&t, 0, offsetof(ShotTx, f));   // a frame still going is dropped: this one is asked for
    t.ser = _serial;
    t.rle = rle;
    if (rle && (t.mirror = ui_task.shotMirror(t.w, t.h))) {
        t.pumping = true;
        shotPump();   // the first band now
        return;
    }
    const bool ok = ui_task.captureBands(t.w, t.h, false, shotBand, &t);
    if (!ok) writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
    else if (t.rle && !t.cut) shotOut(t, true);   // the rest, and the count
    #else
    if (!ui_task.hasDisplay()) {
        writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
        return;
    }

    DisplayDriver* display = ui_task.getDisplay();
    if (!display) {
        writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
        return;
    }

    const uint8_t* buffer = display->getBuffer();
    uint16_t bufferSize = display->getBufferSize();

    if (buffer && bufferSize > 0) {
        sendScreenshotResponse(display->getDisplayType(), display->screenshotRotation(),
                               (uint16_t)display->screenshotWidth(), (uint16_t)display->screenshotHeight(),
                               buffer, bufferSize);
    } else {
        writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
    }
    #endif
    #else
    writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
    #endif
    (void)rle; (void)may_skip;
}

void MyMesh::sendScreenshotResponse(uint8_t displayType, uint8_t rotation, uint16_t width, uint16_t height,
                                    const uint8_t* buffer, uint32_t bufferSize) {
    // Frame format (11-byte header, little-endian multi-byte fields):
    //   [0]    resp_code = RESP_CODE_SCREENSHOT
    //   [1]    display_type (0=OLED page-based, 1=e-ink row-major MSB-first 1=white,
    //                        2=RGB565 little-endian, row-major -- colour screens)
    //   [2]    rotation     (0-3, GxEPD2/Adafruit_GFX value; only meaningful for e-ink)
    //   [3..4] width  (uint16 LE — GxEPD2-reported visible width)
    //   [5..6] height (uint16 LE — GxEPD2-reported visible height)
    //   [7..8] chunk_idx    (uint16 LE)
    //   [9..10] total_chunks (uint16 LE)
    //   [11..] chunk data
    const int HEADER_SIZE = 11;
    const uint32_t MAX_DATA_PER_FRAME = MAX_FRAME_SIZE - HEADER_SIZE;
    const uint16_t totalChunks = (uint16_t)((bufferSize + MAX_DATA_PER_FRAME - 1) / MAX_DATA_PER_FRAME);

    for (uint16_t chunkIdx = 0; chunkIdx < totalChunks; chunkIdx++) {
        int i = 0;
        out_frame[i++] = RESP_CODE_SCREENSHOT;
        out_frame[i++] = displayType;
        out_frame[i++] = rotation;
        out_frame[i++] = width  & 0xFF; out_frame[i++] = width  >> 8;
        out_frame[i++] = height & 0xFF; out_frame[i++] = height >> 8;
        out_frame[i++] = chunkIdx    & 0xFF; out_frame[i++] = chunkIdx    >> 8;
        out_frame[i++] = totalChunks & 0xFF; out_frame[i++] = totalChunks >> 8;

        const uint32_t offset = (uint32_t)chunkIdx * MAX_DATA_PER_FRAME;
        const uint32_t chunkSize = min(MAX_DATA_PER_FRAME, bufferSize - offset);
        memcpy(&out_frame[i], buffer + offset, chunkSize);
        i += chunkSize;

        // A chunk that didn't go (the host not reading: the write timed out)
        // ends the frame -- the rest would each wait out the same timeout,
        // the loop held up for seconds; the host asks for the frame again.
        if (_serial->writeFrame(out_frame, i) < (size_t)i) break;
    }
}
#endif // ENABLE_SCREENSHOT
