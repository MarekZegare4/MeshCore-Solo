#pragma once
// Screenshots over the companion protocol (ENABLE_SCREENSHOT).
// Not part of upstream MyMesh.cpp: included at its bottom, after all class
// definitions (as MyMeshBot.h and MyMeshCli.h).

#ifdef ENABLE_SCREENSHOT
void MyMesh::handleScreenshotRequest(bool rle, bool may_skip) {
    #ifdef DISPLAY_CLASS
    extern UITask ui_task;   // main.cpp -- the Listener may be the UI Core, not UITask
    #ifdef UI_SCREENSHOT_RGB565
    // The host has a frame and takes "unchanged": not drawn again for nothing,
    // nor while a finger is on the screen (UITask::shotWorthIt()).
    if (may_skip && !ui_task.shotWorthIt()) {
        uint8_t* o = out_frame;
        memset(o, 0, 11);
        o[0] = RESP_CODE_SCREENSHOT;
        o[1] = SCREENSHOT_UNCHANGED;
        o[9] = 1;   // one chunk, no data
        _serial->writeFrame(o, 11);
        return;
    }
    // A colour UI renders the frame for it band by band (RGB565, display
    // type 2); the bands go out as they come, in the same frames
    // sendScreenshotResponse() would cut a whole buffer into.
    // Asked for RLE (type 3), each band is coded first: a byte c, then for
    // c < 128 that many + 1 pixels as they are, for c >= 128 one pixel that
    // is there c - 127 times. A UI's flat colours shrink ~6x: a frame goes out
    // in a fraction of the time. Its size isn't known until the end, so the
    // chunks say 0xFFFF for their count but the last, which says it.
    struct Tx { MyMesh* m; int w, h; uint32_t total, done; uint16_t chunk, chunks; uint32_t fill; bool cut, rle; };
    Tx tx = { this, 0, 0, 0, 0, 0, 0, 0, false, rle };
    static const int HEADER_SIZE = 11;
    static const uint32_t MAX_DATA = MAX_FRAME_SIZE - HEADER_SIZE;
    // The chunk filled so far, out; `last` gives the count (RLE).
    static const auto out = [](Tx& t, bool last) {
        uint8_t* o = t.m->out_frame;
        const uint16_t n = t.rle ? (last ? t.chunk + 1 : 0xFFFF) : t.chunks;
        o[0] = RESP_CODE_SCREENSHOT;
        o[1] = t.rle ? SCREENSHOT_TYPE_RGB565_RLE : SCREENSHOT_TYPE_RGB565;
        o[2] = 0;
        o[3] = t.w & 0xFF; o[4] = t.w >> 8;
        o[5] = t.h & 0xFF; o[6] = t.h >> 8;
        o[7] = t.chunk & 0xFF; o[8] = t.chunk >> 8;
        o[9] = n & 0xFF; o[10] = n >> 8;
        if (t.m->_serial->writeFrame(o, HEADER_SIZE + t.fill) < HEADER_SIZE + t.fill) { t.cut = true; return; }
        t.chunk++;
        t.fill = 0;
    };
    // Bytes into the chunk, each full one out.
    static const auto put = [](Tx& t, const void* src, uint32_t left) {
        const uint8_t* b = (const uint8_t*)src;
        while (left && !t.cut) {
            const uint32_t k = min(left, MAX_DATA - t.fill);
            memcpy(&t.m->out_frame[HEADER_SIZE + t.fill], b, k);
            t.fill += k; b += k; left -= k; t.done += k;
            if (t.fill == MAX_DATA || (!t.rle && t.done == t.total)) out(t, false);
        }
    };
    const bool ok = ui_task.captureBands(tx.w, tx.h, false, [](void* ctx, int y0, int rows, const uint16_t* px) {
        (void)y0;
        Tx& t = *(Tx*)ctx;
        if (t.cut) return;   // a chunk didn't go: the host asks for the frame again
        if (!t.total) {
            t.total = (uint32_t)t.w * t.h * 2;
            t.chunks = (uint16_t)((t.total + MAX_DATA - 1) / MAX_DATA);
        }
        const int n = t.w * rows;
        if (!t.rle) { put(t, px, (uint32_t)n * 2); return; }
        for (int i = 0; i < n && !t.cut; ) {
            int r = 1;
            while (i + r < n && r < 128 && px[i + r] == px[i]) r++;
            if (r >= 2) {
                const uint8_t c = 127 + r;
                put(t, &c, 1);
                put(t, &px[i], 2);
                i += r;
                continue;
            }
            int k = i + 1;   // pixels as they are, up to the next run
            while (k < n && k - i < 128 && !(k + 1 < n && px[k + 1] == px[k])) k++;
            const uint8_t c = k - i - 1;
            put(t, &c, 1);
            put(t, &px[i], (uint32_t)(k - i) * 2);
            i = k;
        }
    }, &tx);
    if (ok && tx.rle && !tx.cut) out(tx, true);   // the rest, and the count
    if (!ok) writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
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
