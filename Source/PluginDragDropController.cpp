#include "LibertyDeferredUi.h"
#define private public
#include "MainComponent.h"
#undef private
#include "PluginHost.h"
#include "LibertyPresetCenter.h"
#include "LibertyDeviceChains.h"
#include "OneKnobEffects.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

bool isLibertyMixConsoleVisible(MainComponent* owner);
int getLibertyMixConsolePluginDropTrack(MainComponent* owner,
                                        juce::Point<int> ownerPoint,
                                        bool instrumentPlugin);
void refreshLibertyMixConsole(MainComponent* owner);

namespace
{
std::optional<juce::PluginDescription> resolveSelectedPlugin(juce::Component* eventComponent, std::optional<LibertyPresetCenter::Entry>& preset)
{
    juce::TreeView* tree = nullptr;
    for (auto* c = eventComponent; c != nullptr; c = c->getParentComponent())
    {
        if ((tree = dynamic_cast<juce::TreeView*>(c)) != nullptr)
            break;
    }

    if (tree == nullptr || tree->getNumSelectedItems() <= 0)
        return std::nullopt;

    auto* item = tree->getSelectedItem(0);
    if (item == nullptr)
        return std::nullopt;

    if(auto* native=dynamic_cast<LibertyPresetCenter::Item*>(item)){
        juce::PluginDescription desc;if(!LibertyPluginHost::instance().findKnownPluginByIdentifier(native->entry.deviceId,desc))return std::nullopt;
        preset=native->entry;return desc;
    }
    const auto identifier = item->getUniqueName();
    if (identifier.isEmpty())
        return std::nullopt;

    const auto descriptions = LibertyPluginHost::instance().getPluginDescriptions();
    for (const auto& description : descriptions)
        if (description.createIdentifierString() == identifier)
            return description;

    return std::nullopt;
}

std::optional<LibertyDeviceChains::Entry> resolveChain(juce::Component* component)
{
    for(auto* c=component;c;c=c->getParentComponent())if(auto* tree=dynamic_cast<juce::TreeView*>(c))
        if(auto* item=dynamic_cast<LibertyDeviceChains::Item*>(tree->getSelectedItem(0)))return item->entry;
    return std::nullopt;
}
int chainDropTrack(MainComponent& main,juce::Point<int> local,bool needsInstrument)
{
    const int audio=main.getAudioTrackCount(),first=audio+main.getMidiTrackCount(),end=first+main.getInstrumentTrackCount();
    int row=-1;
    if(isLibertyMixConsoleVisible(&main)){const int target=getLibertyMixConsolePluginDropTrack(&main,local,false);if(target>=1000000)row=(target-1000000)/8;}
    else if(local.y>=main.getArrangeTop()&&local.y<main.getMixerTop())row=main.getArrangeTrackAtY(local.y);
    return (row>=first&&row<end)||(!needsInstrument&&row>=0&&row<audio)?row:-1;
}
std::optional<LibertyOneKnobRack::Type> resolveOneKnob(juce::Component* eventComponent)
{
    auto match = [](juce::Component* c) -> std::optional<LibertyOneKnobRack::Type>
    {
        auto* button = dynamic_cast<juce::TextButton*>(c);
        if (button == nullptr) return std::nullopt;
        const auto text = button->getButtonText().toUpperCase();
        if (text == "1K CHORUS") return LibertyOneKnobRack::Type::chorus;
        if (text == "1K FLANGER") return LibertyOneKnobRack::Type::flanger;
        if (text == "1K PHASER") return LibertyOneKnobRack::Type::phaser;
        if (text == "1K TREMOLO") return LibertyOneKnobRack::Type::tremolo;
        if (text == "1K REVERB") return LibertyOneKnobRack::Type::reverb;
        if (text == "1K DELAY") return LibertyOneKnobRack::Type::delay;
        if (text == "1K DRIVE") return LibertyOneKnobRack::Type::drive;
        if (text == "1K COMP") return LibertyOneKnobRack::Type::compressor;
        if (text == "1K SAT") return LibertyOneKnobRack::Type::saturation;
        if (text == "1K WIDTH") return LibertyOneKnobRack::Type::stereoWidth;
        if (text == "1K FILTER") return LibertyOneKnobRack::Type::filter;
        if (text == "1K DOUBLER") return LibertyOneKnobRack::Type::doubler;
        if (text == "1K EXCITER") return LibertyOneKnobRack::Type::exciter;
        if (text == "1K DE-ESS") return LibertyOneKnobRack::Type::deEsser;
        if (text == "1K GATE") return LibertyOneKnobRack::Type::gate;
        if (text == "1K BASS") return LibertyOneKnobRack::Type::bassBoost;
        if (text == "1K AIR") return LibertyOneKnobRack::Type::air;
        if (text == "1K PUNCH") return LibertyOneKnobRack::Type::punch;
        if (text == "1K CLIP") return LibertyOneKnobRack::Type::softClip;
        return std::nullopt;
    };

    for (auto* c = eventComponent; c != nullptr; c = c->getParentComponent())
        if (auto type = match(c)) return type;

    // Global mouse events may be retargeted to the Browser panel instead of the
    // child TextButton. Resolve the real component underneath the pointer too.
    const auto screen = juce::Desktop::getMousePosition();
    if (auto* underMouse = juce::Desktop::getInstance().findComponentAt(screen))
        for (auto* c = underMouse; c != nullptr; c = c->getParentComponent())
            if (auto type = match(c)) return type;

    return std::nullopt;
}


void resetLibertyDragCursors()
{
    for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i)
        if (auto* window = dynamic_cast<juce::DocumentWindow*>(juce::Desktop::getInstance().getComponent(i)))
            if (auto* owner = dynamic_cast<MainComponent*>(window->getContentComponent()))
                owner->setMouseCursor(juce::MouseCursor::NormalCursor);
}

