// SPDX-License-Identifier: Apache-2.0
#include "voice-worker/sherpa_api.h"

namespace modernime {
namespace {
template <typename Function>
bool loadSymbol(QLibrary &library, Function &target, const char *name, QString *error) {
    target = reinterpret_cast<Function>(library.resolve(name));
    if (target) return true;
    if (error) *error = QStringLiteral("sherpa-onnx 缺少 ABI 符号 %1: %2").arg(QString::fromLatin1(name), library.errorString());
    return false;
}
}

bool SherpaApi::load(const QString &libraryPath, QString *error) {
    if (ready()) return true;
    library_.setFileName(libraryPath);
    if (!library_.load()) {
        if (error) *error = QStringLiteral("无法载入 sherpa-onnx 运行时: %1").arg(library_.errorString());
        return false;
    }
    return loadSymbol(library_, createRecognizer, "SherpaOnnxCreateOnlineRecognizer", error) &&
           loadSymbol(library_, destroyRecognizer, "SherpaOnnxDestroyOnlineRecognizer", error) &&
           loadSymbol(library_, createStream, "SherpaOnnxCreateOnlineStream", error) &&
           loadSymbol(library_, destroyStream, "SherpaOnnxDestroyOnlineStream", error) &&
           loadSymbol(library_, acceptWaveform, "SherpaOnnxOnlineStreamAcceptWaveform", error) &&
           loadSymbol(library_, isReady, "SherpaOnnxIsOnlineStreamReady", error) &&
           loadSymbol(library_, decode, "SherpaOnnxDecodeOnlineStream", error) &&
           loadSymbol(library_, getResult, "SherpaOnnxGetOnlineStreamResult", error) &&
           loadSymbol(library_, destroyResult, "SherpaOnnxDestroyOnlineRecognizerResult", error) &&
           loadSymbol(library_, inputFinished, "SherpaOnnxOnlineStreamInputFinished", error) &&
           loadSymbol(library_, createVoiceActivityDetector, "SherpaOnnxCreateVoiceActivityDetector", error) &&
           loadSymbol(library_, destroyVoiceActivityDetector, "SherpaOnnxDestroyVoiceActivityDetector", error) &&
           loadSymbol(library_, vadAcceptWaveform, "SherpaOnnxVoiceActivityDetectorAcceptWaveform", error) &&
           loadSymbol(library_, vadEmpty, "SherpaOnnxVoiceActivityDetectorEmpty", error) &&
           loadSymbol(library_, vadDetected, "SherpaOnnxVoiceActivityDetectorDetected", error) &&
           loadSymbol(library_, vadReset, "SherpaOnnxVoiceActivityDetectorReset", error) &&
           loadSymbol(library_, vadFlush, "SherpaOnnxVoiceActivityDetectorFlush", error) &&
           loadSymbol(library_, createOfflinePunctuation, "SherpaOnnxCreateOfflinePunctuation", error) &&
           loadSymbol(library_, destroyOfflinePunctuation, "SherpaOnnxDestroyOfflinePunctuation", error) &&
           loadSymbol(library_, addOfflinePunctuation, "SherpaOfflinePunctuationAddPunct", error) &&
           loadSymbol(library_, freeOfflinePunctuationText, "SherpaOfflinePunctuationFreeText", error);
}

bool SherpaApi::ready() const {
    return library_.isLoaded() && createRecognizer && destroyRecognizer && createStream && destroyStream && acceptWaveform && isReady && decode && getResult && destroyResult && inputFinished && createVoiceActivityDetector && destroyVoiceActivityDetector && vadAcceptWaveform && vadEmpty && vadDetected && vadReset && vadFlush && createOfflinePunctuation && destroyOfflinePunctuation && addOfflinePunctuation && freeOfflinePunctuationText;
}
} // namespace modernime
