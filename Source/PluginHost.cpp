#include <limits>
#include <cstdint>
#include "PluginHost.h"
#include "OneKnobEffects.h"
#if defined(LIBERTY_NATIVE_PREVIEW)
#include "Plugin.h"
#include "PluginProcessor.h"
#include "FactoryPresets.h"
#include <cmath>
#endif

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
#if defined(LIBERTY_NATIVE_PREVIEW)
    nativeDevices.registerFactory("liberty.native.freespirit-delay",
        []() -> std::unique_ptr<juce::AudioProcessor>
        { return std::make_unique<FreeSpiritProcessor>(); });
    nativeDevices.registerFactory("liberty.native.love-eq",
        []() -> std::unique_ptr<juce::AudioProcessor>
        { return std::make_unique<LoveEQAudioProcessor>(); });
#endif
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
    for (auto& chain : trackEffects) for (auto& slot : chain) if (slot) prepareSlot(*slot);
    for (auto& slot : instruments) if (slot) prepareSlot(*slot);
    for (auto& chain : instrumentEffects) for (auto& slot : chain) if (slot) prepareSlot(*slot);
}

void LibertyPluginHost::shutdown()
{
    // shutdown() is called explicitly by Liberty and again by the static
    // singleton destructor. Hosted plugins must only be released once.
    if (shutdownCompleted.exchange(true, std::memory_order_acq_rel))
        return;

    const juce::ScopedLock scoped(lock);
    for (auto& chain : trackEffects) for (auto& slot : chain) if (slot)
    {
        closeEditor(*slot); if (slot->processor) slot->processor->releaseResources(); slot->processor.reset(); slot->description = {};
    }
    for (auto& chain : instrumentEffects) for (auto& slot : chain) if (slot)
    {
        closeEditor(*slot); if (slot->processor) slot->processor->releaseResources(); slot->processor.reset(); slot->description = {};
    }
    for (auto& slot : instruments) if (slot)
    {
        closeEditor(*slot);
        if (slot->processor) slot->processor->releaseResources();
        slot->processor.reset(); slot->description = {};
    }
}