MainComponent* findMainComponentAtScreenPoint(juce::Point<int> screenPoint)
{
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
    {
        auto* component = desktop.getComponent(i);
        auto* window = dynamic_cast<juce::DocumentWindow*>(component);
        if (window == nullptr || !window->getScreenBounds().contains(screenPoint))
            continue;
        // The main window hosts LibertyMainContent (menu + editor), not
        // MainComponent directly. Find the actual editor recursively.
        const auto findEditor = [](auto&& self, juce::Component* node) -> MainComponent*
        {
            if (node == nullptr) return nullptr;
            if (auto* editor = dynamic_cast<MainComponent*>(node)) return editor;
            for (int child = 0; child < node->getNumChildComponents(); ++child)
                if (auto* editor = self(self, node->getChildComponent(child))) return editor;
            return nullptr;
        };
        if (auto* main = findEditor(findEditor, window->getContentComponent()))
            return main;
    }
    return nullptr;
}

int arrangerDropTrack(MainComponent& main, juce::Point<int> local, bool instrumentPlugin)
{
    if (local.y < main.getArrangeTop() || local.y >= main.getMixerTop()) return -1;
    const int logicalRow = main.getArrangeTrackAtY(local.y);
    if (logicalRow < 0) return -1;
    const int audioCount = main.getAudioTrackCount();
    const int midiCount = main.getMidiTrackCount();
    const int instrumentFirst = audioCount + midiCount;
    if (instrumentPlugin)
        return logicalRow >= instrumentFirst && logicalRow < instrumentFirst + main.getInstrumentTrackCount() ? logicalRow : -1;
    return logicalRow >= 0 && logicalRow < audioCount ? logicalRow : -1;
}
int pluginDropTrack(MainComponent& main, juce::Point<int> local, bool instrumentPlugin)
{
    if (isLibertyMixConsoleVisible(&main))
        return getLibertyMixConsolePluginDropTrack(&main, local, instrumentPlugin);
    return arrangerDropTrack(main, local, instrumentPlugin);
}

int oneKnobDropTrack(MainComponent& main, juce::Point<int> local)
{
    if (isLibertyMixConsoleVisible(&main))
        return getLibertyMixConsolePluginDropTrack(&main, local, false);
    if (local.y < main.getArrangeTop() || local.y >= main.getMixerTop()) return -1;
    const int logicalRow = main.getArrangeTrackAtY(local.y);
    if (logicalRow < 0) return -1;
    return logicalRow >= 0 && logicalRow < main.getAudioTrackCount() ? logicalRow : -1;
}
class PluginDragDropController final : private juce::MouseListener
{
public:
    PluginDragDropController()
    {
        juce::Desktop::getInstance().addGlobalMouseListener(this);
    }

