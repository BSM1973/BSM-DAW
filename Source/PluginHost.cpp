#include "PluginHost.h"

namespace
{
class PluginEditorWindow final : public juce::DocumentWindow
{
public:
    PluginEditorWindow(const juce::String& title, juce::AudioProcessorEditor* editor)
        : juce::DocumentWindow(title, juce::Colour(0xff15181d), juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
        setContentOwned(editor, true);
        centreWithSize(juce::jmax(420, editor->getWidth()), juce::jmax(260, editor->getHeight()));
        setVisible(true);
    }

    void closeButtonPressed() override { setVisible(false); }
};
}

LibertyPluginHost& LibertyPluginHost::instance()
{
    static LibertyPluginHost host;
    return host;
}

bool LibertyPluginHost::runSingleVST3ScanHelper(const juce::String& pluginIdentifier,
                                                const juce::File& resultFile)
{
    juce::AudioPluginFormatManager helperFormats;
    helperFormats.addDefaultFormats();

    for (int i = 0; i < helperFormats.getNumFormats(); ++i)
    {
        auto* format = helperFormats.getFormat(i);
        if (format == nullptr || format->getName() != "VST3")
            continue;

        juce::KnownPluginList resultList;
        juce::OwnedArray<juce::PluginDescription> found;
        resultList.scanAndAddFile(pluginIdentifier, false, found, *format);
        if (found.isEmpty())
            return false;

        if (auto xml = resultList.createXml())
            return resultFile.replaceWithText(xml->toString(), true, true);

        return false;
    }

    return false;
}

LibertyPluginHost::LibertyPluginHost()
{
    formatManager.addDefaultFormats();
    loadCachedPluginList();
    recoverCrashedPluginsFromDeadMansPedal();
    loadPersistentBlacklist();
}

LibertyPluginHost::~LibertyPluginHost()
{
    shutdown();
}

void LibertyPluginHost::initialise(double sampleRate, int blockSize)
{
    if (shutdownCompleted.load(std::memory_order_acquire))
        return;

    const juce::ScopedLock scoped(lock);
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    currentBlockSize = juce::jmax(16, blockSize);
    for (auto& slot : trackEffects) prepareSlot(slot);
    prepareSlot(instrument);
}

void LibertyPluginHost::shutdown()
{
    // shutdown() is called explicitly by Liberty and again by the static
    // singleton destructor. Hosted plugins must only be released once.
    if (shutdownCompleted.exchange(true, std::memory_order_acq_rel))
        return;

    const juce::ScopedLock scoped(lock);
    for (auto& slot : trackEffects)
    {
        closeEditor(slot);
        if (slot.processor) slot.processor->releaseResources();
        slot.processor.reset();
        slot.description = {};
    }
    closeEditor(instrument);
    if (instrument.processor) instrument.processor->releaseResources();
    instrument.processor.reset();
    instrument.description = {};
}

juce::File LibertyPluginHost::pluginListFile() const
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("BSM").getChildFile("Liberty");
    dir.createDirectory();
    return dir.getChildFile("KnownPlugins.xml");
}

juce::File LibertyPluginHost::deadMansPedalFile() const
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("BSM").getChildFile("Liberty");
    dir.createDirectory();
    return dir.getChildFile("PluginScanDeadMansPedal.txt");
}

juce::File LibertyPluginHost::blacklistFolder() const
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("BSM").getChildFile("Liberty").getChildFile("Blacklist");
    dir.createDirectory();
    return dir;
}

juce::File LibertyPluginHost::blacklistTextFile() const
{
    return blacklistFolder().getChildFile("BlacklistedPlugins.txt");
}

juce::File LibertyPluginHost::getBlacklistFolder() const
{
    return blacklistFolder();
}

void LibertyPluginHost::loadCachedPluginList()
{
    const auto file = pluginListFile();
    if (!file.existsAsFile()) return;
    if (auto xml = juce::XmlDocument::parse(file)) knownPlugins.recreateFromXml(*xml);
}

void LibertyPluginHost::saveCachedPluginList()
{
    if (auto xml = knownPlugins.createXml()) pluginListFile().replaceWithText(xml->toString());
}

void LibertyPluginHost::loadPersistentBlacklist()
{
    juce::StringArray lines;
    blacklistTextFile().readLines(lines);
    lines.removeEmptyStrings();
    lines.removeDuplicates(false);
    for (const auto& pluginID : lines)
        knownPlugins.addToBlacklist(pluginID);
}

