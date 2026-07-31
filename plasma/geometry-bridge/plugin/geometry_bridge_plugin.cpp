// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// Registers GeometryBridge as the QML type provided by the Docklight
// Plasma geometry-bridge plugin.
//
// Qt owns plugin and QML-created object lifetimes. This translation
// unit contains registration only; D-Bus behavior remains in
// GeometryBridge.
//
// ------------------------------------------------------------

#include "geometry_bridge.h"

#include <QQmlExtensionPlugin>
#include <qqml.h>

class DocklightGeometryBridgePlugin
    : public QQmlExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(
        IID QQmlExtensionInterface_iid)

public:
    void registerTypes(
        const char *uri) override
    {
        qmlRegisterType<GeometryBridge>(
            uri,
            1,
            0,
            "GeometryBridge");
    }
};

#include "geometry_bridge_plugin.moc"
