pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid
import org.kde.taskmanager as TaskManager
import "bridge" as DocklightBridge

PlasmoidItem {
    id: root

    Layout.minimumWidth: 1
    Layout.minimumHeight: 1
    Layout.preferredWidth: 1
    Layout.preferredHeight: 1
    Layout.maximumWidth: 1
    Layout.maximumHeight: 1

    Plasmoid.backgroundHints:
        PlasmaCore.Types.NoBackground
    Plasmoid.status:
        PlasmaCore.Types.ActiveStatus

    preferredRepresentation:
        compactRepresentation

    fullRepresentation: Item {}

    DocklightBridge.GeometryBridge {
        id: geometryBridge
    }

    TaskManager.TasksModel {
        id: tasksModel

        groupMode:
            TaskManager.TasksModel.GroupDisabled
        sortMode:
            TaskManager.TasksModel.SortDisabled

        filterByVirtualDesktop: false
        filterByScreen: false
        filterByActivity: false
    }

    compactRepresentation: Item {
        implicitWidth: 1
        implicitHeight: 1
    }

    PlasmaCore.Dialog {
        id: docklightSurface

        readonly property var geometry:
            geometryBridge.surfaceGeometry
        readonly property bool geometryAvailable:
            geometry.width > 0 &&
            geometry.height > 0

        x: geometry.x ?? 0
        y: geometry.y ?? 0
        width: geometry.width ?? 1
        height: geometry.height ?? 1

        visible:
            geometryBridge.connected &&
            geometryAvailable

        type: PlasmaCore.Dialog.Dock
        backgroundHints:
            PlasmaCore.Types.NoBackground
        flags:
            Qt.FramelessWindowHint |
            Qt.WindowDoesNotAcceptFocus |
            Qt.WindowTransparentForInput

        mainItem: Item {
            id: bridgeSurface

            width: docklightSurface.width
            height: docklightSurface.height
            opacity: 0

            function publishGeometries(): void {
                for (let index = 0;
                     index < windowRepeater.count;
                     ++index) {
                    const item =
                        windowRepeater.itemAt(index);

                    if (item)
                        item.publishGeometry();
                }
            }

            Repeater {
                id: windowRepeater

                model: tasksModel

                onCountChanged:
                    publishTimer.restart()

                delegate: Item {
                    id: windowDelegate

                    required property int index
                    required property var model

                    readonly property var iconGeometry: {
                        geometryBridge.revision;
                        return geometryBridge.geometryFor(
                            model.WinIdList);
                    }

                    x:
                        (iconGeometry.x ?? 0) -
                        docklightSurface.x

                    y:
                        (iconGeometry.y ?? 0) -
                        docklightSurface.y

                    width:
                        iconGeometry.width ?? 0
                    height:
                        iconGeometry.height ?? 0

                    opacity: 0

                    function publishGeometry(): void {
                        if (!geometryBridge.connected ||
                            !docklightSurface.visible ||
                            width <= 0 ||
                            height <= 0) {
                            return;
                        }

                        tasksModel
                            .requestPublishDelegateGeometry(
                                tasksModel.makeModelIndex(
                                    index),
                                Qt.rect(
                                    iconGeometry.x,
                                    iconGeometry.y,
                                    width,
                                    height),
                                windowDelegate);
                    }

                    onIconGeometryChanged:
                        publishTimer.restart()
                    onXChanged:
                        publishTimer.restart()
                    onYChanged:
                        publishTimer.restart()
                    onWidthChanged:
                        publishTimer.restart()
                    onHeightChanged:
                        publishTimer.restart()

                    Component.onCompleted:
                        publishTimer.restart()
                }
            }

            Timer {
                id: publishTimer

                interval: 50
                repeat: false

                onTriggered:
                    bridgeSurface.publishGeometries()
            }

            Component.onCompleted:
                publishTimer.restart()
        }

        onXChanged:
            publishTimer.restart()
        onYChanged:
            publishTimer.restart()
        onWidthChanged:
            publishTimer.restart()
        onHeightChanged:
            publishTimer.restart()
        onVisibleChanged:
            publishTimer.restart()
    }
}
