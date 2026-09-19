#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>

namespace
{
juce::String cleanText(juce::String text)
{
    text = text.replace("★ ", "");
    text = text.replace("★", "");
    text = text.replace("▶ ", "");
    text = text.replace("▶", "");
    text = text.replace("■ ", "");
    text = text.replace("■", "");
    text = text.replace("•", " ");
    text = text.replace("→", " ");
    text = text.replace("—", " - ");
    text = text.replace("…", "...");
    text = text.replace("×", "CLOSE");
    while (text.contains("  ")) text = text.replace("  ", " ");
    return text.trim();
}

void cleanComponent(juce::Component* component)
{
    if (component == nullptr) return;

    if (auto* button = dynamic_cast<juce::Button*>(component))
    {
        const auto current = button->getButtonText();
        const auto cleaned = cleanText(current);
        if (cleaned != current && cleaned.isNotEmpty())
            button->setButtonText(cleaned);
    }

    if (auto* label = dynamic_cast<juce::Label*>(component))
    {
        const auto current = label->getText();
        const auto cleaned = cleanText(current);
        if (cleaned != current)
            label->setText(cleaned, juce::dontSendNotification);
    }

    for (int i = 0; i < component->getNumChildComponents(); ++i)
        cleanComponent(component->getChildComponent(i));
}

class UISymbolCleaner final : private juce::Timer
{
public:
    UISymbolCleaner() { startTimerHz(8); }
    ~UISymbolCleaner() override { shutdown(); }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
    }

private:
    void timerCallback() override
    {
        if (stopped.load()) return;
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            cleanComponent(desktop.getComponent(i));
    }

    std::atomic<bool> stopped { false };
};

UISymbolCleaner cleaner;
}

void shutdownLibertyUISymbolCleaner()
{
    cleaner.shutdown();
}
