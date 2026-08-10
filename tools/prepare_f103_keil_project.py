#!/usr/bin/env python3
"""Populate the F103 project with the local Jiangke University StdPeriph template."""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]


def add_file(group: ET.Element, name: str, path: str, file_type: int) -> None:
    files = group.find("Files")
    if files is None:
        files = ET.SubElement(group, "Files")
    entry = ET.SubElement(files, "File")
    ET.SubElement(entry, "FileName").text = name
    ET.SubElement(entry, "FileType").text = str(file_type)
    ET.SubElement(entry, "FilePath").text = path


def add_group(groups: ET.Element, name: str, files: list[tuple[str, str, int]]) -> None:
    group = ET.SubElement(groups, "Group")
    ET.SubElement(group, "GroupName").text = name
    ET.SubElement(group, "Files")
    for file_name, file_path, file_type in files:
        add_file(group, file_name, file_path, file_type)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Regenerate the F103 Keil skeleton from a StdPeriph template."
    )
    parser.add_argument(
        "--template",
        required=True,
        type=Path,
        help="template directory containing Start, Library, User and Project.uvprojx",
    )
    parser.add_argument(
        "--destination",
        type=Path,
        default=ROOT / "firmware_f103",
        help="destination firmware directory (default: repository firmware_f103)",
    )
    args = parser.parse_args()
    template = args.template.expanduser().resolve()
    destination = args.destination.expanduser().resolve()

    required = [
        template / "Start",
        template / "Library",
        template / "User" / "stm32f10x_conf.h",
        template / "Project.uvprojx",
        template / "Project.uvoptx",
    ]
    missing = [str(path) for path in required if not path.exists()]
    if missing:
        raise SystemExit("template is incomplete:\n" + "\n".join(missing))

    shutil.copytree(template / "Start", destination / "Start", dirs_exist_ok=True)
    shutil.copytree(template / "Library", destination / "Library", dirs_exist_ok=True)
    shutil.copy2(template / "User" / "stm32f10x_conf.h",
                 destination / "User" / "stm32f10x_conf.h")
    shutil.copy2(template / "Project.uvprojx", destination / "Project.uvprojx")
    shutil.copy2(template / "Project.uvoptx", destination / "Project.uvoptx")

    project_path = destination / "Project.uvprojx"
    tree = ET.parse(project_path)
    root = tree.getroot()
    for include in root.findall(".//IncludePath"):
        if include.text and ".\\Start" in include.text:
            include.text = r".\Start;.\User;.\Library;.\BSP;.\App;..\common"
    for output_name in root.findall(".//OutputName"):
        output_name.text = "EdgeNodeF103"
    for define in root.findall(".//Define"):
        values = [item.strip() for item in (define.text or "").split(",") if item.strip()]
        if "USE_STDPERIPH_DRIVER" in values and "STM32F10X_MD" not in values:
            values.append("STM32F10X_MD")
            define.text = ",".join(values)

    groups = root.find(".//Groups")
    if groups is None:
        raise SystemExit("Groups element missing")
    existing = {node.text for node in groups.findall("./Group/GroupName")}
    if "App" not in existing:
        add_group(groups, "App", [("app_config.h", r".\App\app_config.h", 5)])
    if "BSP" not in existing:
        bsp_files = []
        for path in sorted((destination / "BSP").glob("*")):
            if path.suffix.lower() in {".c", ".h"}:
                bsp_files.append((path.name, rf".\BSP\{path.name}", 1 if path.suffix.lower() == ".c" else 5))
        add_group(groups, "BSP", bsp_files)
    if "Common" not in existing:
        add_group(groups, "Common", [
            ("edge_protocol.c", r"..\common\edge_protocol.c", 1),
            ("edge_protocol.h", r"..\common\edge_protocol.h", 5),
        ])

    ET.indent(tree, space="  ")
    tree.write(project_path, encoding="utf-8", xml_declaration=True)
    print(project_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