void LibertyPluginHost::clearProjectPlugins()
{
    const juce::ScopedLock scoped(lock);
    for (auto& chain : trackEffects) for (auto& slot : chain) if (slot)
    {
        closeEditor(*slot); if (slot->processor) slot->processor->releaseResources(); slot->processor.reset(); slot->description = {};
    }
    for (auto& chain : instrumentEffects) for (auto& slot : chain) if (slot)
    {
        closeEditor(*slot); if (slot->processor) slot->processor->releaseResources(); slot->processor.reset(); slot->description = {};
    }
    for (auto& slot : instruments) if (slot)
    {
        closeEditor(*slot); if (slot->processor) slot->processor->releaseResources(); slot->processor.reset(); slot->description = {};
    }
    trackEffects.clear();
    instrumentEffects.clear();
    instruments.clear();
    for(auto& present:realtimeInstrumentPresent)present.store(false,std::memory_order_release);
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

#if defined(LIBERTY_NATIVE_PREVIEW)
void LibertyPluginHost::setNativeTempo(double bpm) noexcept
{
    if (std::isfinite(bpm))
        nativePlayHead.bpm.store(juce::jlimit(20.0, 400.0, bpm), std::memory_order_relaxed);
}

juce::Array<juce::PluginDescription> LibertyPluginHost::nativeDescriptions() const
{
    juce::Array<juce::PluginDescription> descriptions;
    // Standard is an entitlement allow-list here, not a released edition.
    for (const auto& device : LibertyEditions::catalogue)
    {
        if (!nativeDevices.isAvailable(LibertyEditions::Edition::Standard, device.id)) continue;
        juce::PluginDescription d;
        d.name = juce::String::fromUTF8(device.name.data(), (int)device.name.size());
        d.descriptiveName = d.name;
        d.fileOrIdentifier = juce::String::fromUTF8(device.id.data(), (int)device.id.size());
        d.pluginFormatName = "Liberty Native";
        d.manufacturerName = "Liberty Music Software";
        d.category = device.id=="liberty.native.love-eq" ? "EQ" : "Delay";
        d.version = "0.2.1";
        d.uniqueId = d.fileOrIdentifier.hashCode();
        d.isInstrument = device.kind == LibertyEditions::Kind::Instrument;
        d.numInputChannels = d.isInstrument ? 0 : 2;
        d.numOutputChannels = 2;
        descriptions.add(d);
    }
    return descriptions;
}
#endif

juce::Array<juce::PluginDescription> LibertyPluginHost::getPluginDescriptions() const
{
    const juce::ScopedLock scoped(lock);
    auto result = knownPlugins.getTypes();
#if defined(LIBERTY_NATIVE_PREVIEW)
    result.addArray(nativeDescriptions());
#endif
    return result;
}

void LibertyPluginHost::prepareSlot(Slot& slot)
{
    if (!slot.processor) return;
    slot.processor->setRateAndBufferSizeDetails(currentSampleRate, currentBlockSize);
    slot.processor->prepareToPlay(currentSampleRate, currentBlockSize);
}

std::vector<LibertyPluginHost::NativePreset> LibertyPluginHost::getNativeFactoryPresets() const
{
    std::vector<NativePreset> result;
#if defined(LIBERTY_NATIVE_PREVIEW)
    for(const auto& d:nativeDescriptions())
        if(d.fileOrIdentifier=="liberty.native.freespirit-delay")
        {
            FreeSpiritProcessor original;
            for(int i=0;i<(int)original.presets.size();++i)
            {
                const auto& p=original.presets[(size_t)i];
                result.push_back({d.fileOrIdentifier+":factory:"+juce::String(i),p.name,p.category,d.fileOrIdentifier,i});
            }
        }
        else if(d.fileOrIdentifier=="liberty.native.love-eq")
        {
            const auto& presets=loveeq::factoryPresets();
            for(int i=0;i<(int)presets.size();++i)
            {
                const auto& p=presets[(size_t)i];
                result.push_back({d.fileOrIdentifier+":factory:"+juce::String(i),juce::String::fromUTF8(p.name),juce::String::fromUTF8(p.category),d.fileOrIdentifier,i});
            }
        }
#endif
    return result;
}
bool LibertyPluginHost::getNativeFactoryPresetState(const juce::String& id,int index,juce::MemoryBlock& out) const
{
    out.reset();
#if defined(LIBERTY_NATIVE_PREVIEW)
    juce::PluginDescription d;
    if(id=="liberty.native.freespirit-delay"&&findKnownPluginByIdentifier(id,d))
    {
        FreeSpiritProcessor original;
        if(index<0||index>=(int)original.presets.size())return false;
        original.loadPreset(index);original.getStateInformation(out);
        return validateNativePresetState(id,out);
    }
    if(id=="liberty.native.love-eq"&&findKnownPluginByIdentifier(id,d))
    {
        if(index<0||index>=(int)loveeq::factoryPresets().size())return false;
        LoveEQAudioProcessor original;original.loadFactoryPreset((size_t)index);original.getStateInformation(out);
        return validateNativePresetState(id,out);
    }
#endif
    return false;
}
bool LibertyPluginHost::validateNativePresetState(const juce::String& id,const juce::MemoryBlock& state) const
{
#if defined(LIBERTY_NATIVE_PREVIEW)
    if(state.getSize()==0||state.getSize()>1024*1024)return false;
    auto xml=juce::AudioProcessor::getXmlFromBinary(state.getData(),(int)state.getSize());
    if(!xml)return false;
    if(id=="liberty.native.freespirit-delay")return xml->hasTagName("FreeSpirit");
    if(id=="liberty.native.love-eq")return xml->hasTagName("LOVE_EQ_STATE");
    return false;
#else
    juce::ignoreUnused(id,state);return false;
#endif
}
bool LibertyPluginHost::captureNativeChain(bool instrument,int lane,NativeChain& out,juce::String& error) const
{
    const juce::ScopedLock guard(lock);
    out.clear();NativeChain staged;
    if(lane<0){error="Piste invalide.";return false;}
    const auto capture=[&](const Slot* slot,int position)
    {
        if(slot==nullptr||!slot->processor)return true;
        if(slot->description.pluginFormatName!="Liberty Native"){error=juce::String::fromUTF8("La chaîne contient un plugin externe non pris en charge.");return false;}
        ChainDevice item;item.slot=position;item.deviceId=slot->description.fileOrIdentifier;slot->processor->getStateInformation(item.state);
        if(!validateNativePresetState(item.deviceId,item.state)){error="Etat natif incompatible.";return false;}
        staged.push_back(std::move(item));return true;
    };
    if(instrument&&lane<(int)instruments.size()&&!capture(instruments[(size_t)lane].get(),-1))return false;
    const auto& effects=instrument?instrumentEffects:trackEffects;
    for(int i=0;i<effectSlotsPerTrack;++i)
    {
        if(LibertyOneKnobManager::instance().hasEffect(instrument?100000+lane*8+i:lane*8+i)){error="One Knob non pris en charge dans cette version.";return false;}
        if(lane<(int)effects.size()&&!capture(effects[(size_t)lane][(size_t)i].get(),i))return false;
    }
    if(staged.empty()){error=juce::String::fromUTF8("Aucun appareil natif à sauvegarder.");return false;}
    out=std::move(staged);return true;
}
bool LibertyPluginHost::insertNativeChain(bool instrument,int lane,const NativeChain& chain,juce::String& error)
{
    if(lane<0||lane>4096||chain.empty()||chain.size()>9){error=juce::String::fromUTF8("Chaîne invalide.");return false;}
    const juce::ScopedLock guard(lock);
    auto& effects=instrument?instrumentEffects:trackEffects;
    for(int i=0;i<effectSlotsPerTrack;++i)
        if(LibertyOneKnobManager::instance().hasEffect(instrument?100000+lane*8+i:lane*8+i)
            ||(lane<(int)effects.size()&&effects[(size_t)lane][(size_t)i]&&effects[(size_t)lane][(size_t)i]->processor))
        {error=juce::String::fromUTF8("Les huit slots FX doivent être libres.");return false;}
    std::array<std::unique_ptr<Slot>,effectSlotsPerTrack> staged{};
    std::unique_ptr<Slot> stagedInstrument;
    const auto releaseStaged=[&]{for(auto& x:staged)if(x&&x->processor)x->processor->releaseResources();if(stagedInstrument&&stagedInstrument->processor)stagedInstrument->processor->releaseResources();};
    for(const auto& item:chain)
    {
        if(item.slot<-1||item.slot>=effectSlotsPerTrack||(item.slot==-1&&!instrument)){error="Position incompatible.";releaseStaged();return false;}
        if(item.slot==-1&&lane<(int)instruments.size()&&instruments[(size_t)lane]&&instruments[(size_t)lane]->processor){error=juce::String::fromUTF8("La piste possède déjà un instrument.");releaseStaged();return false;}
        auto& dest=item.slot==-1?stagedInstrument:staged[(size_t)item.slot];
        juce::PluginDescription desc;
        if(dest||!findKnownPluginByIdentifier(item.deviceId,desc)||desc.pluginFormatName!="Liberty Native"||desc.isInstrument!=(item.slot==-1)||!validateNativePresetState(item.deviceId,item.state))
        {error="Appareil, position ou preset incompatible.";releaseStaged();return false;}
        dest=std::make_unique<Slot>();
        if(!loadIntoSlot(*dest,desc,error,item.slot==-1,&item.state)){releaseStaged();return false;}
    }
    // Publish only after every processor has been resolved, prepared and validated.
    while((int)effects.size()<=lane)effects.emplace_back();
    for(int i=0;i<effectSlotsPerTrack;++i)if(staged[(size_t)i])effects[(size_t)lane][(size_t)i]=std::move(staged[(size_t)i]);
    if(stagedInstrument){ensureInstrumentSlot(lane);instruments[(size_t)lane]=std::move(stagedInstrument);publishInstrumentPresence(lane);}
    return true;
}
bool LibertyPluginHost::renderNativeAudition(const NativeChain& chain,const juce::AudioBuffer<float>& source,double rate,double tempo,AuditionResult& out,juce::String& error,const std::atomic<bool>& cancel) const
{
#if defined(LIBERTY_NATIVE_PREVIEW)
    if(chain.empty()||chain.size()>8||!std::isfinite(rate)||!std::isfinite(tempo)||rate<8000||rate>96000||source.getNumChannels()!=2||source.getNumSamples()<1||source.getNumSamples()>rate*12)
    {error="Source ou chaine invalide.";return false;}
    for(int ch=0;ch<2;++ch)for(int i=0;i<source.getNumSamples();++i)if(!std::isfinite(source.getSample(ch,i))){error="Source audio non finie.";return false;}
    NativePlayHead head;head.bpm.store(juce::jlimit(20.,400.,tempo));
    std::vector<std::unique_ptr<juce::AudioProcessor>> processors;
    const auto release=[&]{for(auto& p:processors)p->releaseResources();};
    auto sorted=chain;std::sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.slot<b.slot;});
    int latency=0,last=-1;constexpr int block=256;
    for(const auto& device:sorted)
    {
        if(cancel.load()){release();return false;}
        if(device.slot<0||device.slot>7||device.slot==last){error="Position FX invalide.";release();return false;}last=device.slot;
        juce::PluginDescription description;
        if(!findKnownPluginByIdentifier(device.deviceId,description)||description.pluginFormatName!="Liberty Native"||description.isInstrument)
        {error=juce::String::fromUTF8("Appareil natif indisponible pour la préécoute.");release();return false;}
        auto p=nativeDevices.create(LibertyEditions::Edition::Standard,device.deviceId.toStdString());
        if(!p){error="Creation impossible.";release();return false;}
        if(device.state.getSize()>0){if(!validateNativePresetState(device.deviceId,device.state)){error="Preset incompatible.";release();return false;}p->setStateInformation(device.state.getData(),(int)device.state.getSize());}
        // A/B must not participate in the live project's shared Relationship EQ registry.
        // Reject relationship-dependent states instead of silently changing their sound.
        if(auto* eq=dynamic_cast<LoveEQAudioProcessor*>(p.get()))
        {
            const auto active=eq->parameters.getRawParameterValue("relationship_active");
            if(active&&active->load()>0.5f){error=juce::String::fromUTF8("Relationship EQ actif : préécoute isolée indisponible.");release();return false;}
            for(const auto* id:{"relationship_role","relationship_group"})
                if(auto* param=eq->parameters.getParameter(id))param->setValueNotifyingHost(param->convertTo0to1(0.f));
        }
        p->setPlayHead(&head);p->setRateAndBufferSizeDetails(rate,block);p->prepareToPlay(rate,block);
        latency+=juce::jmax(0,p->getLatencySamples());processors.push_back(std::move(p));
        if(latency>rate*2){error="Latence excessive.";release();return false;}
    }
    const int length=source.getNumSamples()+(int)(rate*2),total=length+latency;
    AuditionResult result;result.sampleRate=rate;result.latencySamples=latency;result.dry.setSize(2,length);result.dry.clear();result.wet.setSize(2,length);result.wet.clear();
    for(int ch=0;ch<2;++ch)result.dry.copyFrom(ch,0,source,ch,0,source.getNumSamples());
    double elapsed=0.;juce::AudioBuffer<float> work(2,block);juce::MidiBuffer midi;
    for(int pos=0;pos<total;pos+=block)
    {
        if(cancel.load()){release();return false;}
        int n=juce::jmin(block,total-pos);work.setSize(2,n,false,false,true);work.clear();
        const int available=juce::jlimit(0,n,source.getNumSamples()-pos);
        for(int ch=0;ch<2;++ch)if(available>0)work.copyFrom(ch,0,source,ch,pos,available);
        const auto start=juce::Time::getMillisecondCounterHiRes();
        for(auto& p:processors){midi.clear();p->processBlock(work,midi);}
        elapsed+=juce::Time::getMillisecondCounterHiRes()-start;
        for(int i=0;i<n;++i)if(pos+i>=latency&&pos+i-latency<length)
            for(int ch=0;ch<2;++ch){const float v=work.getSample(ch,i);if(!std::isfinite(v)){error="Sortie DSP non finie.";release();return false;}result.wet.setSample(ch,pos+i-latency,v);}
    }
    release();
    double dry=0.,wet=0.;float peak=0.f;
    for(int ch=0;ch<2;++ch)for(int i=0;i<length;++i){const auto a=result.dry.getSample(ch,i),v=result.wet.getSample(ch,i);dry+=double(a)*a;wet+=double(v)*v;peak=juce::jmax(peak,std::abs(v));}
    result.matchGain=wet>1.e-12?juce::jlimit(.25,4.,std::sqrt(dry/wet)):1.;
    // Match RMS with bounded gain; attenuate both equally if headroom requires it.
    const float dryPeak=juce::jmax(result.dry.getMagnitude(0,length),.000001f);
    const float safety=juce::jmin(1.f,.89f/juce::jmax(dryPeak,peak*(float)result.matchGain));
    result.dry.applyGain(safety);result.wet.applyGain(safety*(float)result.matchGain);
    const int fade=juce::jmin(128,length/2);
    for(int ch=0;ch<2;++ch){result.dry.applyGainRamp(ch,0,fade,0.f,1.f);result.wet.applyGainRamp(ch,0,fade,0.f,1.f);result.dry.applyGainRamp(ch,length-fade,fade,1.f,0.f);result.wet.applyGainRamp(ch,length-fade,fade,1.f,0.f);}
    if(cancel.load())return false;
    result.cpuPercent=elapsed/(1000.*total/rate)*100.;out=std::move(result);return true;
