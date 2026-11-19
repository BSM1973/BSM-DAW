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
    def test_plugin_family_disclosure_icons_use_vectors_not_font_glyphs(self):
        item = SOURCE[SOURCE.index("class PluginTreeItem final"):
                      SOURCE.index("class BrowserPanel final")]
        self.assertIn("void paintOpenCloseButton(", item)
        self.assertIn('label == "INSTRUMENTS"', item)
        self.assertIn('label == "EFFETS AUDIO"', item)
        self.assertIn('juce::String::fromUTF8("RÉCENTS")', item)
        self.assertIn("g.drawRoundedRectangle(", item)
        self.assertIn("g.drawEllipse(", item)
        self.assertIn("g.drawLine(", item)
        self.assertNotIn("TreeViewItem::paintOpenCloseButton(", item)

    def test_plugin_groups_collapsed_until_explicit_filter(self):
        build = cpp_function("void rebuildPluginTree()")
        self.assertIn("pluginRoot->setOpen(true);", build)
        self.assertIn("fav->setOpen(revealMatches)", build)
        self.assertIn("recentGroup->setOpen(revealMatches)", build)
        self.assertIn("instruments->setOpen(revealMatches)", build)
        self.assertIn("audioEffects->setOpen(revealMatches)", build)
        self.assertIn("collectionsRoot->setOpen(revealMatches)", build)
        self.assertNotIn("instruments->setOpen(true);", build)
        self.assertNotIn("audioEffects->setOpen(true);", build)

    def test_independent_browser_roots_on_external_volumes(self):
        chooser = cpp_function("void chooseCategoryFolder()")
        self.assertIn("ProjectsBrowserPath.txt", SOURCE)
        self.assertIn("MidiBrowserPath.txt", SOURCE)
        self.assertIn("PresetsBrowserPath.txt", SOURCE)
        self.assertIn("canSelectDirectories", chooser)
        self.assertIn("chosenCategory", chooser)
        self.assertIn("replaceWithText(folder.getFullPathName())", chooser)
        category = cpp_function("void setCategory(Category newCategory)")
        self.assertIn("selectSavedCategoryFolder(category);", category)
        self.assertIn("chooseLibraryButton.setVisible(category == Category::audio", category)
        self.assertIn("category == Category::midi", category)
        self.assertIn("category == Category::presets", category)

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


    def test_missing_only_mode_searches_saved_names_and_exact_ids(self):
        tree = cpp_function("void rebuildPluginTree()")
        self.assertIn("if (missingPluginsOnly) return false;", tree)
        self.assertIn("collectionMemberTitle(key).containsIgnoreCase(query)", tree)
        self.assertIn("key.containsIgnoreCase(query)", tree)
        self.assertIn("const bool showMissing = missingPluginsOnly ||", tree)
        self.assertIn("showMissingPluginItemContextMenu(", tree)
        self.assertIn("visibleCount = 0;", tree)
        self.assertIn("if (fav != nullptr)", tree)
        self.assertIn("if (instruments != nullptr)", tree)

    def test_missing_only_filter_mode_can_reset_and_switch_facets(self):
        filters = cpp_function("void showPluginFiltersMenu()")
        tooltip = cpp_function("void refreshPluginFiltersButton(")
        self.assertIn("accessMenu.addItem(703,", filters)
        self.assertIn("safe->missingPluginsOnly = true;", filters)
        self.assertIn("safe->missingPluginsOnly = false;", filters)
        self.assertIn("safe->artworkView = ArtworkView::all;", filters)
        self.assertIn("missingPluginsOnly ? 1 : 0", tooltip)
        self.assertIn("Manquants uniquement", tooltip)


    def test_exact_match_repair_requires_unique_installed_identity(self):
        builder = cpp_function(
            "std::vector<MissingRepairProposal> missingRepairProposals() const")
        self.assertIn("candidates", builder)
        self.assertIn("collectionMemberTitle(oldId)", builder)
        self.assertIn("globalMissingPluginSummary()", builder)
        self.assertIn("result.push_back(", builder)
        self.assertIn("size() != 1", builder)

    def test_manual_relink_checks_live_descriptor_before_commit(self):
        confirm = cpp_function("void confirmMissingPluginRelink(")
        self.assertIn("installed.find(oldIdentifier)", confirm)
        self.assertIn("installed.find(nextIdentifier)", confirm)
        self.assertIn("exactCandidateStillInstalled", confirm)
        self.assertIn("live.manufacturerName == candidate.manufacturerName", confirm)
        self.assertIn("live.pluginFormatName == candidate.pluginFormatName", confirm)
        self.assertIn("live.isInstrument == candidate.isInstrument", confirm)
        self.assertIn("if (!exactCandidateStillInstalled) return;", confirm)
        self.assertIn("commitPluginCollections(before,", confirm)
        self.assertIn("safe->pluginCollectionTitles = titleBefore;", confirm)

    def test_bulk_repair_only_after_explicit_selection_and_recheck(self):
        table = cpp_function("void showSelectiveMissingRepair()")
        self.assertIn("table->chosen()", table)
        self.assertIn("selected.empty()", table)
        self.assertIn("showOkCancelBox(", table)
        self.assertIn("accepted != 1", table)
        self.assertIn("safe->missingRepairProposals()", table)
        self.assertIn("current.oldId == p.oldId", table)
        self.assertIn("current.newId == p.newId", table)
        self.assertIn("current.occurrences == p.occurrences", table)
        self.assertIn("commitPluginCollections(before,", table)
        self.assertIn("safe->pluginCollectionTitles = beforeTitles;", table)
        self.assertLess(table.index("accepted != 1"),
                        table.index("safe->pluginCollections;"))

    def test_five_level_undo_guards_and_rollback(self):
        can_undo = cpp_function("bool canUndoLastBulkRepair() const")
        record = cpp_function("void rememberBulkRepair(")
        undo = cpp_function("void undoLastBulkRepair()")
        self.assertIn("bulkRepairRevision == collectionsRevision", can_undo)
        self.assertIn("pluginCollections == bulkRepairHistory.back().after", can_undo)
        self.assertIn("bulkRepairHistory.back().after != before", record)
        self.assertIn("bulkRepairHistory.size() > 5", record)
        self.assertIn("bulkRepairHistory.erase(bulkRepairHistory.begin())", record)
        self.assertIn("!safe->canUndoLastBulkRepair()", undo)
        self.assertIn("safe->pluginCollections = entry.before;", undo)
        self.assertIn("safe->pluginCollectionTitles = entry.titlesBefore;", undo)
        self.assertIn("safe->commitPluginCollections(current,", undo)
        self.assertIn("safe->bulkRepairHistory.pop_back()", undo)
        self.assertIn("safe->pluginCollectionTitles = currentTitles;", undo)

    def test_persistence_guards_against_external_changes(self):
        writer = cpp_function("bool savePluginCollections()")
        transaction = cpp_function("bool commitPluginCollections(")
        self.assertIn("target.loadFileAsString() != collectionsDiskContents", writer)
        self.assertIn("collectionsReadOnly = true;", writer)
        self.assertIn("juce::TemporaryFile backupStage(backup);", writer)
        self.assertIn("backupStage.overwriteTargetFileWithTemporary()", writer)
        self.assertIn("juce::TemporaryFile staged(target);", writer)
        self.assertIn("staged.overwriteTargetFileWithTemporary()", writer)
        self.assertIn("++collectionsRevision;", writer)
        self.assertIn("pluginCollections = oldState;", transaction)

    def test_diagnostic_remains_read_only_without_confirmed_candidate(self):
        display = cpp_function("void showMissingSuggestionTable()")
        picker = cpp_function("void chooseDiagnosticMissingReplacement(")
        self.assertNotIn("commitPluginCollections(", display)
        self.assertIn("selectedOldId", display)
        self.assertIn("confirmMissingPluginRelink(", picker)
        self.assertNotIn("commitPluginCollections(", picker)
        self.assertIn("getNumItems()", picker)

    def test_comparison_copy_controls_do_not_mutate_collections(self):
        comparison = cpp_function("struct MissingRelinkComparison final")
        self.assertIn("copyTextToClipboard(previousId)", comparison)
        self.assertIn("copyTextToClipboard(replacementId)", comparison)
        self.assertIn("getWidth() < 610", comparison)
        self.assertNotIn("commitPluginCollections(", comparison)


if __name__ == "__main__":
    unittest.main()
