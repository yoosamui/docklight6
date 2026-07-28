#include "geometry_bridge.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QVariantList>

#include <utility>

namespace
{

constexpr char SERVICE_NAME[] =
    "org.docklight6.WindowIntegration";

constexpr char OBJECT_PATH[] =
    "/org/docklight6/WindowIntegration";

constexpr char INTERFACE_NAME[] =
    "org.docklight6.WindowIntegration1";

}

QDBusArgument &operator<<(
    QDBusArgument &argument,
    const IconGeometryEntry &entry)
{
    argument.beginStructure();
    argument << entry.internal_id
             << entry.x
             << entry.y
             << entry.width
             << entry.height;
    argument.endStructure();

    return argument;
}

const QDBusArgument &operator>>(
    const QDBusArgument &argument,
    IconGeometryEntry &entry)
{
    argument.beginStructure();
    argument >> entry.internal_id
             >> entry.x
             >> entry.y
             >> entry.width
             >> entry.height;
    argument.endStructure();

    return argument;
}

GeometryBridge::GeometryBridge(
    QObject *parent)
    : QObject(parent),
      m_service_watcher(
          QString::fromLatin1(
              SERVICE_NAME),
          QDBusConnection::sessionBus(),
          QDBusServiceWatcher::
              WatchForRegistration |
              QDBusServiceWatcher::
                  WatchForUnregistration,
          this)
{
    qDBusRegisterMetaType<
        IconGeometryEntry>();
    qDBusRegisterMetaType<
        IconGeometryEntries>();

    auto bus =
        QDBusConnection::sessionBus();

    bus.connect(
        QString::fromLatin1(
            SERVICE_NAME),
        QString::fromLatin1(
            OBJECT_PATH),
        QString::fromLatin1(
            INTERFACE_NAME),
        QStringLiteral(
            "IconGeometryChanged"),
        this,
        SLOT(iconGeometryChanged(QString,int,int,int,int)));

    bus.connect(
        QString::fromLatin1(
            SERVICE_NAME),
        QString::fromLatin1(
            OBJECT_PATH),
        QString::fromLatin1(
            INTERFACE_NAME),
        QStringLiteral(
            "IconGeometryRemoved"),
        this,
        SLOT(iconGeometryRemoved(QString)));

    bus.connect(
        QString::fromLatin1(
            SERVICE_NAME),
        QString::fromLatin1(
            OBJECT_PATH),
        QString::fromLatin1(
            INTERFACE_NAME),
        QStringLiteral(
            "DockSurfaceGeometryChanged"),
        this,
        SLOT(dockSurfaceGeometryChanged(bool,int,int,int,int)));

    connect(
        &m_service_watcher,
        &QDBusServiceWatcher::
            serviceRegistered,
        this,
        &GeometryBridge::
            serviceRegistered);

    connect(
        &m_service_watcher,
        &QDBusServiceWatcher::
            serviceUnregistered,
        this,
        &GeometryBridge::
            serviceUnregistered);

    loadSnapshot();
}

quint64 GeometryBridge::revision() const
{
    return m_revision;
}

bool GeometryBridge::connected() const
{
    return m_connected;
}

QVariantMap GeometryBridge::
    surfaceGeometry() const
{
    return m_surface_geometry;
}

QVariantMap GeometryBridge::geometryFor(
    const QVariant &window_ids) const
{
    QVariantList identifiers;

    if (window_ids.canConvert<
            QVariantList>())
    {
        identifiers =
            window_ids.toList();
    }
    else
    {
        identifiers.push_back(
            window_ids);
    }

    for (const auto &identifier :
         identifiers)
    {
        const auto geometry =
            m_geometries.constFind(
                normalizedId(
                    identifier.toString()));

        if (geometry !=
            m_geometries.constEnd())
        {
            return *geometry;
        }
    }

    return {};
}

void GeometryBridge::loadSnapshot()
{
    auto interface =
        new QDBusInterface(
            QString::fromLatin1(
                SERVICE_NAME),
            QString::fromLatin1(
                OBJECT_PATH),
            QString::fromLatin1(
                INTERFACE_NAME),
            QDBusConnection::sessionBus(),
            this);

    if (!interface->isValid())
    {
        interface->deleteLater();
        setConnected(false);
        return;
    }

    auto watcher =
        new QDBusPendingCallWatcher(
            interface->asyncCall(
                QStringLiteral(
                    "GetIconGeometries")),
            interface);

    connect(
        watcher,
        &QDBusPendingCallWatcher::finished,
        this,
        &GeometryBridge::
            finishSnapshot);

    loadSurfaceGeometry();
}