void LibertyPluginHost::savePersistentBlacklist()
{
    auto entries = knownPlugins.getBlacklistedFiles();
    entries.removeEmptyStrings();
    entries.removeDuplicates(false);
    blacklistTextFile().replaceWithText(entries.joinIntoString("\n"), true, true);
}

void LibertyPluginHost::recoverCrashedPluginsFromDeadMansPedal()
{
    const auto deadFile = deadMansPedalFile();
    juce::StringArray crashed;
    deadFile.readLines(crashed);
    crashed.removeEmptyStrings();
    crashed.removeDuplicates(false);
    if (crashed.isEmpty()) return;

    juce::StringArray persistent;
    blacklistTextFile().readLines(persistent);
    persistent.removeEmptyStrings();

    for (const auto& pluginID : crashed)
    {
        knownPlugins.addToBlacklist(pluginID);
        if (!persistent.contains(pluginID)) persistent.add(pluginID);
    }

    blacklistTextFile().replaceWithText(persistent.joinIntoString("\n"), true, true);
    saveCachedPluginList();
    deadFile.deleteFile();
}

juce::StringArray LibertyPluginHost::getBlacklistedPlugins() const
{
    const juce::ScopedLock scoped(lock);
    return knownPlugins.getBlacklistedFiles();
}

void LibertyPluginHost::clearBlacklist()
{
    const juce::ScopedLock scoped(lock);
    knownPlugins.clearBlacklistedFiles();
    blacklistTextFile().deleteFile();
    deadMansPedalFile().deleteFile();
    saveCachedPluginList();
}

void LibertyPluginHost::blacklistPluginIdentifier(const juce::String& identifier)
{
    if (identifier.isEmpty()) return;
    knownPlugins.addToBlacklist(identifier);
    savePersistentBlacklist();
    saveCachedPluginList();
}

bool LibertyPluginHost::scanVST3OutOfProcess(const juce::String& identifier,
                                             const juce::String&)
{
    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    const auto unique = juce::String::toHexString((juce::int64)juce::Time::getHighResolutionTicks())
                      + "_" + juce::String(juce::Random::getSystemRandom().nextInt());
    const auto resultFile = tempDir.getChildFile("LibertyVST3Scan_" + unique + ".xml");
    resultFile.deleteFile();

    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    juce::StringArray arguments;
    arguments.add(executable.getFullPathName());
    arguments.add("--liberty-scan-vst3");
    arguments.add(identifier);
    arguments.add(resultFile.getFullPathName());

    juce::ChildProcess child;
    if (!child.start(arguments, 0))
    {
        blacklistPluginIdentifier(identifier);
        return false;
    }

    constexpr int maximumScanTimeMs = 45000;
    if (!child.waitForProcessToFinish(maximumScanTimeMs))
    {
        child.kill();
        blacklistPluginIdentifier(identifier);
        resultFile.deleteFile();
        return false;
    }

    const auto exitCode = child.getExitCode();
    if (exitCode != 0 || !resultFile.existsAsFile())
    {
        blacklistPluginIdentifier(identifier);
        resultFile.deleteFile();
        return false;
    }

    auto xml = juce::XmlDocument::parse(resultFile);
    resultFile.deleteFile();
    if (xml == nullptr)
    {
        blacklistPluginIdentifier(identifier);
        return false;
    }

    juce::KnownPluginList isolatedResult;
    isolatedResult.recreateFromXml(*xml);
    const auto discovered = isolatedResult.getTypes();
    if (discovered.isEmpty())
    {
        blacklistPluginIdentifier(identifier);
        return false;
    }

    for (const auto& description : discovered)
        knownPlugins.addType(description);

    saveCachedPluginList();
    return true;
}

