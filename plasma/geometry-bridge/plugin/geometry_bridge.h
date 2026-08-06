// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// File:
// geometry_bridge.h
//
// Purpose:
// Declares the QML-facing bridge for Docklight icon and dock-surface
// geometry published over the session D-Bus.
//
// Responsibilities:
// - Maintain the latest geometry snapshot received from Docklight.
// - Expose connection state, revisions, and geometry to QML.
// - Translate the D-Bus geometry structure to Qt value types.
//
// Dependencies and ownership:
// GeometryBridge owns its Qt containers and service watcher. QObject
// parent ownership applies to asynchronous D-Bus watchers.
//
// Design notes:
// The bridge keeps Plasma/QML consumers independent from Docklight's
// GTK implementation and normalizes window identifiers at its boundary.
//
// ------------------------------------------------------------

#pragma once

#include <QDBusArgument>
#include <QDBusServiceWatcher>
#include <QHash>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QVariantMap>

class QDBusPendingCallWatcher;

struct IconGeometryEntry
{
    QString internal_id;

    qint32 x = 0;
    qint32 y = 0;
    qint32 width = 0;
    qint32 height = 0;
};

using IconGeometryEntries =
    QList<IconGeometryEntry>;

Q_DECLARE_METATYPE(IconGeometryEntry)
Q_DECLARE_METATYPE(IconGeometryEntries)

QDBusArgument &operator<<(
    QDBusArgument &argument,
    const IconGeometryEntry &entry);

const QDBusArgument &operator>>(
    const QDBusArgument &argument,
    IconGeometryEntry &entry);

class GeometryBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(
        quint64 revision
        READ revision
        NOTIFY geometriesChanged)
    Q_PROPERTY(
        bool connected
        READ connected
        NOTIFY connectedChanged)
    Q_PROPERTY(
        QVariantMap surfaceGeometry
        READ surfaceGeometry
        NOTIFY surfaceGeometryChanged)

public:
    explicit GeometryBridge(
        QObject *parent = nullptr);

    quint64 revision() const;
    bool connected() const;
    QVariantMap surfaceGeometry() const;

    Q_INVOKABLE QVariantMap
    geometryFor(
        const QVariant &window_ids) const;

signals:
    void geometriesChanged();
    void connectedChanged();
    void surfaceGeometryChanged();

private slots:
    void loadSnapshot();
    void finishSnapshot(
        QDBusPendingCallWatcher *watcher);
    void loadSurfaceGeometry();
    void finishSurfaceGeometry(
        QDBusPendingCallWatcher *watcher);
    void serviceRegistered();
    void serviceUnregistered();
    void iconGeometryChanged(
        const QString &internal_id,
        int x,
        int y,
        int width,
        int height);
    void iconGeometryRemoved(
        const QString &internal_id);
    void dockSurfaceGeometryChanged(
        bool available,
        int x,
        int y,
        int width,
        int height);

private:
    static QString normalizedId(
        const QString &internal_id);
    void setConnected(bool connected);

private:
    QDBusServiceWatcher m_service_watcher;

    QHash<QString, QVariantMap>
        m_geometries;
    QVariantMap m_surface_geometry;

    quint64 m_revision = 0;

    bool m_connected = false;
};