#else
    juce::ignoreUnused(chain,source,rate,tempo,out,cancel);error=juce::String::fromUTF8("Préécoute native indisponible dans cette édition.");return false;
#endif
}
bool LibertyPluginHost::loadIntoSlot(Slot& slot,
                                     const juce::PluginDescription& description,
                                     juce::String& error,
                                     bool requireInstrument, const juce::MemoryBlock* presetState)
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

    std::unique_ptr<juce::AudioProcessor> instance;
    auto resolvedDescription = description;
    if (description.fileOrIdentifier.startsWith("liberty.native.")
        || description.pluginFormatName == "Liberty Native")
    {
#if defined(LIBERTY_NATIVE_PREVIEW)
        for (const auto& native : nativeDescriptions())
        {
            if (native.fileOrIdentifier != description.fileOrIdentifier
                || native.isInstrument != requireInstrument) continue;
            instance = nativeDevices.create(LibertyEditions::Edition::Standard,
                native.fileOrIdentifier.toStdString());
            if (instance)
            {
                instance->setPlayHead(&nativePlayHead);
                resolvedDescription = native;
            }
            break;
        }
#endif
        if (!instance)
        {
            error = "Appareil Liberty natif indisponible dans cette édition.";
            return false;
        }
    }
    else
    {
        instance = formatManager.createPluginInstance(description, currentSampleRate,
                                                      currentBlockSize, error);
        if (!instance) return false;
    }

    if(presetState!=nullptr)
    {
        if(!validateNativePresetState(resolvedDescription.fileOrIdentifier,*presetState)){error="Preset natif incompatible.";return false;}
        instance->setStateInformation(presetState->getData(),(int)presetState->getSize());
    }
    closeEditor(slot);
    if (slot.processor) slot.processor->releaseResources();
    slot.processor = std::move(instance);
    slot.description = resolvedDescription;
    prepareSlot(slot);
    return true;
}