void GeometryBridge::finishSnapshot(
    QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<
        IconGeometryEntries>
        reply = *watcher;

    auto interface =
        watcher->parent();

    watcher->deleteLater();

    if (interface)
        interface->deleteLater();

    if (reply.isError())
    {
        setConnected(false);
        return;
    }

    QHash<QString, QVariantMap>
        geometries;

    for (const auto &entry :
         reply.value())
    {
        QVariantMap geometry;

        geometry.insert(
            QStringLiteral("x"),
            entry.x);
        geometry.insert(
            QStringLiteral("y"),
            entry.y);
        geometry.insert(
            QStringLiteral("width"),
            entry.width);
        geometry.insert(
            QStringLiteral("height"),
            entry.height);

        geometries.insert(
            normalizedId(
                entry.internal_id),
            geometry);
    }

    m_geometries =
        std::move(geometries);

    ++m_revision;
    emit geometriesChanged();
    setConnected(true);
}

void GeometryBridge::loadSurfaceGeometry()
{
    auto interface =
        new QDBusInterface(
            QString::fromLatin1(
                SERVICE_NAME),
            QString::fromLatin1(
                OBJECT_PATH),
            QString::fromLatin1(
                INTERFACE_NAME),
            QDBusConnection::sessionBus(),
            this);

    if (!interface->isValid())
    {
        interface->deleteLater();
        return;
    }

    auto watcher =
        new QDBusPendingCallWatcher(
            interface->asyncCall(
                QStringLiteral(
                    "GetDockSurfaceGeometry")),
            interface);

    connect(
        watcher,
        &QDBusPendingCallWatcher::finished,
        this,
        &GeometryBridge::
            finishSurfaceGeometry);
}

void GeometryBridge::finishSurfaceGeometry(
    QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<
        bool,
        int,
        int,
        int,
        int>
        reply = *watcher;

    auto interface =
        watcher->parent();

    watcher->deleteLater();

    if (interface)
        interface->deleteLater();

    if (reply.isError())
        return;

    dockSurfaceGeometryChanged(
        reply.argumentAt<0>(),
        reply.argumentAt<1>(),
        reply.argumentAt<2>(),
        reply.argumentAt<3>(),
        reply.argumentAt<4>());
}

void GeometryBridge::serviceRegistered()
{
    loadSnapshot();
}

void GeometryBridge::serviceUnregistered()
{
    const bool had_geometries =
        !m_geometries.isEmpty();

    m_geometries.clear();
    const bool had_surface =
        !m_surface_geometry.isEmpty();
    m_surface_geometry.clear();

    if (had_geometries)
    {
        ++m_revision;
        emit geometriesChanged();
    }

    if (had_surface)
        emit surfaceGeometryChanged();

    setConnected(false);
}

void GeometryBridge::iconGeometryChanged(
    const QString &internal_id,
    int x,
    int y,
    int width,
    int height)
{
    QVariantMap geometry;

    geometry.insert(
        QStringLiteral("x"),
        x);
    geometry.insert(
        QStringLiteral("y"),
        y);
    geometry.insert(
        QStringLiteral("width"),
        width);
    geometry.insert(
        QStringLiteral("height"),
        height);

    const auto key =
        normalizedId(internal_id);

    if (m_geometries.value(key) ==
        geometry)
    {
        return;
    }

    m_geometries.insert(
        key,
        geometry);

    ++m_revision;
    emit geometriesChanged();
    setConnected(true);
}

void GeometryBridge::iconGeometryRemoved(
    const QString &internal_id)
{
    if (m_geometries.remove(
            normalizedId(
                internal_id)) == 0)
    {
        return;
    }

    ++m_revision;
    emit geometriesChanged();
}

void GeometryBridge::
    dockSurfaceGeometryChanged(
        bool available,
        int x,
        int y,
        int width,
        int height)
{
    QVariantMap geometry;

    if (available &&
        width > 0 &&
        height > 0)
    {
        geometry.insert(
            QStringLiteral("x"),
            x);
        geometry.insert(
            QStringLiteral("y"),
            y);
        geometry.insert(
            QStringLiteral("width"),
            width);
        geometry.insert(
            QStringLiteral("height"),
            height);
    }

    if (m_surface_geometry == geometry)
        return;

    m_surface_geometry =
        std::move(geometry);
    emit surfaceGeometryChanged();
}

QString GeometryBridge::normalizedId(
    const QString &internal_id)
{
    auto normalized =
        internal_id.trimmed().toLower();

    if (normalized.startsWith(
            QLatin1Char('{')) &&
        normalized.endsWith(
            QLatin1Char('}')))
    {
        normalized =
            normalized.mid(
                1,
                normalized.size() - 2);
    }

    return normalized;
}

void GeometryBridge::setConnected(
    bool connected)
{
    if (m_connected == connected)
        return;

    m_connected = connected;
    emit connectedChanged();
}
