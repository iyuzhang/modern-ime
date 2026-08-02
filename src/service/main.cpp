#include "service/service.h"
#include "contracts/logging.h"
#include "contracts/xdg.h"
#include <QCoreApplication>
#include <QDBusConnection>
int main(int argc, char **argv) { QCoreApplication app(argc, argv); app.setApplicationName(QStringLiteral("modern-ime-service")); modernime::ensureXdgDirectories(modernime::xdgPaths()); modernime::Service service; auto bus = QDBusConnection::sessionBus(); if (!bus.registerService(QStringLiteral("org.modernime.Service1")) || !bus.registerObject(QStringLiteral("/org/modernime/Service1"), &service, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)) { modernime::logError("service", "unable to acquire org.modernime.Service1"); return 1; } modernime::logInfo("service", "started"); return app.exec(); }
