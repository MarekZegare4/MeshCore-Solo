#pragma once

#include "Mesh.h"
#include <stddef.h>
#include <stdio.h>


class LocationProvider {
protected:
    bool _time_sync_needed = true;

public:
    // Whether a fix may set the clock (Solo: Settings > Time from GPS). One
    // switch for the board, so a provider standing in front of another sees it.
    static bool& timeFromGps() { static bool on = true; return on; }
    virtual void syncTime() { _time_sync_needed = true; }
    virtual bool waitingTimeSync() { return _time_sync_needed; }
    virtual long getLatitude() = 0;
    virtual long getLongitude() = 0;
    virtual long getAltitude() = 0;
    virtual long satellitesCount() = 0;
    // Horizontal Dilution of Precision, in tenths (11 == HDOP 1.1) -- lower is
    // better, and a much more direct read on fix quality than satellite count
    // alone (few satellites in good geometry can beat many in poor geometry).
    // -1 means this provider doesn't expose it; callers fall back to
    // satellitesCount() in that case.
    virtual long getHDOP() { return -1; }
    virtual bool isValid() = 0;
    virtual long getTimestamp() = 0;
    // Default no-op body: a subclass that doesn't override it (variants/sim's
    // SimLocationProvider) still links -- declared without a body, the base
    // class's vtable was an undefined symbol.
    virtual void sendSentence(const char * sentence) { }
    virtual bool waitFor(const char* prefix, uint32_t timeout_ms) { return false; }
    virtual void drain() { }
    virtual void reset() = 0;
    virtual void begin() = 0;
    virtual void stop() = 0;
    virtual void loop() = 0;
    virtual bool isEnabled() = 0;

    // Format compact diagnostics in the caller-provided buffer. Providers that
    // have more information (for example UART counters) can override this.
    virtual void formatDiagnostics(char* out, size_t out_size) {
        if (out_size == 0) return;
        snprintf(out, out_size, "en:%u sat:%ld fix:%u",
            isEnabled() ? 1U : 0U,
            satellitesCount(),
            isValid() ? 1U : 0U);
    }
};