    ~PluginDragDropController() override
    {
        shutdown();
    }

    void shutdown()
    {
        if (!registered) return;
        juce::Desktop::getInstance().removeGlobalMouseListener(this);
        resetLibertyDragCursors();
        registered = false;
        candidate.reset();
        oneKnobCandidate.reset();
        dragging = false;
    }

private:
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (!event.mods.isLeftButtonDown()) return;
        resetLibertyDragCursors();
        oneKnobCandidate.reset();presetCandidate.reset();chainCandidate=resolveChain(event.eventComponent);
        oneKnobCandidate = resolveOneKnob(event.eventComponent);
        candidate = oneKnobCandidate.has_value() ? std::nullopt : resolveSelectedPlugin(event.eventComponent,presetCandidate);
        dragStartScreen = event.getScreenPosition();
        dragging = false;
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!event.mods.isLeftButtonDown()) return;

        if (!oneKnobCandidate.has_value())
            oneKnobCandidate = resolveOneKnob(event.eventComponent);
        if(!chainCandidate&&!oneKnobCandidate&&!candidate)chainCandidate=resolveChain(event.eventComponent);
        if (!chainCandidate && !oneKnobCandidate.has_value() && !candidate.has_value())
            candidate = resolveSelectedPlugin(event.eventComponent,presetCandidate);
        if (!chainCandidate && !oneKnobCandidate.has_value() && !candidate.has_value())
            return;

        const auto delta = event.getScreenPosition() - dragStartScreen;
        if (!dragging && delta.getDistanceFromOrigin() >= 6.0f)
            dragging = true;

        if (!dragging) return;

        if (auto* main = findMainComponentAtScreenPoint(event.getScreenPosition()))
        {
            const auto local = main->getLocalPoint(nullptr, event.getScreenPosition());
            const int target = chainCandidate?chainDropTrack(*main,local,chainCandidate->needsInstrument()):oneKnobCandidate.has_value()
                ? oneKnobDropTrack(*main, local)
                : pluginDropTrack(*main, local, candidate->isInstrument);
            main->setMouseCursor(target >= 0 ? juce::MouseCursor::DraggingHandCursor
                                             : juce::MouseCursor::NoCursor);
        }
        else
        {
            resetLibertyDragCursors();
        }
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        auto chain=std::move(chainCandidate);chainCandidate.reset();
        auto plugin = candidate;
        auto preset = std::move(presetCandidate);presetCandidate.reset();
        auto oneKnob = oneKnobCandidate;
        candidate.reset();
        oneKnobCandidate.reset();

        if (!dragging || (!chain && !plugin.has_value() && !oneKnob.has_value()))
        {
            dragging = false;
            resetLibertyDragCursors();
            return;
        }
        dragging = false;

        auto* main = findMainComponentAtScreenPoint(event.getScreenPosition());
        if (main == nullptr)
        {
            resetLibertyDragCursors();
            return;
        }
        main->setMouseCursor(juce::MouseCursor::NormalCursor);

        const auto local = main->getLocalPoint(nullptr, event.getScreenPosition());
        if(chain)
        {
            const int row=chainDropTrack(*main,local,chain->needsInstrument());if(row<0)return;
            const int first=main->getAudioTrackCount()+main->getMidiTrackCount();const bool instrument=row>=first;juce::String error;
            if(LibertyPluginHost::instance().insertNativeChain(instrument,instrument?row-first:row,chain->devices,error))
            {main->selectedTrack=row;if(chain->needsInstrument())main->notifyInstrumentChanged();refreshLibertyMixConsole(main);main->repaint();}
            else juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,"Device Chains",error);
            return;
        }
        const int target = oneKnob.has_value()
            ? oneKnobDropTrack(*main, local)
            : pluginDropTrack(*main, local, plugin->isInstrument);

        if (target < 0)
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                oneKnob.has_value() ? "Liberty - One Knob" : (plugin->isInstrument ? "Liberty - Instrument" : "Liberty - Effet"),
                oneKnob.has_value()
                    ? "Dépose le One Knob directement sur un slot FX 1 à FX 8."
                    : (plugin->isInstrument
                        ? "Dépose l'instrument sur une piste Instrument."
                        : "Dépose l'effet directement sur un slot FX 1 à FX 8."),
                "OK");
            return;
        }

        if (oneKnob.has_value())
        {
            if (target < 1000000) { resetLibertyDragCursors(); return; }
            const int packed = target - 1000000;
            const int logicalTrack = packed / 8;
            const int insertSlot = packed % 8;
            const int audioCount = main->getAudioTrackCount();
            const int firstInstrument = audioCount + main->getMidiTrackCount();
            const int oneKnobKey = logicalTrack < audioCount ? logicalTrack * 8 + insertSlot : 100000 + (logicalTrack - firstInstrument) * 8 + insertSlot;
            auto& manager = LibertyOneKnobManager::instance();
            auto& host = LibertyPluginHost::instance();
            if (logicalTrack < audioCount)
                host.unloadEffectForTrackSlot(logicalTrack, insertSlot);
            else if (logicalTrack >= firstInstrument)
                host.unloadEffectForInstrumentTrackSlot(logicalTrack - firstInstrument, insertSlot);
            manager.setEffect(oneKnobKey, *oneKnob);
            manager.showEditor(oneKnobKey);
            main->selectedTrack = logicalTrack;
            refreshLibertyMixConsole(main);
            main->repaint();
            return;
        }

        auto& host = LibertyPluginHost::instance();
        juce::String error;
        bool loaded = false;
        juce::MemoryBlock presetState;const juce::MemoryBlock* state=nullptr;
        if(preset){if(!preset->getState(presetState))return;state=&presetState;}

        if (plugin->isInstrument)
        {
            main->selectedTrack = target;
            const int lane = target - main->getAudioTrackCount() - main->getMidiTrackCount();
            loaded = host.loadInstrumentForTrack(lane, *plugin, error, state);
            if (loaded)
            {
                main->notifyInstrumentChanged();
                host.showInstrumentEditorForTrack(lane);
            }
        }
        else
        {
            if (target < 1000000) { error = "Dépose l'effet directement sur un slot FX 1 à FX 8."; }
            else
            {
                const int packed = target - 1000000;
                const int logicalTrack = packed / 8;
                const int insertSlot = packed % 8;
                const int audioCount = main->getAudioTrackCount();
                const int firstInstrument = audioCount + main->getMidiTrackCount();
                main->selectedTrack = logicalTrack;
                if (logicalTrack < audioCount)
                {
                    loaded = host.loadEffectForTrackSlot(logicalTrack, insertSlot, *plugin, error, state);
                    if (loaded)
                    {
                        LibertyOneKnobManager::instance().clearEffect(logicalTrack * 8 + insertSlot);
                        host.showEditorForTrackSlot(logicalTrack, insertSlot);
                    }
                }
                else if (logicalTrack >= firstInstrument)
                {
                    const int lane = logicalTrack - firstInstrument;
                    loaded = host.loadEffectForInstrumentTrackSlot(lane, insertSlot, *plugin, error, state);
                    if (loaded)
                    {
                        LibertyOneKnobManager::instance().clearEffect(100000 + lane * 8 + insertSlot);
                        host.showEffectEditorForInstrumentTrackSlot(lane, insertSlot);
                    }
                }
            }
        }

        if (loaded) refreshLibertyMixConsole(main);
        if (!loaded)
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Liberty - Plugin",
                                                   error.isNotEmpty() ? error : "Le plugin n'a pas pu être chargé.",
                                                   "OK");

        main->repaint();
    }

    std::optional<juce::PluginDescription> candidate;
    std::optional<LibertyPresetCenter::Entry> presetCandidate;
    std::optional<LibertyDeviceChains::Entry> chainCandidate;
    std::optional<LibertyOneKnobRack::Type> oneKnobCandidate;
    juce::Point<int> dragStartScreen;
    bool dragging = false;
    bool registered = true;
};

LibertyDeferredUi::Deferred<PluginDragDropController> controller;
}

void shutdownLibertyPluginDragDropController()
{
    controller->shutdown();
}
