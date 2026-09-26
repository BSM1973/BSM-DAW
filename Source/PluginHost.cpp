#include "PluginHost.h"
#include "OneKnobEffects.h"

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
    for (auto& slot : trackEffects) if (slot) prepareSlot(*slot);
    for (auto& slot : instruments) if (slot) prepareSlot(*slot);
}

void LibertyPluginHost::shutdown()
{
    // shutdown() is called explicitly by Liberty and again by the static
    // singleton destructor. Hosted plugins must only be released once.
    if (shutdownCompleted.exchange(true, std::memory_order_acq_rel))
        return;

    const juce::ScopedLock scoped(lock);
    for (auto& slot : trackEffects) if (slot)
    {
        closeEditor(*slot);
        if (slot->processor) slot->processor->releaseResources();
        slot->processor.reset(); slot->description = {};
    }
    for (auto& slot : instruments) if (slot)
    {
        closeEditor(*slot);
        if (slot->processor) slot->processor->releaseResources();
        slot->processor.reset(); slot->description = {};
    }
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

void LibertyPluginHost::ensureAudioTrackSlot(int trackIndex)
{
    while ((int) trackEffects.size() <= trackIndex) trackEffects.push_back(std::make_unique<Slot>());
}
void LibertyPluginHost::ensureInstrumentSlot(int instrumentTrack)
{
    while ((int) instruments.size() <= instrumentTrack) instruments.push_back(std::make_unique<Slot>());
}

bool LibertyPluginHost::loadEffectForTrack(int trackIndex,
                                           const juce::PluginDescription& description,
                                           juce::String& error)
{
    if (!validTrack(trackIndex)) { error = "Piste Audio invalide."; return false; }
    const juce::ScopedLock scoped(lock);
    ensureAudioTrackSlot(trackIndex);
    return loadIntoSlot(*trackEffects[(size_t)trackIndex], description, error, false);
}

bool LibertyPluginHost::loadInstrument(const juce::PluginDescription& description, juce::String& error)
{ return loadInstrumentForTrack(0, description, error); }
bool LibertyPluginHost::loadInstrumentForTrack(int instrumentTrack, const juce::PluginDescription& description, juce::String& error)
{
    if (instrumentTrack < 0) { error = "Piste Instrument invalide."; return false; }
    const juce::ScopedLock scoped(lock); ensureInstrumentSlot(instrumentTrack);
    return loadIntoSlot(*instruments[(size_t)instrumentTrack], description, error, true);
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
    if (trackIndex >= (int)trackEffects.size() || !trackEffects[(size_t)trackIndex]) return;
    auto& slot = *trackEffects[(size_t)trackIndex];
    closeEditor(slot); if (slot.processor) slot.processor->releaseResources(); slot.processor.reset(); slot.description = {};
}

void LibertyPluginHost::unloadInstrument(){ unloadInstrumentForTrack(0); }
void LibertyPluginHost::unloadInstrumentForTrack(int instrumentTrack)
{
    const juce::ScopedLock scoped(lock);
    if (instrumentTrack < 0 || instrumentTrack >= (int)instruments.size() || !instruments[(size_t)instrumentTrack]) return;
    auto& slot=*instruments[(size_t)instrumentTrack]; closeEditor(slot);
    if(slot.processor)slot.processor->releaseResources(); slot.processor.reset(); slot.description={};
}

bool LibertyPluginHost::hasEffectForTrack(int trackIndex) const
{
    if (!validTrack(trackIndex)) return false;
    const juce::ScopedLock scoped(lock);
    return trackIndex < (int)trackEffects.size() && trackEffects[(size_t)trackIndex] && trackEffects[(size_t)trackIndex]->processor != nullptr;
}

bool LibertyPluginHost::hasInstrument() const { return hasInstrumentForTrack(0); }
bool LibertyPluginHost::hasInstrumentForTrack(int t) const
{
    const juce::ScopedLock scoped(lock);
    return t>=0 && t<(int)instruments.size() && instruments[(size_t)t] && instruments[(size_t)t]->processor!=nullptr;
}

juce::String LibertyPluginHost::getEffectName(int trackIndex) const
{
    if (!validTrack(trackIndex)) return {};
    const juce::ScopedLock scoped(lock);
    return trackIndex<(int)trackEffects.size() && trackEffects[(size_t)trackIndex] && trackEffects[(size_t)trackIndex]->processor ? trackEffects[(size_t)trackIndex]->description.name : juce::String{};
}

bool LibertyPluginHost::getEffectDescriptionForTrack(int t, juce::PluginDescription& out) const
{
    const juce::ScopedLock sl(lock);
    if(t<0||t>=(int)trackEffects.size()||!trackEffects[(size_t)t]||!trackEffects[(size_t)t]->processor)return false;
    out=trackEffects[(size_t)t]->description; return true;
}
bool LibertyPluginHost::getInstrumentDescriptionForTrack(int t, juce::PluginDescription& out) const
{
    const juce::ScopedLock sl(lock);
    if(t<0||t>=(int)instruments.size()||!instruments[(size_t)t]||!instruments[(size_t)t]->processor)return false;
    out=instruments[(size_t)t]->description; return true;
}
bool LibertyPluginHost::findKnownPluginByIdentifier(const juce::String& id, juce::PluginDescription& out) const
{
    if(id.isEmpty())return false;
    for(const auto& d:knownPlugins.getTypes())if(d.fileOrIdentifier==id){out=d;return true;}
    return false;
}
juce::String LibertyPluginHost::getInstrumentName() const { return getInstrumentNameForTrack(0); }
juce::String LibertyPluginHost::getInstrumentNameForTrack(int t) const
{
    const juce::ScopedLock scoped(lock);
    return t>=0&&t<(int)instruments.size()&&instruments[(size_t)t]&&instruments[(size_t)t]->processor?instruments[(size_t)t]->description.name:juce::String{};
}

void LibertyPluginHost::beginAudioTrackBlock(int trackIndex,
                                             float* const* outputChannelData,
                                             int numOutputChannels,
                                             int numSamples)
{
    if (!validTrack(trackIndex) || numSamples <= 0) return;
    if (!lock.tryEnter()) return;
    if (trackIndex >= (int)trackEffects.size() || !trackEffects[(size_t)trackIndex]) return;
    auto& slot = *trackEffects[(size_t)trackIndex];
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
    if (trackIndex >= (int)trackEffects.size() || !trackEffects[(size_t)trackIndex]) return;
    auto& slot = *trackEffects[(size_t)trackIndex];
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

bool LibertyPluginHost::processInstrument(float* const* d,int ch,int n,juce::MidiBuffer& midi){return processInstrumentForTrack(0,d,ch,n,midi);}
bool LibertyPluginHost::processInstrumentForTrack(int t,float* const* outputChannelData,int numOutputChannels,int numSamples,juce::MidiBuffer& midi,float gain,float pan)
{
    if(numSamples<=0||numOutputChannels<=0||t<0)return false;if(!lock.tryEnter())return false;
    if(t>=(int)instruments.size()||!instruments[(size_t)t]||!instruments[(size_t)t]->processor){lock.exit();return false;}
    auto& instrument=*instruments[(size_t)t];const int channels=juce::jlimit(1,2,numOutputChannels);
    instrument.work.setSize(juce::jmax(2,channels),numSamples,false,false,true);instrument.work.clear();
    instrument.processor->processBlock(instrument.work,midi);
    gain=juce::jlimit(0.f,2.f,gain);pan=juce::jlimit(-1.f,1.f,pan);
    const float leftGain=gain*(pan>0.f?1.f-pan:1.f),rightGain=gain*(pan<0.f?1.f+pan:1.f);
    for(int c=0;c<channels;++c)if(outputChannelData[c])juce::FloatVectorOperations::addWithMultiply(outputChannelData[c],instrument.work.getReadPointer(c),c==0?leftGain:rightGain,numSamples);
    lock.exit();return true;
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
    if (trackIndex >= (int)trackEffects.size() || !trackEffects[(size_t)trackIndex]) return;
    auto& slot = *trackEffects[(size_t)trackIndex];
    showEditor(slot, "Liberty - " + slot.description.name);
}

void LibertyPluginHost::showInstrumentEditor(){showInstrumentEditorForTrack(0);}
void LibertyPluginHost::showInstrumentEditorForTrack(int t)
{
    const juce::ScopedLock scoped(lock);
    if(t<0||t>=(int)instruments.size()||!instruments[(size_t)t])return;
    auto& slot=*instruments[(size_t)t];showEditor(slot,"Liberty - "+slot.description.name);
}
