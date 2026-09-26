#define private public
#include "MainComponent.h"
#undef private
#include "PluginHost.h"
#include "OneKnobEffects.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

bool isLibertyMixConsoleVisible(MainComponent* owner);
int getLibertyMixConsolePluginDropTrack(MainComponent* owner,
                                        juce::Point<int> ownerPoint,
                                        bool instrumentPlugin);

namespace
{
std::optional<juce::PluginDescription> resolveSelectedPlugin(juce::Component* eventComponent)
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

    const auto identifier = item->getUniqueName();
    if (identifier.isEmpty())
        return std::nullopt;

    const auto descriptions = LibertyPluginHost::instance().getPluginDescriptions();
    for (const auto& description : descriptions)
        if (description.createIdentifierString() == identifier)
            return description;

    return std::nullopt;
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

MainComponent* findMainComponentAtScreenPoint(juce::Point<int> screenPoint)
{
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
    {
        auto* component = desktop.getComponent(i);
        auto* window = dynamic_cast<juce::DocumentWindow*>(component);
        if (window == nullptr || !window->getScreenBounds().contains(screenPoint))
            continue;
        if (auto* main = dynamic_cast<MainComponent*>(window->getContentComponent()))
            return main;
    }
    return nullptr;
}

int arrangerDropTrack(MainComponent& main, juce::Point<int> local, bool instrumentPlugin)
{
    const int rowH = getLibertyTrackRowHeight();
    const int relativeY = local.y - main.getArrangeTop();
    if (relativeY < 0 || local.y >= main.getMixerTop()) return -1;
    const int logicalRow = main.getTrackScrollRows() + relativeY / juce::jmax(1, rowH);
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
    const int rowH = getLibertyTrackRowHeight();
    const int relativeY = local.y - main.getArrangeTop();
    if (relativeY < 0 || local.y >= main.getMixerTop()) return -1;
    const int logicalRow = main.getTrackScrollRows() + relativeY / juce::jmax(1, rowH);
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
        for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i)
            if (auto* window = dynamic_cast<juce::DocumentWindow*>(juce::Desktop::getInstance().getComponent(i)))
                if (auto* owner = dynamic_cast<MainComponent*>(window->getContentComponent()))
                    owner->setMouseCursor(juce::MouseCursor::NormalCursor);
        registered = false;
        candidate.reset();
        oneKnobCandidate.reset();
        dragging = false;
    }

private:
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (!event.mods.isLeftButtonDown()) return;
        for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i)
            if (auto* window = dynamic_cast<juce::DocumentWindow*>(juce::Desktop::getInstance().getComponent(i)))
                if (auto* owner = dynamic_cast<MainComponent*>(window->getContentComponent()))
                    owner->setMouseCursor(juce::MouseCursor::NormalCursor);
        oneKnobCandidate.reset();
        oneKnobCandidate = resolveOneKnob(event.eventComponent);
        candidate = oneKnobCandidate.has_value() ? std::nullopt : resolveSelectedPlugin(event.eventComponent);
        dragStartScreen = event.getScreenPosition();
        dragging = false;
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!event.mods.isLeftButtonDown()) return;

        if (!oneKnobCandidate.has_value())
            oneKnobCandidate = resolveOneKnob(event.eventComponent);
        if (!oneKnobCandidate.has_value() && !candidate.has_value())
            candidate = resolveSelectedPlugin(event.eventComponent);
        if (!oneKnobCandidate.has_value() && !candidate.has_value())
            return;

        const auto delta = event.getScreenPosition() - dragStartScreen;
        if (!dragging && delta.getDistanceFromOrigin() >= 6.0f)
            dragging = true;

        if (!dragging) return;

        if (auto* main = findMainComponentAtScreenPoint(event.getScreenPosition()))
        {
            const auto local = main->getLocalPoint(nullptr, event.getScreenPosition());
            const int target = oneKnobCandidate.has_value()
                ? oneKnobDropTrack(*main, local)
                : pluginDropTrack(*main, local, candidate->isInstrument);
            main->setMouseCursor(target >= 0 ? juce::MouseCursor::DraggingHandCursor
                                             : juce::MouseCursor::NoCursor);
        }
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        auto plugin = candidate;
        auto oneKnob = oneKnobCandidate;
        candidate.reset();
        oneKnobCandidate.reset();

        if (!dragging || (!plugin.has_value() && !oneKnob.has_value()))
        {
            dragging = false;
            for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i)
                if (auto* window = dynamic_cast<juce::DocumentWindow*>(juce::Desktop::getInstance().getComponent(i)))
                    if (auto* owner = dynamic_cast<MainComponent*>(window->getContentComponent()))
                        owner->setMouseCursor(juce::MouseCursor::NormalCursor);
            return;
        }
        dragging = false;

        auto* main = findMainComponentAtScreenPoint(event.getScreenPosition());
        if (main == nullptr)
        {
            for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i)
                if (auto* window = dynamic_cast<juce::DocumentWindow*>(juce::Desktop::getInstance().getComponent(i)))
                    if (auto* owner = dynamic_cast<MainComponent*>(window->getContentComponent()))
                        owner->setMouseCursor(juce::MouseCursor::NormalCursor);
            return;
        }
        main->setMouseCursor(juce::MouseCursor::NormalCursor);

        const auto local = main->getLocalPoint(nullptr, event.getScreenPosition());
        const int target = oneKnob.has_value()
            ? oneKnobDropTrack(*main, local)
            : pluginDropTrack(*main, local, plugin->isInstrument);

        if (target < 0)
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                oneKnob.has_value() ? "Liberty - One Knob" : (plugin->isInstrument ? "Liberty - Instrument" : "Liberty - Effet"),
                oneKnob.has_value()
                    ? "Dépose le One Knob sur une piste Audio."
                    : (plugin->isInstrument
                        ? "Dépose l'instrument sur une piste Instrument."
                        : "Dépose l'effet sur une piste Audio."),
                "OK");
            return;
        }

        if (oneKnob.has_value())
        {
            const int slot = target;
            auto& manager = LibertyOneKnobManager::instance();
            manager.setEffect(slot, *oneKnob);
            manager.showEditor(slot);
            main->selectedTrack = target;
            main->repaint();
            return;
        }

        auto& host = LibertyPluginHost::instance();
        juce::String error;
        bool loaded = false;

        if (plugin->isInstrument)
        {
            main->selectedTrack = target;
            const int lane = target - main->getAudioTrackCount() - main->getMidiTrackCount();
            loaded = host.loadInstrumentForTrack(lane, *plugin, error);
            if (loaded) host.showInstrumentEditorForTrack(lane);
        }
        else
        {
            main->selectedTrack = target;
            loaded = host.loadEffectForTrack(target, *plugin, error);
            if (loaded) host.showEditorForTrack(target);
        }

        if (!loaded)
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Liberty - Plugin",
                                                   error.isNotEmpty() ? error : "Le plugin n'a pas pu être chargé.",
                                                   "OK");

        main->repaint();
    }

    std::optional<juce::PluginDescription> candidate;
    std::optional<LibertyOneKnobRack::Type> oneKnobCandidate;
    juce::Point<int> dragStartScreen;
    bool dragging = false;
    bool registered = true;
};

PluginDragDropController controller;
}

void shutdownLibertyPluginDragDropController()
{
    controller.shutdown();
}
