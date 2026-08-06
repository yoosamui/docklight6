"use strict";

const packageId =
    "org.docklight6.geometrybridge";
const taskManagerIds = [
    "org.kde.plasma.icontasks",
    "org.kde.plasma.taskmanager"
];

const panelContainments = panels();
const allContainments =
    desktops().concat(panelContainments);

if (panelContainments.length === 0) {
    throw new Error(
        "No Plasma panel is available for the Docklight geometry bridge");
}

let targetPanel = null;

for (const panel of panelContainments) {
    for (const widgetId of panel.widgetIds) {
        const widget =
            panel.widgetById(widgetId);

        if (taskManagerIds.includes(
                widget.type)) {
            targetPanel = panel;
            break;
        }
    }

    if (targetPanel)
        break;
}

if (!targetPanel)
    targetPanel = panelContainments[0];

const instances = [];

for (const containment of allContainments) {
    for (const widgetId of
         containment.widgetIds) {
        const widget =
            containment.widgetById(
                widgetId);

        if (widget.type === packageId) {
            instances.push({
                containment,
                widget
            });
        }
    }
}

let retainedInstance = null;

for (const instance of instances) {
    if (instance.containment ===
        targetPanel) {
        retainedInstance = instance;
        break;
    }
}

if (!retainedInstance) {
    for (const instance of instances) {
        if (panelContainments.includes(
                instance.containment)) {
            retainedInstance = instance;
            break;
        }
    }
}

for (const instance of instances) {
    if (instance !== retainedInstance)
        instance.widget.remove();
}

if (!retainedInstance) {
    targetPanel.addWidget(packageId);

    const installed =
        targetPanel.widgetIds.some(
            widgetId =>
                targetPanel.widgetById(
                    widgetId).type ===
                packageId);

    if (!installed) {
        throw new Error(
            "Plasma could not create the Docklight geometry bridge");
    }
}
