#include "MainComponent.h"
#include "MidiEngine.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
MainComponent* findMainInTree(juce::Component* component) noexcept
{
    if (component == nullptr) return nullptr;
    if (auto* main = dynamic_cast<MainComponent*>(component)) return main;
    for (int i = 0; i < component->getNumChildComponents(); ++i)
        if (auto* main = findMainInTree(component->getChildComponent(i))) return main;
    return nullptr;
}

MainComponent* findMainComponent() noexcept
{
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* main = findMainInTree(desktop.getComponent(i))) return main;
    return nullptr;
}
}

bool handleLibertyMidiQuantizeKeyPress(const juce::KeyPress& key)
{
    auto* main = findMainComponent();
    if (main == nullptr) return false;

    const auto modifiers = key.getModifiers();
    if (modifiers.isCommandDown() || modifiers.isCtrlDown()) return false;
    if (main->getMidiEngine().getNumSelectedNotes() == 0) return false;

    std::int64_t gridTicks = 0;
    switch (key.getKeyCode())
    {
        case 'Q': case 'q': gridTicks = MidiEngine::ticksPerQuarterNote / 4; break;
        case 'W': case 'w': gridTicks = MidiEngine::ticksPerQuarterNote / 2; break;
        case 'E': case 'e': gridTicks = MidiEngine::ticksPerQuarterNote; break;
        default: return false;
    }

    if (!main->getMidiEngine().quantizeSelectedNotes(gridTicks)) return false;
    main->updateMidiClipTiming();
    main->repaint();
    return true;
}
