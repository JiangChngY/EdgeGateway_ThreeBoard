#!/usr/bin/env python3
"""Add the gateway Activity to a COPY of ALIENTEK systemui source, before qmake."""
import argparse
from pathlib import Path
import shutil

def integrate(root):
    root = root.resolve()
    source = Path(__file__).resolve().parent
    module_files = ('edgegatewaybackend.h', 'edgegatewaybackend.cpp', 'Activity.qml',
                    'Dashboard.qml', 'main.qml', 'edgegateway.qrc', 'edgegateway.pri', 'icon.svg')
    # Fail before patching the vendor copy if the supplied module is incomplete.
    for name in module_files:
        if not (source / name).is_file():
            raise RuntimeError("Missing gateway module file: " + str(source / name))
    main = root / "main.cpp"
    page = root / "Page1.qml"
    project = root / "systemui.pro"
    originals = {p: p.read_text(encoding="utf-8") for p in (main, page, project)}
    updates = dict(originals)
    if '#include "edgegatewaybackend.h"' not in updates[main]:
        anchor = '#include <QGuiApplication>'
        if anchor not in updates[main]:
            raise RuntimeError("Unsupported systemui main.cpp: QGuiApplication include missing")
        updates[main] = updates[main].replace(anchor, anchor + '\n#include "edgegatewaybackend.h"', 1)
    if 'setContextProperty("gateway"' not in updates[main]:
        anchor = '    QQmlApplicationEngine engine;'
        if anchor not in updates[main]:
            raise RuntimeError("Unsupported systemui main.cpp: engine declaration missing")
        # Backend must outlive the QML engine (reverse destruction order).
        updates[main] = updates[main].replace(anchor,
            '    EdgeGatewayBackend gateway;\n' + anchor +
            '\n    engine.rootContext()->setContextProperty("gateway", &gateway);', 1)
    if 'name: "edgegateway"' not in updates[page]:
        anchor = '        id: listModel1'
        if anchor not in updates[page]:
            raise RuntimeError("Unsupported Page1.qml: listModel1 missing")
        updates[page] = updates[page].replace(anchor, anchor + '''
        ListElement {
            path: "qrc:/edgegateway/Activity.qml"
            name: "edgegateway"
            cName: qsTr("边缘采集")
            iconPath: "qrc:/edgegateway/icon.svg"
        }''', 1)
    line = 'include(apps/edgegateway/edgegateway.pri)'
    updates[page] = updates[page].replace('iconPath: "qrc:/appicons/sensor.png"',
                                        'iconPath: "qrc:/edgegateway/icon.svg"')
    if line not in updates[project]:
        updates[project] += '\n' + line + '\n'
    # The vendor project lists common.qrc once relatively and once absolutely.
    # Normalize before unique(), otherwise qmake emits duplicate rcc/link rules.
    marker = '# BEGIN EDGEGATEWAY RESOURCE NORMALIZATION'
    if marker not in updates[project]:
        updates[project] += '''
# BEGIN EDGEGATEWAY RESOURCE NORMALIZATION
EDGEGATEWAY_RESOURCES =
for(resource, RESOURCES) {
    EDGEGATEWAY_RESOURCES += $$quote($$absolute_path($$resource, $$PWD))
}
RESOURCES = $$unique(EDGEGATEWAY_RESOURCES)
# END EDGEGATEWAY RESOURCE NORMALIZATION
'''
    # Validate all anchors before making any changes, and retain first originals.
    for path, content in updates.items():
        if content != originals[path]:
            backup = path.with_name(path.name + '.pre-edgegateway')
            if not backup.exists():
                shutil.copy2(path, backup)
            # Path.write_text(newline=...) is unavailable on Python 3.8 SDK hosts.
            with path.open('w', encoding='utf-8', newline='\n') as output:
                output.write(content)
    dest = root / 'apps' / 'edgegateway'
    dest.mkdir(parents=True, exist_ok=True)
    for name in module_files:
        target = dest / name
        if not target.is_file() or target.read_bytes() != (source / name).read_bytes():
            shutil.copy2(source / name, target)
    print('Integrated edgegateway into', root)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('systemui', type=Path)
    integrate(parser.parse_args().systemui)