void LibertyPluginHost::ensureAudioTrackSlot(int trackIndex)
{
    while ((int) trackEffects.size() <= trackIndex) trackEffects.emplace_back();
    for (auto& slot : trackEffects[(size_t)trackIndex]) if (!slot) slot = std::make_unique<Slot>();
}
void LibertyPluginHost::ensureInstrumentSlot(int instrumentTrack)
{
    while ((int) instruments.size() <= instrumentTrack) instruments.push_back(std::make_unique<Slot>());
}

bool LibertyPluginHost::loadEffectForTrack(int trackIndex, const juce::PluginDescription& description, juce::String& error)
{ return loadEffectForTrackSlot(trackIndex, 0, description, error); }
bool LibertyPluginHost::loadEffectForTrackSlot(int trackIndex, int slotIndex, const juce::PluginDescription& description, juce::String& error, const juce::MemoryBlock* presetState)
{
    if (!validTrack(trackIndex) || slotIndex < 0 || slotIndex >= effectSlotsPerTrack) { error = "Slot FX invalide."; return false; }
    const juce::ScopedLock scoped(lock); ensureAudioTrackSlot(trackIndex);
    return loadIntoSlot(*trackEffects[(size_t)trackIndex][(size_t)slotIndex], description, error, false, presetState);
}

bool LibertyPluginHost::loadInstrument(const juce::PluginDescription& description, juce::String& error)
{ return loadInstrumentForTrack(0, description, error); }
bool LibertyPluginHost::loadInstrumentForTrack(int instrumentTrack, const juce::PluginDescription& description, juce::String& error, const juce::MemoryBlock* presetState)
{
    if (instrumentTrack < 0) { error = "Piste Instrument invalide."; return false; }
    const juce::ScopedLock scoped(lock); ensureInstrumentSlot(instrumentTrack);
    const bool ok=loadIntoSlot(*instruments[(size_t)instrumentTrack], description, error, true, presetState);
    publishInstrumentPresence(instrumentTrack);
    return ok;
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
    if (trackIndex >= (int)trackEffects.size() || !trackEffects[(size_t)trackIndex][0]) return;
    auto& slot = *trackEffects[(size_t)trackIndex][0];
    closeEditor(slot); if (slot.processor) slot.processor->releaseResources(); slot.processor.reset(); slot.description = {};
}

