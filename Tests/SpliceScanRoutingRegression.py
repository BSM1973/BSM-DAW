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

    def test_splice_local_scan_uses_worker_thread(self):
        self.assertIn("void requestLibertySpliceSampleScan(MainComponent* owner)", SPLICE)
        self.assertIn("it->second->requestSampleScan();", SPLICE)
        self.assertIn("splicePanel->startScan();", SPLICE)
        start = SPLICE.index("void startScan()")
        end = SPLICE.index("\n    juce::TextButton ", start)
        self.assertIn("scanThread = std::thread(", SPLICE[start:end])
        self.assertIn("juce::MessageManager::callAsync", SPLICE[start:end])


if __name__ == "__main__":
    unittest.main()
