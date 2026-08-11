from __future__ import annotations

import ast
import configparser
import re
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ProjectStructureTests(unittest.TestCase):
    def test_required_delivery_files_exist(self):
        required = [
            "README.md",
            ".gitignore",
            ".gitattributes",
            ".github/workflows/host-ci.yml",
            "common/edge_protocol.c",
            "common/edge_protocol.h",
            "firmware_f103/Project.uvprojx",
            "mp157_hmi/mp157_hmi.pro",
            "imx6ull_aggregator/imx6ull_aggregator.pro",
            "config/mp157.ini",
            "config/imx6ull.ini",
            "docs/02_接线说明.md",
            "docs/06_协议与网络联调.md",
            "docs/07_系统演示.md",
            "docs/09_代码审查修复记录.md",
            "docs/10_项目逻辑与流程图.md",
            "tests/c_protocol_test.c",
        ]
        missing = [relative for relative in required if not (ROOT / relative).is_file()]
        self.assertEqual(missing, [])

        public_suffixes = {".md", ".py", ".ini", ".sh", ".service", ".yml", ".yaml"}
        private_path_pattern = re.compile(
            r"(?:[A-Za-z]:\\(?:Users|32|codex_)\\|C:/Users/|/home/[^/]+/)"
        )
        leaked_paths = []
        for path in ROOT.rglob("*"):
            if (path.is_file() and path.resolve() != Path(__file__).resolve()
                    and path.suffix.lower() in public_suffixes):
                if private_path_pattern.search(path.read_text(encoding="utf-8")):
                    leaked_paths.append(str(path.relative_to(ROOT)))
        self.assertEqual(leaked_paths, [])

    def test_keil_project_is_valid_and_all_sources_exist(self):
        project_path = ROOT / "firmware_f103" / "Project.uvprojx"
        tree = ET.parse(project_path)
        project = tree.getroot()
        defines = ",".join((node.text or "") for node in project.findall(".//Define"))
        self.assertIn("USE_STDPERIPH_DRIVER", defines)
        self.assertIn("STM32F10X_MD", defines)
        self.assertTrue(any((node.text or "") == "STM32F103C8"
                            for node in project.findall(".//Device")))

        missing = []
        base = project_path.parent
        for node in project.findall(".//FilePath"):
            relative = (node.text or "").replace("\\", "/")
            if relative and not (base / relative).resolve().is_file():
                missing.append(relative)
        self.assertEqual(missing, [])

    def test_qmake_projects_reference_existing_files(self):
        token_pattern = re.compile(r"(?:\.\./)?[A-Za-z0-9_./-]+\.(?:c|cpp|h)\b")
        missing = []
        for relative in ("mp157_hmi/mp157_hmi.pro",
                         "imx6ull_aggregator/imx6ull_aggregator.pro"):
            project = ROOT / relative
            text = project.read_text(encoding="utf-8")
            for token in token_pattern.findall(text):
                if not (project.parent / token).resolve().is_file():
                    missing.append(f"{relative}: {token}")
        self.assertEqual(missing, [])

    def test_ini_files_parse(self):
        for path in sorted((ROOT / "config").glob("*.ini")):
            parser = configparser.ConfigParser()
            with path.open("r", encoding="utf-8") as stream:
                parser.read_file(stream)
            self.assertGreater(len(parser.sections()), 0, path.name)

    def test_python_files_parse(self):
        for path in ROOT.rglob("*.py"):
            ast.parse(path.read_text(encoding="utf-8"), filename=str(path))

    def test_deployment_scripts_use_linux_line_endings(self):
        for path in sorted((ROOT / "deploy").glob("*.sh")):
            content = path.read_bytes()
            self.assertTrue(content.startswith(b"#!/bin/sh\n"), path.name)
            self.assertNotIn(b"\r\n", content, path.name)


if __name__ == "__main__":
    unittest.main(verbosity=2)
