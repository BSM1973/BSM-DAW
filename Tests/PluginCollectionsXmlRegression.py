#!/usr/bin/env python3
"""Source-level regression plus UTF-8 XML fixtures for Liberty collections.

This checks invariants without JUCE; the macOS CMake build validates JUCE code.
"""
from pathlib import Path
import unittest
from xml.etree import ElementTree as ET

SOURCE = (Path(__file__).resolve().parents[1] / "Source" / "BrowserController.cpp").read_text(encoding="utf-8")
MAX_BYTES = 5 * 1024 * 1024


def cpp_function(marker):
    begin = SOURCE.index(marker)
    opening = SOURCE.index("{", begin)
    depth = 0
    for i in range(opening, len(SOURCE)):
        if SOURCE[i] == "{":
            depth += 1
        elif SOURCE[i] == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[begin:i + 1]
    raise AssertionError("Unclosed C++ function: " + marker)


class PluginCollectionsRegression(unittest.TestCase):
    def test_one_parser_for_startup_and_import(self):
        reader = cpp_function("static bool readPluginCollectionsFile(")
        importer = cpp_function("void importPluginCollections()")
        startup = cpp_function("void loadPluginCollections()")
        self.assertIn("readPluginCollectionsFile(source, incoming, &incomingTitles)", importer)
        self.assertIn("readPluginCollectionsFile(file, recovered, &titles)", startup)
        self.assertNotIn("XmlDocument::parse(source)", importer)
        self.assertIn("source.getSize() > maxPluginCollectionsBytes", reader)
        self.assertIn("!seenMemberKeys.insert(key).second", reader)

    def test_import_is_staged_and_reversible(self):
        importer = cpp_function("void importPluginCollections()")
        self.assertIn("auto stagedCollections = before;", importer)
        self.assertIn("auto stagedTitles = oldTitles;", importer)
        self.assertIn("safe->pluginCollections = std::move(stagedCollections);", importer)
        self.assertIn("safe->pluginCollectionTitles = oldTitles;", importer)
        self.assertIn("commitPluginCollections(before,", importer)
        self.assertLess(importer.index("if (!valid || total > 20000)"),
                        importer.index("safe->pluginCollections = std::move(stagedCollections);"))

    def test_same_size_and_membership_limits(self):
        reader = cpp_function("static bool readPluginCollectionsFile(")
        writer = cpp_function("bool savePluginCollections()")
        policy = cpp_function("bool pluginCollectionsWithinLimits() const")
        for cap in ("2048", "20000", "64"):
            self.assertIn(cap, reader)
            self.assertIn(cap, policy)
        self.assertIn("!seenKeys.insert(key).second", policy)
        self.assertIn("xml.getNumBytesAsUTF8()", writer)
        self.assertIn("maxPluginCollectionsBytes", writer)

    def test_utf8_labels_and_escaping_round_trip(self):
        root = ET.Element("LibertyPluginCollections", version="1")
        group = ET.SubElement(root, "collection", name="Synthés & Nappes")
        key = 'VST3|Éditeur & Synthé|Pad <Air> "Large"'
        label = "Nappe céleste - Éditeur (VST3)"
        ET.SubElement(group, "plugin", id=key, display=label)
        encoded = ET.tostring(root, encoding="utf-8")
        self.assertLess(len(encoded), MAX_BYTES)
        self.assertIn(b"&amp;", encoded)
        self.assertIn(b"&lt;", encoded)
        decoded = ET.fromstring(encoded).find("./collection/plugin")
        self.assertIsNotNone(decoded)
        self.assertEqual(decoded.get("id"), key)
        self.assertEqual(decoded.get("display"), label)

    def test_xml_escaping_can_exceed_five_megabytes(self):
        root = ET.Element("LibertyPluginCollections", version="1")
        group = ET.SubElement(root, "collection", name="Pads")
        for n in range(600):
            ET.SubElement(group, "plugin",
                          id="&" * 2040 + f"{n:08d}", display="Pad")
        payload = ET.tostring(root, encoding="utf-8")
        self.assertGreater(len(payload), MAX_BYTES)
        self.assertLess(len(group), 20000)
        self.assertTrue(all(len(item.get("id")) <= 2048 for item in group))

    def test_export_uses_prevalidated_bytes(self):
        exporter = cpp_function("void exportPluginCollections()")
        self.assertIn("pluginCollectionsWithinLimits()", exporter)
        self.assertIn("exportXml.getNumBytesAsUTF8()", exporter)
        self.assertIn("[safe, chooser, xml = exportXml]", exporter)


if __name__ == "__main__":
    unittest.main()
