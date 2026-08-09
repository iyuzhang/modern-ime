#pragma once

#include <cstdint>

namespace modernime {

enum class SilentCaptureAction { None, Reconnect, Fail };

// A real microphone has a noise floor even when the user is silent. PipeWire
// Bluetooth nodes that survived a broken SCO transport instead deliver long
// runs of exact digital zeroes. Keep this policy independent of PipeWire so
// its retry boundary can be covered without recording test audio.
bool isDigitallySilentCapture(uint64_t frames, float peak, uint64_t minimumFrames = 1'600);

class SilentCaptureMonitor final {
public:
    SilentCaptureAction observe(uint64_t frames, float peak);
    void reset();
    bool recoveryAttempted() const;

private:
    bool recovery_attempted_ = false;
};

} // namespace modernime
