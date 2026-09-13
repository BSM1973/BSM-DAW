#define private public
#include "MainComponent.h"
#undef private
#include "PluginHost.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace
{
constexpr int transportHeight = 76;
constexpr int rulerHeight = 32;
constexpr int instrumentTrackIndex = AudioEngine::maxAudioTracks + 1;

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
    if (item == nullptr || item->getParentItem() == nullptr)
        return std::nullopt;

    const auto pluginName = item->getUniqueName();
    const auto manufacturerName = item->getParentItem()->getUniqueName();
    if (pluginName.isEmpty() || manufacturerName.isEmpty())
        return std::nullopt;

    const auto descriptions = LibertyPluginHost::instance().getPluginDescriptions();
    for (const auto& description : descriptions)
    {
        auto manufacturer = description.manufacturerName.trim();
        if (manufacturer.isEmpty()) manufacturer = "Other";
        if (description.name == pluginName && manufacturer == manufacturerName)
            return description;
    }

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
        registered = false;
        candidate.reset();
        dragging = false;
    }

private:
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (!event.mods.isLeftButtonDown()) return;
        candidate = resolveSelectedPlugin(event.eventComponent);
        dragStartScreen = event.getScreenPosition();
        dragging = false;
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!event.mods.isLeftButtonDown()) return;

        if (!candidate.has_value())
            candidate = resolveSelectedPlugin(event.eventComponent);
        if (!candidate.has_value())
            return;

        const auto delta = event.getScreenPosition() - dragStartScreen;
        if (!dragging && delta.getDistanceFromOrigin() >= 6.0f)
            dragging = true;

        if (!dragging) return;

        if (auto* main = findMainComponentAtScreenPoint(event.getScreenPosition()))
        {
            const auto local = main->getLocalPoint(nullptr, event.getScreenPosition());
            const int rowH = getLibertyTrackRowHeight();
            const int row = (local.y - transportHeight - rulerHeight) / juce::jmax(1, rowH);

            const bool validAudioTarget = !candidate->isInstrument
                && row >= 0 && row < AudioEngine::maxAudioTracks;
            const bool validInstrumentTarget = candidate->isInstrument
                && row == instrumentTrackIndex;

            main->setMouseCursor((validAudioTarget || validInstrumentTarget)
                ? juce::MouseCursor::DraggingHandCursor
                : juce::MouseCursor::NoCursor);
        }
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        auto plugin = candidate;
        candidate.reset();

        if (!dragging || !plugin.has_value())
        {
            dragging = false;
            return;
        }
        dragging = false;

        auto* main = findMainComponentAtScreenPoint(event.getScreenPosition());
        if (main == nullptr) return;
        main->setMouseCursor(juce::MouseCursor::NormalCursor);

        const auto local = main->getLocalPoint(nullptr, event.getScreenPosition());
        const int rowH = getLibertyTrackRowHeight();
        const int relativeY = local.y - transportHeight - rulerHeight;
        if (relativeY < 0) return;
        const int row = relativeY / juce::jmax(1, rowH);

        auto& host = LibertyPluginHost::instance();
        juce::String error;
        bool loaded = false;

        if (plugin->isInstrument)
        {
            if (row != instrumentTrackIndex)
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                       "Liberty - Instrument",
                                                       "Dépose l'instrument sur la piste Instrument 1.",
                                                       "OK");
                return;
            }

            main->selectedTrack = instrumentTrackIndex;
            loaded = host.loadInstrument(*plugin, error);
            if (loaded) host.showInstrumentEditor();
        }
        else
        {
            if (row < 0 || row >= AudioEngine::maxAudioTracks)
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                       "Liberty - Effet",
                                                       "Dépose l'effet sur une piste Audio 1 à 4.",
                                                       "OK");
                return;
            }

            main->selectedTrack = row;
            loaded = host.loadEffectForTrack(row, *plugin, error);
            if (loaded) host.showEditorForTrack(row);
        }

        if (!loaded)
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Liberty - Plugin",
                                                   error.isNotEmpty() ? error : "Le plugin n'a pas pu être chargé.",
                                                   "OK");

        main->repaint();
    }

    std::optional<juce::PluginDescription> candidate;
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