void LibertyPluginHost::unloadEffectForTrackSlot(int t,int si){const juce::ScopedLock scoped(lock);if(t<0||si<0||si>=effectSlotsPerTrack||t>=(int)trackEffects.size()||!trackEffects[(size_t)t][(size_t)si])return;auto&x=*trackEffects[(size_t)t][(size_t)si];closeEditor(x);if(x.processor)x.processor->releaseResources();x.processor.reset();x.description={};}
bool LibertyPluginHost::hasEffectForTrackSlot(int t,int si)const{const juce::ScopedLock scoped(lock);return t>=0&&si>=0&&si<effectSlotsPerTrack&&t<(int)trackEffects.size()&&trackEffects[(size_t)t][(size_t)si]&&trackEffects[(size_t)t][(size_t)si]->processor;}
juce::String LibertyPluginHost::getEffectNameForTrackSlot(int t,int si)const{const juce::ScopedLock scoped(lock);return t>=0&&si>=0&&si<effectSlotsPerTrack&&t<(int)trackEffects.size()&&trackEffects[(size_t)t][(size_t)si]&&trackEffects[(size_t)t][(size_t)si]->processor?trackEffects[(size_t)t][(size_t)si]->description.name:juce::String{};}
void LibertyPluginHost::unloadInstrument(){ unloadInstrumentForTrack(0); }
void LibertyPluginHost::unloadInstrumentForTrack(int instrumentTrack)
{
    const juce::ScopedLock scoped(lock);
    if (instrumentTrack < 0 || instrumentTrack >= (int)instruments.size() || !instruments[(size_t)instrumentTrack]) return;
    auto& slot=*instruments[(size_t)instrumentTrack]; closeEditor(slot);
    if(slot.processor)slot.processor->releaseResources(); slot.processor.reset(); slot.description={};
    publishInstrumentPresence(instrumentTrack);
}

bool LibertyPluginHost::hasEffectForTrack(int trackIndex) const
{
    if (!validTrack(trackIndex)) return false;
    const juce::ScopedLock scoped(lock);
    return trackIndex < (int)trackEffects.size() && trackEffects[(size_t)trackIndex][0] && trackEffects[(size_t)trackIndex][0]->processor != nullptr;
}

bool LibertyPluginHost::hasInstrument() const { return hasInstrumentForTrack(0); }
void LibertyPluginHost::publishInstrumentPresence(int t) noexcept
{
    if(t>=0&&t<realtimePresenceCapacity)
        realtimeInstrumentPresent[(size_t)t].store(t<(int)instruments.size()&&instruments[(size_t)t]&&instruments[(size_t)t]->processor!=nullptr,std::memory_order_release);
}

bool LibertyPluginHost::hasInstrumentForTrackRealtime(int t) const noexcept
{
    if(t<0)return false;
    if(t<realtimePresenceCapacity)return realtimeInstrumentPresent[(size_t)t].load(std::memory_order_acquire);
    // Unusual projects beyond the cache: don't invent an absent instrument
    // on contention. Rendering already uses a nonblocking try-lock.
    if(!lock.tryEnter()){realtimeLockMisses.fetch_add(1,std::memory_order_relaxed);return true;}
    const bool present=t<(int)instruments.size()&&instruments[(size_t)t]&&instruments[(size_t)t]->processor!=nullptr;
    lock.exit();return present;
}

int LibertyPluginHost::getDeclaredLaneLatency(int t,bool instrument) const
{
    if(t<0)return 0;
    const juce::ScopedLock scoped(lock);
    std::int64_t sum=0;
    const auto add=[&](const std::unique_ptr<Slot>& slot){
        if(slot&&slot->processor)sum+=juce::jmax(0,slot->processor->getLatencySamples());
    };
    if(instrument){
        if(t<(int)instruments.size())add(instruments[(size_t)t]);
        if(t<(int)instrumentEffects.size())for(const auto& slot:instrumentEffects[(size_t)t])add(slot);
    }else if(t<(int)trackEffects.size())for(const auto& slot:trackEffects[(size_t)t])add(slot);
    return static_cast<int>((std::min)(sum,static_cast<std::int64_t>((std::numeric_limits<int>::max)())));
}

bool LibertyPluginHost::hasInstrumentForTrack(int t) const
{
    const juce::ScopedLock scoped(lock);
    return t>=0 && t<(int)instruments.size() && instruments[(size_t)t] && instruments[(size_t)t]->processor!=nullptr;
}

juce::String LibertyPluginHost::getEffectName(int trackIndex) const
{
    if (!validTrack(trackIndex)) return {};
    const juce::ScopedLock scoped(lock);
    return trackIndex<(int)trackEffects.size() && trackEffects[(size_t)trackIndex][0] && trackEffects[(size_t)trackIndex][0]->processor ? trackEffects[(size_t)trackIndex][0]->description.name : juce::String{};
}