void LibertyPluginHost::scanInstalledPlugins(const ScanProgressCallback& progressCallback)
{
    const juce::ScopedLock scoped(lock);

    loadPersistentBlacklist();

    for (int formatIndex = 0; formatIndex < formatManager.getNumFormats(); ++formatIndex)
    {
        auto* format = formatManager.getFormat(formatIndex);
        if (format == nullptr) continue;
        const auto formatName = format->getName();
        if (formatName != "AudioUnit" && formatName != "VST3") continue;

        const auto locations = format->getDefaultLocationsToSearch();
        const auto candidates = format->searchPathsForPlugins(locations, true, false);
        if (candidates.isEmpty()) continue;

        if (formatName == "VST3")
        {
            const auto blacklist = knownPlugins.getBlacklistedFiles();
            for (int candidateIndex = 0; candidateIndex < candidates.size(); ++candidateIndex)
            {
                const auto& identifier = candidates.getReference(candidateIndex);
                const float progress = candidates.size() > 0
                    ? static_cast<float>(candidateIndex) / static_cast<float>(candidates.size())
                    : 1.0f;
                const auto pluginName = format->getNameOfPluginFromIdentifier(identifier);

                if (progressCallback)
                    progressCallback(formatName, pluginName, progress);

                if (blacklist.contains(identifier))
                    continue;

                if (knownPlugins.isListingUpToDate(identifier, *format))
                    continue;

                scanVST3OutOfProcess(identifier, pluginName);
            }

            savePersistentBlacklist();
            saveCachedPluginList();
            continue;
        }

        juce::PluginDirectoryScanner scanner(knownPlugins,
                                             *format,
                                             locations,
                                             true,
                                             deadMansPedalFile(),
                                             false);
        scanner.setFilesOrIdentifiersToScan(candidates);

        for (int candidate = 0; candidate < candidates.size(); ++candidate)
        {
            const auto pluginName = scanner.getNextPluginFileThatWillBeScanned();
            if (progressCallback)
                progressCallback(formatName, pluginName, scanner.getProgress());

            const int knownBefore = knownPlugins.getNumTypes();
            juce::String scannedName;
            const bool more = scanner.scanNextFile(true, scannedName);

            if (knownPlugins.getNumTypes() != knownBefore)
                saveCachedPluginList();

            savePersistentBlacklist();
            if (!more) break;
        }
    }

    savePersistentBlacklist();
    saveCachedPluginList();
    deadMansPedalFile().deleteFile();
    if (progressCallback) progressCallback({}, {}, 1.0f);
}

juce::Array<juce::PluginDescription> LibertyPluginHost::getPluginDescriptions() const
{
    const juce::ScopedLock scoped(lock);
    return knownPlugins.getTypes();
}

void LibertyPluginHost::prepareSlot(Slot& slot)
{
    if (!slot.processor) return;
    slot.processor->setRateAndBufferSizeDetails(currentSampleRate, currentBlockSize);
    slot.processor->prepareToPlay(currentSampleRate, currentBlockSize);
}

bool LibertyPluginHost::loadIntoSlot(Slot& slot,
                                     const juce::PluginDescription& description,
                                     juce::String& error,
                                     bool requireInstrument)
{
    if (requireInstrument && !description.isInstrument)
    {
        error = "Ce plugin n'est pas un instrument virtuel.";
        return false;
    }
    if (!requireInstrument && description.isInstrument)
    {
        error = "Ce plugin est un instrument. Charge-le sur la piste Instrument.";
        return false;
    }

    auto instance = formatManager.createPluginInstance(description,
                                                       currentSampleRate,
                                                       currentBlockSize,
                                                       error);
    if (!instance) return false;

    closeEditor(slot);
    if (slot.processor) slot.processor->releaseResources();
    slot.processor = std::move(instance);
    slot.description = description;
    prepareSlot(slot);
    return true;
}

bool LibertyPluginHost::loadEffectForTrack(int trackIndex,
                                           const juce::PluginDescription& description,
                                           juce::String& error)
{
    if (!validTrack(trackIndex)) { error = "Piste Audio invalide."; return false; }
    const juce::ScopedLock scoped(lock);
    return loadIntoSlot(trackEffects[(size_t)trackIndex], description, error, false);
}

bool LibertyPluginHost::loadInstrument(const juce::PluginDescription& description, juce::String& error)
{
    const juce::ScopedLock scoped(lock);
    return loadIntoSlot(instrument, description, error, true);
}

void LibertyPluginHost::closeEditor(Slot& slot)
{
    if (slot.editorWindow)
    {
        slot.editorWindow->setVisible(false);
        slot.editorWindow.reset();
    }
}

void LibertyPluginHost::unloadEffectForTrack(int trackIndex)
{
    if (!validTrack(trackIndex)) return;
    const juce::ScopedLock scoped(lock);
    auto& slot = trackEffects[(size_t)trackIndex];
    closeEditor(slot);
    if (slot.processor) slot.processor->releaseResources();
    slot.processor.reset();
    slot.description = {};
}

void LibertyPluginHost::unloadInstrument()
{
    const juce::ScopedLock scoped(lock);
    closeEditor(instrument);
    if (instrument.processor) instrument.processor->releaseResources();
    instrument.processor.reset();
    instrument.description = {};
}

bool LibertyPluginHost::hasEffectForTrack(int trackIndex) const
{
    if (!validTrack(trackIndex)) return false;
    const juce::ScopedLock scoped(lock);
    return trackEffects[(size_t)trackIndex].processor != nullptr;
}

