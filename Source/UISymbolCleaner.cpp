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
    text = text.replace("×", "X");
    // Preserve correctly encoded accents. Repair only exact, known
    // Windows-1252 mojibake sequences; never remove a legitimate
    // standalone French letter (including capital A circumflex).
    // All sequences below are assembled by Unicode codepoints so this
    // sanitizer cannot itself embed corrupted strings in the source.
    const auto unicode=[](int codepoint)
    {
        return juce::String::charToString(codepoint);
    };
    const auto brokenPrefix=unicode(0x00C3);
    if(text.contains(brokenPrefix))
    {
        struct AccentFix {int suffix;int correct;};
        // Trailing Windows-1252 codepoint -> original French character.
        constexpr AccentFix repairs[]={
            {0x00A0,0x00E0}, // a grave
            {0x00A2,0x00E2}, // a circumflex
            {0x00A4,0x00E4}, // a diaeresis
            {0x00A7,0x00E7}, // c cedilla
            {0x00A8,0x00E8}, // e grave
            {0x00A9,0x00E9}, // e acute
            {0x00AA,0x00EA}, // e circumflex
            {0x00AB,0x00EB}, // e diaeresis
            {0x00AE,0x00EE}, // i circumflex
            {0x00AF,0x00EF}, // i diaeresis
            {0x00B4,0x00F4}, // o circumflex
            {0x00B6,0x00F6}, // o diaeresis
            {0x00B9,0x00F9}, // u grave
            {0x00BB,0x00FB}, // u circumflex
            {0x00BC,0x00FC}, // u diaeresis
            {0x20AC,0x00C0}, // A grave
            {0x2021,0x00C7}, // C cedilla
            {0x02C6,0x00C8}, // E grave
            {0x2030,0x00C9}, // E acute
            {0x0160,0x00CA}, // E circumflex
            {0x2039,0x00CB}, // E diaeresis
            {0x017D,0x00CE}, // I circumflex
            {0x201D,0x00D4}, // O circumflex
            {0x2013,0x00D6}, // O diaeresis
            {0x203A,0x00DB}, // U circumflex
            {0x0152,0x00DC}  // U diaeresis
        };
        for(const auto& fix:repairs)
            text=text.replace(brokenPrefix+unicode(fix.suffix),
                              unicode(fix.correct));
    }
    const auto brokenA=unicode(0x00E2);
    const auto euro=unicode(0x20AC);
    text=text.replace(brokenPrefix+unicode(0x2014),"X");
    text=text.replace(brokenA+euro+unicode(0x201D)," - ");
    text=text.replace(brokenA+euro+unicode(0x201C),"-");
    text=text.replace(brokenA+unicode(0x2020)+unicode(0x2019),">");
    text=text.replace(brokenA+euro+unicode(0x00A2)," ");
    text=text.replace(brokenA+euro+unicode(0x2122),unicode(0x2019));
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
