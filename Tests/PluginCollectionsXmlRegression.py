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


    def test_import_preview_requires_explicit_approval(self):
        importer = cpp_function("void importPluginCollections()")
        self.assertIn("Aperçu de l'importation", importer)
        self.assertIn("showOkCancelBox(", importer)
        self.assertIn("sourceRevision = safe->collectionsRevision", importer)
        self.assertIn("safe->collectionsRevision != sourceRevision", importer)
        self.assertIn("confirmed != 1", importer)
        self.assertIn("Associations déjà présentes", importer)
        self.assertIn("Associations ajoutées", importer)
        self.assertLess(importer.index("showOkCancelBox("),
                        importer.index("safe->pluginCollections = std::move(stagedCollections);"))

    def test_import_preview_checks_merged_xml_before_approval(self):
        importer = cpp_function("void importPluginCollections()")
        self.assertIn("previewXml.toString().getNumBytesAsUTF8()", importer)
        self.assertIn("maxPluginCollectionsBytes", importer)
        self.assertIn("if (created == 0 && added == 0 && addedTitles == 0)", importer)
        self.assertLess(importer.index("previewXml.toString().getNumBytesAsUTF8()"),
                        importer.index("showOkCancelBox("))


    def test_missing_plugins_global_report_is_read_only_and_deduplicates(self):
        calculator = cpp_function("GlobalMissingPluginSummary globalMissingPluginSummary() const")
        summary = cpp_function("void showGlobalMissingPluginReport()")
        copy = cpp_function("void copyGlobalMissingPluginReport()")
        self.assertIn("installedPluginIdentifiers()", calculator)
        self.assertIn("result.uniqueIdentifiers.insert(key)", calculator)
        self.assertIn("++result.associations", calculator)
        self.assertIn("summary.uniqueIdentifiers.size()", summary)
        self.assertIn("summary.byCollection", summary)
        builder = cpp_function("juce::String makeGlobalMissingPluginTsv(")
        self.assertIn("quotedReportField(id)", builder)
        self.assertIn("makeGlobalMissingPluginTsv(summary)", copy)
        self.assertIn("copyTextToClipboard(makeGlobalMissingPluginTsv(summary))", copy)
        self.assertNotIn("savePluginCollections(", summary)
        self.assertNotIn("commitPluginCollections(", copy)

    def test_missing_report_is_available_in_collection_filters(self):
        filters = cpp_function("void showPluginFiltersMenu(")
        self.assertIn("collectionsMenu.addItem(5405,", filters)
        self.assertIn("collectionsMenu.addItem(5406,", filters)
        self.assertIn("safe->showGlobalMissingPluginReport();", filters)
        self.assertIn("safe->copyGlobalMissingPluginReport();", filters)

    def test_global_missing_counts_distinguish_repeated_associations(self):
        installed = {"VST3|Synth|Stable"}
        saved = {
            "Pads": ["VST3|Synth|Stable", "AU|Synth|Missing", "VST3|FX|Missing"],
            "FX": ["VST3|FX|Missing"],
        }
        missing = [(group, plugin) for group, members in saved.items()
                   for plugin in members if plugin not in installed]
        self.assertEqual(len(missing), 3)
        self.assertEqual(len({plugin for _, plugin in missing}), 2)
        self.assertEqual({group for group, _ in missing}, {"Pads", "FX"})



    def test_global_missing_tsv_file_export_preserves_exact_identifiers(self):
        builder = cpp_function(
            "juce::String makeGlobalMissingPluginTsv(")
        copier = cpp_function("void copyGlobalMissingPluginReport()")
        exporter = cpp_function("void exportGlobalMissingPluginReport()")
        self.assertIn('quotedReportField(id)', builder)
        self.assertIn('quotedReportField(group.first)', builder)
        self.assertIn('makeGlobalMissingPluginTsv(summary)', copier)
        self.assertIn('makeGlobalMissingPluginTsv(summary)', exporter)
        self.assertIn("Liberty-Plugins-Manquants.tsv", exporter)
        self.assertIn("warnAboutOverwriting", exporter)
        self.assertIn("juce::String::charToString(0xfeff)", exporter)
        self.assertIn('tsv.replace("\\n", "\\r\\n")', exporter)
        self.assertIn("juce::TemporaryFile staged(file);", exporter)
        self.assertIn("staged.overwriteTargetFileWithTemporary()", exporter)
        self.assertNotIn("savePluginCollections(", exporter)
        self.assertNotIn("commitPluginCollections(", exporter)

    def test_global_missing_tsv_export_is_available_from_filters(self):
        filters = cpp_function("void showPluginFiltersMenu(")
        self.assertIn("collectionsMenu.addItem(5407,", filters)
        self.assertIn("safe->exportGlobalMissingPluginReport();", filters)


    def test_complete_inventory_reports_scanned_and_missing_statuses(self):
        generator = cpp_function("CollectionInventorySnapshot makeCollectionInventoryTsv() const")
        self.assertIn("knownById.emplace(id, &description)", generator)
        self.assertIn("result.absentFromScan", generator)
        self.assertIn("result.present", generator)
        self.assertIn("ABSENT DU SCAN", generator)
        self.assertIn("PRÉSENT", generator)
        self.assertIn("NON DÉTERMINÉ", generator)
        self.assertIn("description->pluginFormatName.trim()", generator)
        self.assertIn("description->isInstrument", generator)
        self.assertIn("quotedReportField(id)", generator)
        self.assertNotIn("savePluginCollections(", generator)

    def test_complete_inventory_export_is_atomic_and_preserves_accents(self):
        exporter = cpp_function("void exportCollectionInventoryTsv()")
        filters = cpp_function("void showPluginFiltersMenu(")
        self.assertIn("makeCollectionInventoryTsv()", exporter)
        self.assertIn("Liberty-Inventaire-Collections.tsv", exporter)
        self.assertIn("juce::String::charToString(0xfeff)", exporter)
        self.assertIn("snapshot.tsv.replace", exporter)
        self.assertIn("juce::TemporaryFile staged(destination);", exporter)
        self.assertIn("staged.overwriteTargetFileWithTemporary()", exporter)
        self.assertIn("scanning.load()", exporter)
        self.assertIn("collectionsMenu.addItem(5408,", filters)
        self.assertIn("safe->exportCollectionInventoryTsv();", filters)


if __name__ == "__main__":
    unittest.main()
