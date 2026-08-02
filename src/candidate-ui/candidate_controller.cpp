#include "candidate-ui/candidate_controller.h"
#include <QDBusConnection>
#include <QDBusInterface>
#include <QGuiApplication>
#include <QFontMetrics>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
namespace modernime {
CandidateController::CandidateController(QObject *parent) : QObject(parent) {
    auto bus = QDBusConnection::sessionBus();
    bus.connect(QStringLiteral("org.modernime.Service1"), QStringLiteral("/org/modernime/Service1"),
                QStringLiteral("org.modernime.Service1"), QStringLiteral("ConfigChanged"), this,
                SLOT(ApplyConfig(qulonglong,QString)));
    loadAppearance();
    error_timer_.setSingleShot(true);
    error_timer_.setInterval(6'000);
    connect(&error_timer_, &QTimer::timeout, this, [this] {
        if (voice_.state != VoiceState::Error) return;
        voice_ = {};
        visible_ = snapshot_.visible;
        emit changed();
    });
}
bool CandidateController::visible() const { return visible_; }
QString CandidateController::preedit() const { return voice_.state == VoiceState::Idle ? QString::fromUtf8(snapshot_.preedit) : QString{}; }
QString CandidateController::mode() const { if (voice_.state == VoiceState::Listening) return QStringLiteral("● 聆听中"); if (voice_.state == VoiceState::Finalizing) return QStringLiteral("⌛ 识别中"); if (voice_.state == VoiceState::Review) return QStringLiteral("✓ 识别完成"); if (voice_.state == VoiceState::Error) return QStringLiteral("! 语音错误"); return {}; }
QVariantList CandidateController::candidates() const {
    QVariantList result;
    const bool showingVoice = voice_.state == VoiceState::Listening || voice_.state == VoiceState::Finalizing || voice_.state == VoiceState::Review || voice_.state == VoiceState::Error;
    std::vector<Candidate> voiceItems;
    const auto *items = &snapshot_.candidates;
    if (showingVoice) {
        const auto text = voice_.text.empty() ? (voice_.state == VoiceState::Error ? voiceErrorText().toUtf8().toStdString() : "…") : voice_.text;
        const auto annotation = voice_.state == VoiceState::Review ? "Enter 提交  Esc 取消" : voice_.state == VoiceState::Error ? voiceErrorHint().toUtf8().toStdString() : "Esc 取消";
        voiceItems.push_back({text, annotation, false, false});
        items = &voiceItems;
    }
    int index = 0;
    for (const auto &candidate : *items) result.push_back(QVariantMap{{QStringLiteral("number"), index++ + 1}, {QStringLiteral("text"), QString::fromUtf8(candidate.text)}, {QStringLiteral("annotation"), showingVoice ? QString::fromUtf8(candidate.annotation) : QString{}}, {QStringLiteral("pinned"), candidate.pinned}});
    return result;
}
int CandidateController::selected() const { return snapshot_.selected; }
QString CandidateController::pageIndicator() const { return snapshot_.pages > 1 ? QStringLiteral("%1/%2").arg(snapshot_.page).arg(snapshot_.pages) : QString{}; }
int CandidateController::windowHeight() const { return std::max(52, appearance_.font_size + 36); }
int CandidateController::windowWidth() const {
    if (vertical()) return 420;
    const auto textWidth = [](const QString &text, int pixelSize, bool bold = false) {
        QFont font;
        font.setPixelSize(pixelSize);
        font.setBold(bold);
        return QFontMetrics(font).horizontalAdvance(text);
    };
    int width = 24;
    bool first = true;
    const auto addItem = [&width, &first](int itemWidth) {
        if (!first) width += 12;
        width += itemWidth;
        first = false;
    };
    const int preeditSize = std::max(12, appearance_.font_size - 1);
    const int indexSize = std::max(11, appearance_.font_size - 2);
    const int candidateSize = appearance_.font_size + 1;
    const int annotationSize = std::max(11, appearance_.font_size - 3);
    if (!mode().isEmpty()) addItem(textWidth(mode(), preeditSize, true));
    if (!preedit().isEmpty()) addItem(std::min(140, textWidth(preedit(), preeditSize)));
    if (!pageIndicator().isEmpty()) addItem(textWidth(pageIndicator(), annotationSize));
    int index = 0;
    for (const auto &candidate : candidates()) {
        const auto item = candidate.toMap();
        int candidateWidth = textWidth(item.value(QStringLiteral("number")).toString(), indexSize);
        candidateWidth += 5 + textWidth(item.value(QStringLiteral("text")).toString(), candidateSize, index == selected());
        const auto annotation = item.value(QStringLiteral("annotation")).toString();
        if (!annotation.isEmpty()) candidateWidth += 5 + textWidth(annotation, annotationSize);
        addItem(candidateWidth);
        ++index;
    }
    // Extra breathing room covers font fallback differences between the Qt
    // metrics and the QML renderer, preventing the trailing Esc hint from
    // being cut off on CJK fonts.
    return std::clamp(width + 24, 240, 1'280);
}
bool CandidateController::vertical() const { return snapshot_.layout == CandidateLayout::Vertical; }
QString CandidateController::theme() const {
    switch (appearance_.theme) {
    case CandidateTheme::Aurora: return QStringLiteral("aurora");
    case CandidateTheme::Cloud: return QStringLiteral("cloud");
    case CandidateTheme::Ink: return QStringLiteral("ink");
    default: return QStringLiteral("midnight");
    }
}
int CandidateController::fontSize() const { return appearance_.font_size; }
int CandidateController::cornerRadius() const { return appearance_.corner_radius; }
int CandidateController::backgroundOpacity() const { return appearance_.opacity; }
int CandidateController::windowX() const { return x_; }
int CandidateController::windowY() const { return y_; }

void CandidateController::loadAppearance() {
    QDBusInterface service(QStringLiteral("org.modernime.Service1"), QStringLiteral("/org/modernime/Service1"),
                           QStringLiteral("org.modernime.Service1"), QDBusConnection::sessionBus());
    const auto reply = service.call(QStringLiteral("GetConfig"));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().empty()) return;
    if (const auto config = parseConfigSnapshot(reply.arguments().front().toString().toUtf8().toStdString())) appearance_ = *config;
}

void CandidateController::ApplyConfig(qulonglong, const QString &serialized) {
    const auto config = parseConfigSnapshot(serialized.toUtf8().toStdString());
    if (!config) return;
    appearance_ = *config;
    if (visible_) position();
    emit changed();
}
QString CandidateController::voiceErrorText() const {
    switch (voice_.error.value_or(ErrorCode::InvalidRequest)) {
    case ErrorCode::AudioDeviceMissing: return QStringLiteral("未找到可用麦克风");
    case ErrorCode::AudioPermissionDenied: return QStringLiteral("麦克风权限被拒绝");
    case ErrorCode::AudioBufferOverrun: return QStringLiteral("录音缓冲区溢出");
    case ErrorCode::ModelMissing: return QStringLiteral("语音模型未安装");
    case ErrorCode::ModelCorrupt: return QStringLiteral("语音模型已损坏");
    case ErrorCode::ModelLoadFailed: return QStringLiteral("语音模型无法加载");
    case ErrorCode::RecognizerTooSlow: return QStringLiteral("语音识别速度过慢");
    case ErrorCode::FocusChanged: return QStringLiteral("输入焦点已改变");
    case ErrorCode::EmptySpeech: return QStringLiteral("未检测到语音");
    case ErrorCode::StorageFailure: return QStringLiteral("语音配置无法保存");
    default: return QStringLiteral("语音请求未完成");
    }
}
QString CandidateController::voiceErrorHint() const {
    switch (voice_.error.value_or(ErrorCode::InvalidRequest)) {
    case ErrorCode::AudioDeviceMissing: return QStringLiteral("检查麦克风；蓝牙需切到 HFP/HSP");
    case ErrorCode::EmptySpeech: return QStringLiteral("靠近麦克风说话；蓝牙请保持 HFP/HSP 通话模式");
    default: return QStringLiteral("Esc 关闭");
    }
}
CandidateController::LogicalCursor CandidateController::logicalCursor() const {
    LogicalCursor result;
    result.position = {snapshot_.cursor_x, snapshot_.cursor_y};
    result.height = snapshot_.cursor_height;
    result.screen = QGuiApplication::primaryScreen();
    if (snapshot_.cursor_x < 0) return result;

    // Fcitx's Qt X11 frontend deliberately converts Qt's device-independent
    // cursor rectangle to native pixels before publishing it. QWindow::setX/Y
    // expects device-independent coordinates and converts them back to native
    // pixels. Feeding the Fcitx values through unchanged therefore applies the
    // desktop scale twice (for example 300 becomes 450 at 150%).
    //
    // Match the inverse of fcitx5-qt's conversion. Screen origins remain in
    // the virtual desktop coordinate space while each screen's size and local
    // cursor offset are scaled by its devicePixelRatio.
    const QPoint nativePoint(snapshot_.cursor_x, snapshot_.cursor_y);
    for (auto *screen : QGuiApplication::screens()) {
        const auto geometry = screen->geometry();
        const qreal scale = std::max<qreal>(1.0, screen->devicePixelRatio());
        const QRect nativeGeometry(geometry.topLeft(),
                                   QSize(qRound(geometry.width() * scale), qRound(geometry.height() * scale)));
        if (nativeGeometry.contains(nativePoint)) {
            result.screen = screen;
            break;
        }
    }
    if (!result.screen) return result;
    result.scale = std::max<qreal>(1.0, result.screen->devicePixelRatio());
    const auto origin = result.screen->geometry().topLeft();
    result.position = origin + QPoint(qRound((snapshot_.cursor_x - origin.x()) / result.scale),
                                      qRound((snapshot_.cursor_y - origin.y()) / result.scale));
    result.height = qRound(snapshot_.cursor_height / result.scale);
    return result;
}

void CandidateController::position() {
    const auto cursor = logicalCursor();
    auto *screen = cursor.screen;
    if (!screen) return;
    const auto geometry = screen->availableGeometry();
    const int width = windowWidth();
    const int height = windowHeight();
    constexpr int margin = 16;
    if (cursor.position.x() < 0) {
        x_ = geometry.center().x() - width / 2;
        y_ = geometry.bottom() - height - margin;
        return;
    }
    x_ = std::clamp(cursor.position.x(), geometry.left() + margin, geometry.right() - width - margin);
    const int below = cursor.position.y() + std::max(cursor.height, 24) + margin;
    y_ = below + height <= geometry.bottom() - margin
             ? below
             : std::max(geometry.top() + margin, cursor.position.y() - height - margin);
}
void CandidateController::ShowSnapshot(const QString &serialized) {
    const auto value = parseCandidateSnapshot(serialized.toUtf8().toStdString());
    if (!value) return;
    const bool sameProducer = value->producer_id == snapshot_.producer_id;
    if (sameProducer && value->sequence < snapshot_.sequence) return;
    snapshot_ = *value;
    if (snapshot_.visible) {
        voice_ = {};
        error_timer_.stop();
    }
    visible_ = snapshot_.visible || voice_.state != VoiceState::Idle;
    if (visible_) position();
    emit changed();
}
void CandidateController::Hide() { snapshot_.visible = false; if (voice_.state == VoiceState::Idle) visible_ = false; emit changed(); }
void CandidateController::HideSession(const QString &sessionId) {
    hidden_voice_session_ = sessionId;
    if (QString::fromUtf8(voice_.session_id) != sessionId) return;
    voice_ = {};
    error_timer_.stop();
    visible_ = snapshot_.visible;
    emit changed();
}
void CandidateController::ShowVoiceEvent(const QString &serialized) {
    const auto value = parseVoiceEvent(serialized.toUtf8().toStdString());
    if (!value) return;
    if (QString::fromUtf8(value->session_id) == hidden_voice_session_) return;
    const bool changedVisually = voice_.state != value->state || voice_.text != value->text || voice_.error != value->error;
    const bool wasVisible = visible_;
    voice_ = *value;
    if (voice_.state == VoiceState::Error) error_timer_.start(); else error_timer_.stop();
    visible_ = snapshot_.visible || (voice_.state != VoiceState::Idle && voice_.state != VoiceState::Starting);
    if (visible_ && (!wasVisible || changedVisually)) position();
    if (changedVisually || wasVisible != visible_) emit changed();
}
QString CandidateController::Status() const {
    QJsonObject value;
    value.insert(QStringLiteral("visible"), visible_);
    value.insert(QStringLiteral("preedit"), preedit());
    value.insert(QStringLiteral("candidate_count"), candidates().size());
    value.insert(QStringLiteral("page"), snapshot_.page);
    value.insert(QStringLiteral("pages"), snapshot_.pages);
    value.insert(QStringLiteral("theme"), theme());
    value.insert(QStringLiteral("font_size"), fontSize());
    value.insert(QStringLiteral("corner_radius"), cornerRadius());
    value.insert(QStringLiteral("background_opacity"), backgroundOpacity());
    value.insert(QStringLiteral("window_width"), windowWidth());
    value.insert(QStringLiteral("window_height"), windowHeight());
    value.insert(QStringLiteral("voice_state"), QString::fromStdString(voiceStateName(voice_.state)));
    if (voice_.error) value.insert(QStringLiteral("voice_error"), QString::fromStdString(errorCodeName(*voice_.error)));
    value.insert(QStringLiteral("x"), x_);
    value.insert(QStringLiteral("y"), y_);
    const auto cursor = logicalCursor();
    value.insert(QStringLiteral("cursor_native_x"), snapshot_.cursor_x);
    value.insert(QStringLiteral("cursor_native_y"), snapshot_.cursor_y);
    value.insert(QStringLiteral("cursor_logical_x"), cursor.position.x());
    value.insert(QStringLiteral("cursor_logical_y"), cursor.position.y());
    value.insert(QStringLiteral("device_pixel_ratio"), cursor.scale);
    return QJsonDocument(value).toJson(QJsonDocument::Compact);
}
}