bool LibertyPluginHost::getEffectDescriptionForTrack(int t, juce::PluginDescription& out) const
{
    const juce::ScopedLock sl(lock);
    if(t<0||t>=(int)trackEffects.size()||!trackEffects[(size_t)t][0]||!trackEffects[(size_t)t][0]->processor)return false;
    out=trackEffects[(size_t)t][0]->description; return true;
}
bool LibertyPluginHost::getEffectDescriptionForTrackSlot(int t,int si,juce::PluginDescription& out) const
{
    const juce::ScopedLock sl(lock);
    if(t<0||si<0||si>=effectSlotsPerTrack||t>=(int)trackEffects.size()||!trackEffects[(size_t)t][(size_t)si]||!trackEffects[(size_t)t][(size_t)si]->processor)return false;
    out=trackEffects[(size_t)t][(size_t)si]->description; return true;
}
bool LibertyPluginHost::getEffectDescriptionForInstrumentTrackSlot(int t,int si,juce::PluginDescription& out) const
{
    const juce::ScopedLock sl(lock);
    if(t<0||si<0||si>=effectSlotsPerTrack||t>=(int)instrumentEffects.size()||!instrumentEffects[(size_t)t][(size_t)si]||!instrumentEffects[(size_t)t][(size_t)si]->processor)return false;
    out=instrumentEffects[(size_t)t][(size_t)si]->description; return true;
}
bool LibertyPluginHost::getInstrumentDescriptionForTrack(int t, juce::PluginDescription& out) const
{
    const juce::ScopedLock sl(lock);
    if(t<0||t>=(int)instruments.size()||!instruments[(size_t)t]||!instruments[(size_t)t]->processor)return false;
    out=instruments[(size_t)t]->description; return true;
}
bool LibertyPluginHost::getEffectStateForTrackSlot(int t,int si,juce::MemoryBlock& out) const
{
    const juce::ScopedLock sl(lock); if(t<0||si<0||si>=effectSlotsPerTrack||t>=(int)trackEffects.size()||!trackEffects[(size_t)t][(size_t)si]||!trackEffects[(size_t)t][(size_t)si]->processor)return false;
    out.reset(); trackEffects[(size_t)t][(size_t)si]->processor->getStateInformation(out); return true;
}
bool LibertyPluginHost::setEffectStateForTrackSlot(int t,int si,const juce::MemoryBlock& state)
{
    const juce::ScopedLock sl(lock); if(t<0||si<0||si>=effectSlotsPerTrack||t>=(int)trackEffects.size()||!trackEffects[(size_t)t][(size_t)si]||!trackEffects[(size_t)t][(size_t)si]->processor)return false;
    trackEffects[(size_t)t][(size_t)si]->processor->setStateInformation(state.getData(),(int)state.getSize()); return true;
}
bool LibertyPluginHost::getEffectStateForInstrumentTrackSlot(int t,int si,juce::MemoryBlock& out) const
{
    const juce::ScopedLock sl(lock); if(t<0||si<0||si>=effectSlotsPerTrack||t>=(int)instrumentEffects.size()||!instrumentEffects[(size_t)t][(size_t)si]||!instrumentEffects[(size_t)t][(size_t)si]->processor)return false;
    out.reset(); instrumentEffects[(size_t)t][(size_t)si]->processor->getStateInformation(out); return true;
}
bool LibertyPluginHost::setEffectStateForInstrumentTrackSlot(int t,int si,const juce::MemoryBlock& state)
{
    const juce::ScopedLock sl(lock); if(t<0||si<0||si>=effectSlotsPerTrack||t>=(int)instrumentEffects.size()||!instrumentEffects[(size_t)t][(size_t)si]||!instrumentEffects[(size_t)t][(size_t)si]->processor)return false;
    instrumentEffects[(size_t)t][(size_t)si]->processor->setStateInformation(state.getData(),(int)state.getSize()); return true;
}
bool LibertyPluginHost::getInstrumentStateForTrack(int t,juce::MemoryBlock& out) const
{
    const juce::ScopedLock sl(lock); if(t<0||t>=(int)instruments.size()||!instruments[(size_t)t]||!instruments[(size_t)t]->processor)return false;
    out.reset(); instruments[(size_t)t]->processor->getStateInformation(out); return true;
}
bool LibertyPluginHost::setInstrumentStateForTrack(int t,const juce::MemoryBlock& state)
{
    const juce::ScopedLock sl(lock); if(t<0||t>=(int)instruments.size()||!instruments[(size_t)t]||!instruments[(size_t)t]->processor)return false;
    instruments[(size_t)t]->processor->setStateInformation(state.getData(),(int)state.getSize()); return true;
}

bool LibertyPluginHost::findKnownPluginByIdentifier(const juce::String& id, juce::PluginDescription& out) const
{
    if(id.isEmpty())return false;
#if defined(LIBERTY_NATIVE_PREVIEW)
    for(const auto& d:nativeDescriptions())if(d.fileOrIdentifier==id){out=d;return true;}
#endif
    if(id.startsWith("liberty.native."))return false;
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
    if (trackIndex >= (int)trackEffects.size() || !trackEffects[(size_t)trackIndex][0]) { lock.exit(); return; }
    auto& slot = *trackEffects[(size_t)trackIndex][0];
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
    if (trackIndex >= (int)trackEffects.size() || !trackEffects[(size_t)trackIndex][0]) { lock.exit(); return; }
    auto& slot = *trackEffects[(size_t)trackIndex][0];
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
    if(numSamples<=0||numOutputChannels<=0||t<0)return false;
    if(offlineInstrumentTrack.load(std::memory_order_acquire)==t)return true;
    if(!lock.tryEnter()){realtimeLockMisses.fetch_add(1,std::memory_order_relaxed);return false;}
    if(t>=(int)instruments.size()||!instruments[(size_t)t]||!instruments[(size_t)t]->processor){lock.exit();return false;}
    auto& instrument=*instruments[(size_t)t];const int channels=juce::jlimit(1,2,numOutputChannels);
    instrument.work.setSize(juce::jmax(2,channels),numSamples,false,false,true);instrument.work.clear();
    instrument.processor->processBlock(instrument.work,midi);
    gain=juce::jlimit(0.f,2.f,gain);pan=juce::jlimit(-1.f,1.f,pan);
    const float leftGain=gain*(pan>0.f?1.f-pan:1.f),rightGain=gain*(pan<0.f?1.f+pan:1.f);
    for(int c=0;c<channels;++c)if(outputChannelData[c])juce::FloatVectorOperations::addWithMultiply(outputChannelData[c],instrument.work.getReadPointer(c),c==0?leftGain:rightGain,numSamples);
    lock.exit();return true;
}

