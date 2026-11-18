#!/usr/bin/env python3
"""Guard the Splice button against triggering a full AU/VST3 scan."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
BROWSER = (ROOT / "Source/BrowserController.cpp").read_text(encoding="utf-8")
SPLICE = (ROOT / "Source/SpliceBrowserController.cpp").read_text(encoding="utf-8")


class SpliceRoutingTests(unittest.TestCase):
    def test_splice_scan_button_opens_local_library(self):
        self.assertIn(
            "spliceScanButton.onClick = [this] { requestLibertySpliceSampleScan(&owner); };",
            BROWSER)
        self.assertNotIn("spliceScanButton.onClick = [this] { scanPlugins(); };", BROWSER)

    def test_splice_open_does_not_auto_rescan_1800_plugins(self):
        start = BROWSER.index("void openSpliceSounds()")
        end = BROWSER.index("\n    void ", start + 5)
        self.assertNotIn("scanPlugins();", BROWSER[start:end])

    def test_splice_panel_attaches_to_current_french_browser_tabs(self):
        start = SPLICE.index("juce::Component* findBrowserPanel(")
        end = SPLICE.index("bool isAudioSample(", start)
        lookup = SPLICE[start:end]
        self.assertIn('findDirectButton(parent, "PROJETS")', lookup)
        self.assertIn('findDirectButton(parent, "SAMPLES")', lookup)
        self.assertIn('findDirectButton(parent, "SPLICE")', lookup)
        self.assertIn('findDirectButton(browserPanel, "PROJETS")', SPLICE)
        self.assertIn('findDirectButton(browserPanel, "SAMPLES")', SPLICE)

    def test_missing_sample_folder_prompts_user_and_displays_path(self):
        start = SPLICE.index("void requestLocalSampleScan()")
        end = SPLICE.index("    void paint(", start)
        request = SPLICE[start:end]
        self.assertIn("if (!spliceRoot.isDirectory())", request)
        self.assertIn("chooseSpliceFolder();", request)
        self.assertIn("startScan();", request)
        self.assertIn("saveRootFolder();", SPLICE)
        self.assertIn("spliceRoot.getFullPathName(), juce::dontSendNotification", SPLICE)

    def test_splice_local_scan_uses_worker_thread(self):
        self.assertIn("void requestLibertySpliceSampleScan(MainComponent* owner)", SPLICE)
        self.assertIn("it->second->requestSampleScan();", SPLICE)
        self.assertIn("splicePanel->requestLocalSampleScan();", SPLICE)
        self.assertIn("void requestLocalSampleScan()", SPLICE)
        self.assertIn("        showLibrary();\n        startScan();", SPLICE)
        self.assertNotIn("splicePanel->showLibrary();", SPLICE)
        self.assertNotIn("splicePanel->startScan();", SPLICE)
        start = SPLICE.index("void startScan()")
        end = SPLICE.index("\n    juce::TextButton ", start)
        self.assertIn("scanThread = std::thread(", SPLICE[start:end])
        self.assertIn("juce::MessageManager::callAsync", SPLICE[start:end])


if __name__ == "__main__":
    unittest.main()
