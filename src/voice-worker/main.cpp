#include "voice-worker/voice_worker.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
int main(int argc, char **argv) { QCoreApplication app(argc, argv); QCommandLineParser parser; parser.addOption({QStringLiteral("source"), QStringLiteral("PipeWire source name"), QStringLiteral("source")}); parser.addOption({QStringLiteral("model"), QStringLiteral("Installed local model identifier"), QStringLiteral("model")}); parser.process(app); modernime::VoiceWorker worker(parser.value(QStringLiteral("source")), parser.value(QStringLiteral("model"))); auto bus = QDBusConnection::sessionBus(); if (!bus.registerService(QStringLiteral("org.modernime.VoiceWorker1")) || !bus.registerObject(QStringLiteral("/org/modernime/VoiceWorker1"), &worker, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)) return 1; return app.exec(); }