bool LibertyPluginHost::beginOfflineInstrumentRender(int t)
{
    if(t<0)return false;
    int expected=-1;
    if(!offlineInstrumentTrack.compare_exchange_strong(expected,t,std::memory_order_acq_rel))return false;
    const juce::ScopedLock scoped(lock);
    if(t>=(int)instruments.size()||!instruments[(size_t)t]||!instruments[(size_t)t]->processor)
    {
        offlineInstrumentTrack.store(-1,std::memory_order_release);
        return false;
    }
    return true;
}
bool LibertyPluginHost::processOfflineInstrument(int t,float* const* outputChannelData,int numOutputChannels,int numSamples,juce::MidiBuffer& midi,float gain,float pan)
{
    if(t<0||offlineInstrumentTrack.load(std::memory_order_acquire)!=t||numSamples<=0||numOutputChannels<=0)return false;
    const juce::ScopedLock scoped(lock);
    if(t>=(int)instruments.size()||!instruments[(size_t)t]||!instruments[(size_t)t]->processor)return false;
    auto& instrument=*instruments[(size_t)t];const int channels=juce::jlimit(1,2,numOutputChannels);
    instrument.work.setSize(juce::jmax(2,channels),numSamples,false,false,true);instrument.work.clear();
    instrument.processor->processBlock(instrument.work,midi);
    gain=juce::jlimit(0.f,2.f,gain);pan=juce::jlimit(-1.f,1.f,pan);
    const float leftGain=gain*(pan>0.f?1.f-pan:1.f),rightGain=gain*(pan<0.f?1.f+pan:1.f);
    for(int c=0;c<channels;++c)if(outputChannelData[c])juce::FloatVectorOperations::addWithMultiply(outputChannelData[c],instrument.work.getReadPointer(c),c==0?leftGain:rightGain,numSamples);
    return true;
}
void LibertyPluginHost::endOfflineInstrumentRender(int t)
{
    if(offlineInstrumentTrack.load(std::memory_order_acquire)==t)
        offlineInstrumentTrack.store(-1,std::memory_order_release);
}

bool LibertyPluginHost::renderInstrumentForTrack(int t, juce::AudioBuffer<float>& destination, int n, juce::MidiBuffer& midi, float gain, float pan)
{
    if (n <= 0 || t < 0) return false;
    if (offlineInstrumentTrack.load(std::memory_order_acquire) == t) return false;
    if (!lock.tryEnter()) {realtimeLockMisses.fetch_add(1,std::memory_order_relaxed);return false;}
    if (t >= (int) instruments.size() || !instruments[(size_t)t] || !instruments[(size_t)t]->processor) { lock.exit(); return false; }
    auto& instrument = *instruments[(size_t)t];
    destination.setSize(2, n, false, false, true);
    destination.clear();
    instrument.processor->processBlock(destination, midi);
    gain = juce::jlimit(0.0f, 2.0f, gain); pan = juce::jlimit(-1.0f, 1.0f, pan);
    destination.applyGain(0, 0, n, gain * (pan > 0.0f ? 1.0f - pan : 1.0f));
    destination.applyGain(1, 0, n, gain * (pan < 0.0f ? 1.0f + pan : 1.0f));
    lock.exit();
    return true;
}

bool LibertyPluginHost::loadEffectForInstrumentTrack(int t, const juce::PluginDescription& d, juce::String& error)
{ return loadEffectForInstrumentTrackSlot(t, 0, d, error); }
bool LibertyPluginHost::loadEffectForInstrumentTrackSlot(int t,int si,const juce::PluginDescription& d,juce::String& error, const juce::MemoryBlock* presetState)
{
    if(t<0||si<0||si>=effectSlotsPerTrack||d.isInstrument){error="Slot FX instrument invalide.";return false;}
    const juce::ScopedLock scoped(lock);while((int)instrumentEffects.size()<=t)instrumentEffects.emplace_back();
    auto& slot=instrumentEffects[(size_t)t][(size_t)si];if(!slot)slot=std::make_unique<Slot>();return loadIntoSlot(*slot,d,error,false,presetState);
}
void LibertyPluginHost::unloadEffectForInstrumentTrack(int t){unloadEffectForInstrumentTrackSlot(t,0);}
void LibertyPluginHost::unloadEffectForInstrumentTrackSlot(int t,int si){const juce::ScopedLock scoped(lock);if(t<0||si<0||si>=effectSlotsPerTrack||t>=(int)instrumentEffects.size()||!instrumentEffects[(size_t)t][(size_t)si])return;auto&x=*instrumentEffects[(size_t)t][(size_t)si];closeEditor(x);if(x.processor)x.processor->releaseResources();x.processor.reset();x.description={};}
bool LibertyPluginHost::hasEffectForInstrumentTrack(int t)const{return hasEffectForInstrumentTrackSlot(t,0);}
bool LibertyPluginHost::hasEffectForInstrumentTrackSlot(int t,int si)const{const juce::ScopedLock scoped(lock);return t>=0&&si>=0&&si<effectSlotsPerTrack&&t<(int)instrumentEffects.size()&&instrumentEffects[(size_t)t][(size_t)si]&&instrumentEffects[(size_t)t][(size_t)si]->processor;}
juce::String LibertyPluginHost::getEffectNameForInstrumentTrack(int t)const{return getEffectNameForInstrumentTrackSlot(t,0);}
juce::String LibertyPluginHost::getEffectNameForInstrumentTrackSlot(int t,int si)const{const juce::ScopedLock scoped(lock);return t>=0&&si>=0&&si<effectSlotsPerTrack&&t<(int)instrumentEffects.size()&&instrumentEffects[(size_t)t][(size_t)si]&&instrumentEffects[(size_t)t][(size_t)si]->processor?instrumentEffects[(size_t)t][(size_t)si]->description.name:juce::String{};}
void LibertyPluginHost::showEffectEditorForInstrumentTrack(int t){const juce::ScopedLock scoped(lock);if(t<0||t>=(int)instrumentEffects.size()||!instrumentEffects[(size_t)t][0])return;auto&x=*instrumentEffects[(size_t)t][0];showEditor(x,"Liberty - "+x.description.name);}
void LibertyPluginHost::showEffectEditorForInstrumentTrackSlot(int t,int si){const juce::ScopedLock scoped(lock);if(t<0||si<0||si>=effectSlotsPerTrack||t>=(int)instrumentEffects.size()||!instrumentEffects[(size_t)t][(size_t)si])return;auto&x=*instrumentEffects[(size_t)t][(size_t)si];if(x.processor)showEditor(x,"Liberty - "+x.description.name);}
void LibertyPluginHost::processInstrumentEffect(int t,juce::AudioBuffer<float>& buffer,int n){if(t<0||n<=0||!lock.tryEnter())return;if(t>=(int)instrumentEffects.size()){lock.exit();return;}juce::MidiBuffer empty;for(auto&slot:instrumentEffects[(size_t)t])if(slot&&slot->processor)slot->processor->processBlock(buffer,empty);lock.exit();}

