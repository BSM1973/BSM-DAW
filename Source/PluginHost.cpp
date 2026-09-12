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

LibertyPluginHost::LibertyPluginHost()
{
    formatManager.addDefaultFormats();
    loadCachedPluginList();
}

LibertyPluginHost::~LibertyPluginHost()
{
    shutdown();
}

void LibertyPluginHost::initialise(double sampleRate, int blockSize)
{
    const juce::ScopedLock scoped(lock);
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    currentBlockSize = juce::jmax(16, blockSize);
    for (auto& slot : trackEffects) prepareSlot(slot);
    prepareSlot(instrument);
}

void LibertyPluginHost::shutdown()
{
    const juce::ScopedLock scoped(lock);
    for (auto& slot : trackEffects)
    {
        closeEditor(slot);
        if (slot.processor) slot.processor->releaseResources();
        slot.processor.reset();
    }
    closeEditor(instrument);
    if (instrument.processor) instrument.processor->releaseResources();
    instrument.processor.reset();
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

void LibertyPluginHost::scanInstalledPlugins()
{
    const juce::ScopedLock scoped(lock);
    knownPlugins.clear();

    for (int formatIndex = 0; formatIndex < formatManager.getNumFormats(); ++formatIndex)
    {
        auto* format = formatManager.getFormat(formatIndex);
        if (format == nullptr) continue;
        const auto name = format->getName();
        if (name != "AudioUnit" && name != "VST3") continue;

        juce::PluginDirectoryScanner scanner(knownPlugins,
                                             *format,
                                             format->getDefaultLocationsToSearch(),
                                             true,
                                             deadMansPedalFile(),
                                             false);
        juce::String pluginBeingScanned;
        while (scanner.scanNextFile(true, pluginBeingScanned)) {}
    }

    saveCachedPluginList();
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
