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