void LibertyPluginHost::processAudioEffectSlot(int t,int si,juce::AudioBuffer<float>& buffer)
{
    if(t<0||si<0||si>=effectSlotsPerTrack)return;
    if(!lock.tryEnter()){realtimeLockMisses.fetch_add(1,std::memory_order_relaxed);return;}
    if(t<(int)trackEffects.size()&&trackEffects[(size_t)t][(size_t)si]&&trackEffects[(size_t)t][(size_t)si]->processor)
    {
        juce::MidiBuffer empty;
        trackEffects[(size_t)t][(size_t)si]->processor->processBlock(buffer,empty);
    }
    lock.exit();
}
void LibertyPluginHost::processInstrumentEffectSlot(int t,int si,juce::AudioBuffer<float>& buffer)
{
    if(t<0||si<0||si>=effectSlotsPerTrack)return;
    if(!lock.tryEnter()){realtimeLockMisses.fetch_add(1,std::memory_order_relaxed);return;}
    if(t<(int)instrumentEffects.size()&&instrumentEffects[(size_t)t][(size_t)si]&&instrumentEffects[(size_t)t][(size_t)si]->processor)
    {
        juce::MidiBuffer empty;
        instrumentEffects[(size_t)t][(size_t)si]->processor->processBlock(buffer,empty);
    }
    lock.exit();
}

void LibertyPluginHost::resetInstrumentEffectState(int t)
{
    const juce::ScopedLock scoped(lock);
    if(t<0||t>=(int)instrumentEffects.size())return;
    for(auto& slot:instrumentEffects[(size_t)t])
        if(slot&&slot->processor)slot->processor->reset();
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
    if (trackIndex >= (int)trackEffects.size() || !trackEffects[(size_t)trackIndex][0]) return;
    auto& slot = *trackEffects[(size_t)trackIndex][0];
    showEditor(slot, "Liberty - " + slot.description.name);
}
void LibertyPluginHost::showEditorForTrackSlot(int t,int si){if(!validTrack(t)||si<0||si>=effectSlotsPerTrack)return;const juce::ScopedLock scoped(lock);if(t>=(int)trackEffects.size()||!trackEffects[(size_t)t][(size_t)si])return;auto&x=*trackEffects[(size_t)t][(size_t)si];if(x.processor)showEditor(x,"Liberty - "+x.description.name);}

bool LibertyPluginHost::isInstrumentEditorVisibleForTrack(int t) const
{
    const juce::ScopedLock scoped(lock);
    return t >= 0 && t < (int) instruments.size() && instruments[(size_t)t]
        && instruments[(size_t)t]->editorWindow
        && instruments[(size_t)t]->editorWindow->isVisible();
}
void LibertyPluginHost::toggleInstrumentEditorForTrack(int t)
{
    const juce::ScopedLock scoped(lock);
    if (t < 0 || t >= (int) instruments.size() || !instruments[(size_t)t]) return;
    auto& slot = *instruments[(size_t)t];
    if (!slot.processor) return;
    if (slot.editorWindow && slot.editorWindow->isVisible())
        slot.editorWindow->setVisible(false);
    else
        showEditor(slot, "Liberty - " + slot.description.name);
}
void LibertyPluginHost::toggleEditorForTrackSlot(int t, int si)
{
    if (!validTrack(t) || si < 0 || si >= effectSlotsPerTrack) return;
    const juce::ScopedLock scoped(lock);
    if (t >= (int) trackEffects.size() || !trackEffects[(size_t)t][(size_t)si]) return;
    auto& slot = *trackEffects[(size_t)t][(size_t)si];
    if (!slot.processor) return;
    if (slot.editorWindow && slot.editorWindow->isVisible())
        slot.editorWindow->setVisible(false);
    else
        showEditor(slot, "Liberty - " + slot.description.name);
}
void LibertyPluginHost::showInstrumentEditor(){showInstrumentEditorForTrack(0);}
void LibertyPluginHost::showInstrumentEditorForTrack(int t)
{
    const juce::ScopedLock scoped(lock);
    if(t<0||t>=(int)instruments.size()||!instruments[(size_t)t])return;
    auto& slot=*instruments[(size_t)t];showEditor(slot,"Liberty - "+slot.description.name);
}
