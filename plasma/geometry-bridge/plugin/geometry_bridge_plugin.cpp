// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// geometry_bridge_plugin.cpp
//
// Implementation overview:
// Registers GeometryBridge as the QML type exported by the Docklight Plasma
// geometry-bridge plugin.
//
// Important implementation decisions:
// - Type registration is isolated from the bridge implementation.
// - Qt owns the plugin and QML-created object lifetimes.
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
