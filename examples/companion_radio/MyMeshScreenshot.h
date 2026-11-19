#pragma once
// Screenshots over the companion protocol (ENABLE_SCREENSHOT).
// Not part of upstream MyMesh.cpp: included at its bottom, after all class
// definitions (as MyMeshBot.h and MyMeshCli.h).

#ifdef ENABLE_SCREENSHOT
void MyMesh::handleScreenshotRequest() {
    #ifdef DISPLAY_CLASS
    extern UITask ui_task;   // main.cpp -- the Listener may be the UI Core, not UITask
    #ifdef UI_SCREENSHOT_RGB565
    // A colour UI renders the frame for it band by band (RGB565, display
    // type 2); the bands go out as they come, in the same frames
    // sendScreenshotResponse() would cut a whole buffer into.
    struct Tx { MyMesh* m; int w, h; uint32_t total, done; uint16_t chunk, chunks; uint32_t fill; };
    Tx tx = { this, 0, 0, 0, 0, 0, 0, 0 };
    const bool ok = ui_task.captureBands(tx.w, tx.h, false, [](void* ctx, int y0, int rows, const uint16_t* px) {
        (void)y0;
        Tx& t = *(Tx*)ctx;
        const int HEADER_SIZE = 11;
        const uint32_t MAX_DATA = MAX_FRAME_SIZE - HEADER_SIZE;
        if (!t.total) {
            t.total = (uint32_t)t.w * t.h * 2;
            t.chunks = (uint16_t)((t.total + MAX_DATA - 1) / MAX_DATA);
        }
        const uint8_t* b = (const uint8_t*)px;
        uint32_t left = (uint32_t)t.w * rows * 2;
        while (left) {
            const uint32_t k = min(left, MAX_DATA - t.fill);
            memcpy(&t.m->out_frame[HEADER_SIZE + t.fill], b, k);
            t.fill += k; b += k; left -= k; t.done += k;
            if (t.fill == MAX_DATA || t.done == t.total) {
                uint8_t* o = t.m->out_frame;
                o[0] = RESP_CODE_SCREENSHOT;
                o[1] = SCREENSHOT_TYPE_RGB565;
                o[2] = 0;
                o[3] = t.w & 0xFF; o[4] = t.w >> 8;
                o[5] = t.h & 0xFF; o[6] = t.h >> 8;
                o[7] = t.chunk & 0xFF; o[8] = t.chunk >> 8;
                o[9] = t.chunks & 0xFF; o[10] = t.chunks >> 8;
                t.m->_serial->writeFrame(o, HEADER_SIZE + t.fill);
                t.chunk++;
                t.fill = 0;
            }
        }
    }, &tx);
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

        _serial->writeFrame(out_frame, i);
    }
}
#endif // ENABLE_SCREENSHOT
