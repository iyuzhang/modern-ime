#include "contracts/audio_health.h"

#include <cmath>

namespace modernime {
namespace {
constexpr uint64_t kRecoveryObservationFrames = 8'000; // 500 ms at 16 kHz.
constexpr float kDigitalSilencePeak = 1.0e-8F;
} // namespace

bool isDigitallySilentCapture(uint64_t frames, float peak, uint64_t minimumFrames) {
    return frames >= minimumFrames && std::isfinite(peak) && std::abs(peak) <= kDigitalSilencePeak;
}

SilentCaptureAction SilentCaptureMonitor::observe(uint64_t frames, float peak) {
    if (!isDigitallySilentCapture(frames, peak, kRecoveryObservationFrames)) return SilentCaptureAction::None;
    if (!recovery_attempted_) {
        recovery_attempted_ = true;
        return SilentCaptureAction::Reconnect;
    }
    return SilentCaptureAction::Fail;
}

void SilentCaptureMonitor::reset() { recovery_attempted_ = false; }
bool SilentCaptureMonitor::recoveryAttempted() const { return recovery_attempted_; }

} // namespace modernime
