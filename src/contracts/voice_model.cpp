#include "contracts/voice_model.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace modernime {
namespace {
QString resolveModelFile(const QString &directory, const QString &relative, bool optional, QString *error) {
    if (relative.isEmpty() && optional) return {};
    const auto clean = QDir::cleanPath(relative);
    if (relative.isEmpty() || QDir::isAbsolutePath(relative) || clean == QStringLiteral("..") || clean.startsWith(QStringLiteral("../"))) {
        if (error) *error = QStringLiteral("invalid model file path: ") + relative;
        return {};
    }
    const auto path = QDir(directory).absoluteFilePath(clean);
    const QFileInfo file(path);
    const auto root = QFileInfo(directory).canonicalFilePath();
    const auto canonical = file.canonicalFilePath();
    if (!file.isFile() || root.isEmpty() || canonical.isEmpty() || !canonical.startsWith(root + QLatin1Char('/'))) {
        if (error) *error = QStringLiteral("missing model file: ") + clean;
        return {};
    }
    return canonical;
}
} // namespace

std::optional<VoiceModelSpec> loadVoiceModelSpec(const QString &directory, QString *error) {
    QFile manifest(QDir(directory).filePath(QStringLiteral("model.json")));
    if (!manifest.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("missing model.json");
        return std::nullopt;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(manifest.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("invalid model.json");
        return std::nullopt;
    }
    const auto object = document.object();
    const auto id = object.value(QStringLiteral("id")).toString();
    if (object.value(QStringLiteral("format")).toString() != QStringLiteral("modern-ime-model") || id.isEmpty()) {
        if (error) *error = QStringLiteral("unsupported model manifest");
        return std::nullopt;
    }

    VoiceModelSpec result;
    result.id = id;
    result.directory = QDir(directory).absolutePath();
    result.source = object.value(QStringLiteral("source")).toString();
    result.license = object.value(QStringLiteral("license")).toString(QStringLiteral("UNKNOWN"));
    QString encoderName;
    QString decoderName;
    QString joinerName;
    QString tokensName;
    QString bpeName;
    const auto files = object.value(QStringLiteral("files")).toObject();
    if (!files.isEmpty()) {
        if (object.value(QStringLiteral("kind")).toString() != QStringLiteral("transducer")) {
            if (error) *error = QStringLiteral("unsupported recognizer kind");
            return std::nullopt;
        }
        encoderName = files.value(QStringLiteral("encoder")).toString();
        decoderName = files.value(QStringLiteral("decoder")).toString();
        joinerName = files.value(QStringLiteral("joiner")).toString();
        tokensName = files.value(QStringLiteral("tokens")).toString();
        bpeName = files.value(QStringLiteral("bpe_vocab")).toString();
        result.modeling_unit = object.value(QStringLiteral("modeling_unit")).toString(QStringLiteral("cjkchar"));
        result.supports_hotwords = object.value(QStringLiteral("supports_hotwords")).toBool();
    } else if (id == QString::fromLatin1(kLegacyBilingualVoiceModel)) {
        encoderName = QStringLiteral("encoder-epoch-99-avg-1.int8.onnx");
        decoderName = QStringLiteral("decoder-epoch-99-avg-1.onnx");
        joinerName = QStringLiteral("joiner-epoch-99-avg-1.int8.onnx");
        tokensName = QStringLiteral("tokens.txt");
        bpeName = QStringLiteral("bpe.vocab");
        result.modeling_unit = QStringLiteral("cjkchar+bpe");
        result.supports_hotwords = true;
    } else {
        if (error) *error = QStringLiteral("model manifest does not describe its files");
        return std::nullopt;
    }

    result.encoder = resolveModelFile(directory, encoderName, false, error);
    result.decoder = resolveModelFile(directory, decoderName, false, error);
    result.joiner = resolveModelFile(directory, joinerName, false, error);
    result.tokens = resolveModelFile(directory, tokensName, false, error);
    result.bpe_vocab = resolveModelFile(directory, bpeName, true, error);
    if (result.encoder.isEmpty() || result.decoder.isEmpty() || result.joiner.isEmpty() || result.tokens.isEmpty() || (!bpeName.isEmpty() && result.bpe_vocab.isEmpty())) return std::nullopt;
    return result;
}

bool isValidVoiceModelId(QStringView modelId) {
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$"));
    return pattern.match(modelId.toString()).hasMatch();
}

std::optional<VoiceHotword> voiceHotwordForLexeme(const Lexeme &lexeme) {
    if (lexeme.blocked || lexeme.output.empty()) return std::nullopt;
    auto phrase = QString::fromUtf8(lexeme.output.data(), static_cast<qsizetype>(lexeme.output.size())).trimmed();
    phrase.replace(QRegularExpression(QStringLiteral("[\\r\\n]+")), QStringLiteral(" "));
    static const QRegularExpression safePhrase(QStringLiteral("^[\\p{L}\\p{N} _+.-]+$"));
    const bool curated = lexeme.pinned || lexeme.kind == "manual" || lexeme.kind == "replacement";
    const bool usefulLearnedPhrase = phrase.size() >= 3 || (phrase.size() == 2 && lexeme.select_count >= 2);
    if ((!curated && !usefulLearnedPhrase) || phrase.isEmpty() || phrase.size() > 64 || !safePhrase.match(phrase).hasMatch()) return std::nullopt;
    return VoiceHotword{phrase, lexeme.pinned ? 3.0F : (curated ? 2.5F : 0.0F)};
}

} // namespace modernime