bool LibertyPluginHost::hasInstrument() const
{
    const juce::ScopedLock scoped(lock);
    return instrument.processor != nullptr;
}

juce::String LibertyPluginHost::getEffectName(int trackIndex) const
{
    if (!validTrack(trackIndex)) return {};
    const juce::ScopedLock scoped(lock);
    return trackEffects[(size_t)trackIndex].processor ? trackEffects[(size_t)trackIndex].description.name : juce::String{};
}

juce::String LibertyPluginHost::getInstrumentName() const
{
    const juce::ScopedLock scoped(lock);
    return instrument.processor ? instrument.description.name : juce::String{};
}

void LibertyPluginHost::beginAudioTrackBlock(int trackIndex,
                                             float* const* outputChannelData,
                                             int numOutputChannels,
                                             int numSamples)
{
    if (!validTrack(trackIndex) || numSamples <= 0) return;
    if (!lock.tryEnter()) return;
    auto& slot = trackEffects[(size_t)trackIndex];
    if (!slot.processor) { lock.exit(); return; }

    const int channels = juce::jlimit(1, 2, numOutputChannels);
    slot.baseline.setSize(channels, numSamples, false, false, true);
    for (int ch = 0; ch < channels; ++ch)
    {
        if (outputChannelData[ch] != nullptr)
            slot.baseline.copyFrom(ch, 0, outputChannelData[ch], numSamples);
        else
            slot.baseline.clear(ch, 0, numSamples);
    }
    lock.exit();
}

void LibertyPluginHost::endAudioTrackBlock(int trackIndex,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples)
{
    if (!validTrack(trackIndex) || numSamples <= 0) return;
    if (!lock.tryEnter()) return;
    auto& slot = trackEffects[(size_t)trackIndex];
    if (!slot.processor) { lock.exit(); return; }

    const int channels = juce::jlimit(1, 2, numOutputChannels);
    slot.work.setSize(juce::jmax(2, channels), numSamples, false, false, true);
    slot.work.clear();
    for (int ch = 0; ch < channels; ++ch)
    {
        if (outputChannelData[ch] != nullptr)
        {
            slot.work.copyFrom(ch, 0, outputChannelData[ch], numSamples);
            slot.work.addFrom(ch, 0, slot.baseline, ch, 0, numSamples, -1.0f);
        }
    }

    juce::MidiBuffer emptyMidi;
    slot.processor->processBlock(slot.work, emptyMidi);

    for (int ch = 0; ch < channels; ++ch)
    {
        if (outputChannelData[ch] == nullptr) continue;
        juce::FloatVectorOperations::copy(outputChannelData[ch], slot.baseline.getReadPointer(ch), numSamples);
        juce::FloatVectorOperations::add(outputChannelData[ch], slot.work.getReadPointer(ch), numSamples);
    }
    lock.exit();
}

bool LibertyPluginHost::processInstrument(float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          juce::MidiBuffer& midi)
{
    if (numSamples <= 0 || numOutputChannels <= 0) return false;
    if (!lock.tryEnter()) return false;
    if (!instrument.processor) { lock.exit(); return false; }

    const int channels = juce::jlimit(1, 2, numOutputChannels);
    instrument.work.setSize(juce::jmax(2, channels), numSamples, false, false, true);
    instrument.work.clear();
    instrument.processor->processBlock(instrument.work, midi);

    for (int ch = 0; ch < channels; ++ch)
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::add(outputChannelData[ch], instrument.work.getReadPointer(ch), numSamples);

    lock.exit();
    return true;
}

void LibertyPluginHost::showEditor(Slot& slot, const juce::String& title)
{
    if (!slot.processor) return;
    if (slot.editorWindow)
    {
        slot.editorWindow->setVisible(true);
        slot.editorWindow->toFront(true);
        return;
    }
    if (auto* editor = slot.processor->createEditorIfNeeded())
        slot.editorWindow = std::make_unique<PluginEditorWindow>(title, editor);
}

void LibertyPluginHost::showEditorForTrack(int trackIndex)
{
    if (!validTrack(trackIndex)) return;
    const juce::ScopedLock scoped(lock);
    auto& slot = trackEffects[(size_t)trackIndex];
    showEditor(slot, "Liberty - " + slot.description.name);
}

void LibertyPluginHost::showInstrumentEditor()
{
    const juce::ScopedLock scoped(lock);
    showEditor(instrument, "Liberty - " + instrument.description.name);
}
