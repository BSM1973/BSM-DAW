#define private public
#include "MainComponent.h"
#undef private

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <vector>
#include <map>
#include <memory>

double getLibertyTimelinePixelsPerSecond() noexcept;
int getLibertyTrackRowHeight() noexcept;

namespace
{
constexpr int colourDefault = 0;
constexpr int headerW = 210;
constexpr int rulerH = 32;
constexpr std::array<juce::uint32, 33> palette {
    0xff31506a, // Default
    0xff3b82f6, 0xff22c55e, 0xffeab308, 0xfff97316,
    0xffef4444, 0xffa855f7, 0xffec4899, 0xff14b8a6,
    0xff60a5fa, 0xff84cc16, 0xfffacc15, 0xfffb923c,
    0xfff87171, 0xffc084fc, 0xfff472b6, 0xff2dd4bf,
    0xff1d4ed8, 0xff15803d, 0xffa16207, 0xffc2410c,
    0xffb91c1c, 0xff7e22ce, 0xffbe185d, 0xff0f766e,
    0xff93c5fd, 0xffbbf7d0, 0xfffde68a, 0xfffed7aa,
    0xfffecaca, 0xffe9d5ff, 0xfffbcfe8, 0xff99f6e4
};

juce::String colourName(int id)
{
    if (id == 0) return juce::String::fromUTF8("Défaut");
    return juce::String::fromUTF8("Couleur ") + juce::String(id);
}

class ColourGridPalette final : public juce::Component
{
public:
    ColourGridPalette(int selected, std::function<void(int)> onSelect)
        : selectedId(juce::jlimit(0, 32, selected)), selection(std::move(onSelect))
    {
        setOpaque(true);
        expanded = selectedId > 16;
        setSize(336, expanded ? 292 : 214);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff142633));
        g.setColour(juce::Colour(0xff54d6dc));
        g.fillRoundedRectangle(12.0f, 12.0f, 4.0f, 25.0f, 2.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(17.0f, juce::Font::bold));
        g.drawText("COULEUR DE PISTE", 25, 12, 290, 26, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff89aab9));
        g.setFont(juce::Font(13.0f));
        g.drawText(expanded ? "32 couleurs" : "16 couleurs", 18, 46, 300, 20,
                   juce::Justification::centredLeft);

        // All swatches are painted and hit-tested by this single component.
        // No child buttons or overlapping desktop peers can swallow rows 2-4.
        const int count = expanded ? 32 : 16;
        for (int i = 0; i < count; ++i)
        {
            const auto rect = swatchBounds(i);
            g.setColour(juce::Colour(palette[(size_t)i + 1]));
            g.fillRoundedRectangle(rect.toFloat(), 5.0f);
            if (selectedId == i + 1)
            {
                g.setColour(juce::Colours::white);
                g.drawRoundedRectangle(rect.expanded(2).toFloat(), 6.0f, 2.0f);
            }
        }
        const int bottom = expanded ? 246 : 168;
        g.setColour(juce::Colour(0xff263d4b));
        g.fillRoundedRectangle(18.0f, (float)bottom, 184.0f, 30.0f, 5.0f);
        g.fillRoundedRectangle(208.0f, (float)bottom, 110.0f, 30.0f, 5.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(12.0f));
        g.drawFittedText(expanded ? "-  Moins de couleurs" : "+  16 couleurs supplémentaires",
                         22, bottom, 176, 30, juce::Justification::centred, 1);
        g.drawFittedText("Par défaut", 210, bottom, 106, 30,
                         juce::Justification::centred, 1);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition();
        const int count = expanded ? 32 : 16;
        for (int i = 0; i < count; ++i)
        {
            if (swatchBounds(i).contains(p))
            {
                selectedId = i + 1;
                repaint();
                selection(selectedId);
                return;
            }
        }
        const int bottom = expanded ? 246 : 168;
        if (juce::Rectangle<int>(18, bottom, 184, 30).contains(p))
        {
            expanded = !expanded;
            setSize(336, expanded ? 292 : 214);
            repaint();
        }
        else if (juce::Rectangle<int>(208, bottom, 110, 30).contains(p))
        {
            selectedId = 0;
            selection(0);
        }
    }

private:
    static juce::Rectangle<int> swatchBounds(int i)
    {
        return { 18 + (i % 8) * 39, 76 + (i / 8) * 39, 32, 30 };
    }
    int selectedId = 0;
    bool expanded = false;
    std::function<void(int)> selection;
};

// Native JUCE popup menus avoid temporary-window hit-testing failures on macOS.
// Every colour has its own menu command; the submenu groups keep all 32 reachable.
void showColourGrid(juce::Point<int> screenPoint, int selectedId, std::function<void(int)> onSelect)
{
    juce::PopupMenu menu;
    menu.addItem(100, juce::String::fromUTF8("Couleur par défaut"), true, selectedId == 0);
    menu.addSeparator();
    for (int group = 0; group < 4; ++group)
    {
        juce::PopupMenu colours;
        for (int index = group * 8 + 1; index <= (group + 1) * 8; ++index)
            colours.addItem(index, colourName(index), true, selectedId == index);
        const auto title = juce::String::fromUTF8("Couleurs ")
            + juce::String(group * 8 + 1) + " - " + juce::String((group + 1) * 8);
        menu.addSubMenu(title, colours);
    }
    juce::PopupMenu::Options options;
    options = options.withTargetScreenArea({screenPoint.x, screenPoint.y, 1, 1});
    menu.showMenuAsync(options, [onSelect = std::move(onSelect)](int result)
    {
        if (result == 0)
            return; // Dismissed without selecting.
        const int selected = result == 100 ? 0 : result;
        if (selected < 0 || selected > 32)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                "LIBERTY - Diagnostic couleur", "Identifiant de menu inattendu : " + juce::String(result));
            return;
        }
        onSelect(selected);
    });
}

std::vector<int> colourIds;
std::vector<juce::String> trackNames;
void ensureTrackMetadata(int count){if(count<0)count=0;if((int)colourIds.size()<count)colourIds.resize((size_t)count,0);if((int)trackNames.size()<count)trackNames.resize((size_t)count);}


juce::String defaultTrackName(const MainComponent& owner,int track)
{
    const int a=owner.getAudioTrackCount(),m=owner.getMidiTrackCount(),n=owner.getInstrumentTrackCount();
    if(track>=0&&track<a)return "Audio "+juce::String(track+1);
    if(track<a+m)return "MIDI "+juce::String(track-a+1);
    if(track<a+m+n)return "Instrument "+juce::String(track-a-m+1);
    return {};
}
juce::String effectiveTrackName(const MainComponent& owner,int track)
{
    const int total=owner.getTotalArrangeTrackCount();ensureTrackMetadata(total);
    if(track<0||track>=total)return {};
    return trackNames[(size_t)track].isNotEmpty()?trackNames[(size_t)track]:defaultTrackName(owner,track);
}

juce::Colour colourForId(int id)
{
    if (id <= 0 || id >= (int)palette.size()) return juce::Colour(palette[0]);
    return juce::Colour(palette[(size_t)id]);
}

class TrackColourController final : public juce::Component, private juce::Timer
{
public:
    explicit TrackColourController(MainComponent& ownerIn) : owner(ownerIn), listener(*this)
    {
        setInterceptsMouseClicks(false, true);
        owner.addAndMakeVisible(this);
        owner.addMouseListener(&listener, true);
        // Also receive clicks on overlay controls and other nested header components.

        renameEditor.setVisible(false);
        renameEditor.setSelectAllWhenFocused(true);
        renameEditor.setReturnKeyStartsNewLine(false);
        renameEditor.setEscapeAndReturnKeysConsumed(false);
        renameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff161a20));
        renameEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
        renameEditor.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff63c7e8));
        renameEditor.onReturnKey = [this] { finishRename(true); };
        renameEditor.onEscapeKey = [this] { finishRename(false); };
        renameEditor.onFocusLost = [this] { if (editingTrack >= 0) finishRename(true); };
        addAndMakeVisible(renameEditor);
        renameEditor.setVisible(false);
        startTimerHz(8);
    }

    ~TrackColourController() override { shutdown(); }

    void shutdown()
    {
        if (shutDown) return;
        shutDown = true;
        stopTimer();
        renameEditor.onReturnKey = nullptr;
        renameEditor.onEscapeKey = nullptr;
        renameEditor.onFocusLost = nullptr;
        owner.removeMouseListener(&listener);
        setVisible(false);
    }

    void paint(juce::Graphics& g) override
    {
        // Track overlays belong strictly to the arranger viewport. Never allow
        // dynamic rows to paint over the fixed mixer at the bottom.
        g.reduceClipRegion(juce::Rectangle<int>(0, 108, getWidth(),
                                                juce::jmax(0, owner.getHeight() - 108 - 218)));
        const double pixelsPerSecond = getLibertyTimelinePixelsPerSecond();
        const int rowH = getLibertyTrackRowHeight();

        const int totalTracks=owner.getTotalArrangeTrackCount(); ensureTrackMetadata(totalTracks);
        const int scroll=owner.getTrackScrollRows();
        for (int i = 0; i < totalTracks; ++i)
        {
            const int id = colourIds[(size_t)i];
            const auto colour = colourForId(id);
            const int rowY = 76 + rulerH + (i-scroll) * rowH;
            const auto header = juce::Rectangle<int>(0, rowY, headerW, rowH);

            if (id != colourDefault)
            {
                g.setColour(colour.withAlpha(0.24f));
                g.fillRect(header);
                g.setColour(colour.withAlpha(0.95f));
                g.fillRect(header.getX(), header.getY(), 5, header.getHeight());
                g.fillRect(header.getX(), header.getY(), header.getWidth(), 3);

                if (i < owner.getAudioTrackCount() && owner.audioEngine.hasAudioFile(i))
                {
                    const int clipX = headerW + (int)std::round(owner.audioEngine.getTrackStartSeconds(i) * pixelsPerSecond);
                    const int clipW = juce::jmax(1, (int)std::round(owner.audioEngine.getAudioFileLengthSeconds(i) * pixelsPerSecond));
                    const auto clip = juce::Rectangle<int>(clipX, rowY + 4, clipW, rowH - 8);
                    g.setColour(colour.withAlpha(0.20f));
                    g.fillRoundedRectangle(clip.toFloat(), 5.0f);
                    g.setColour(colour.withAlpha(0.95f));
                    g.drawRoundedRectangle(clip.toFloat(), 5.0f, 2.0f);
                }
                if (i < owner.getAudioTrackCount())
                {
                    const int mixerTop = owner.getHeight() - 210;
                    const auto strip = juce::Rectangle<int>(220 + i * 125, mixerTop + 12, 116, 188);
                    g.setColour(colour.withAlpha(0.12f));
                    g.fillRoundedRectangle(strip.toFloat(), 5.0f);
                    g.setColour(colour.withAlpha(0.95f));
                    g.fillRoundedRectangle((float)strip.getX(), (float)strip.getY(), (float)strip.getWidth(), 5.0f, 2.0f);
                    g.drawRoundedRectangle(strip.toFloat(), 5.0f, 1.5f);
                }
            }

            if (editingTrack != i)
            {
                const auto titleArea = juce::Rectangle<int>(8, rowY + 6, 96, 24);
                g.setColour(juce::Colour(0xff1e232a));
                g.fillRect(titleArea);
                if (id != colourDefault)
                {
                    g.setColour(colour.withAlpha(0.20f));
                    g.fillRect(titleArea);
                }
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(13.0f, juce::Font::bold));
                g.drawText(effectiveTrackName(owner,i), titleArea.reduced(4, 0), juce::Justification::centredLeft, true);
            }

            if (i < owner.getAudioTrackCount())
            {
                const int mixerTop = owner.getHeight() - 210;
                const auto mixerTitle = juce::Rectangle<int>(224 + i * 125, mixerTop + 18, 108, 20);
                g.setColour(juce::Colour(0xff171b20));
                g.fillRect(mixerTitle);
                if (id != colourDefault)
                {
                    g.setColour(colour.withAlpha(0.16f));
                    g.fillRect(mixerTitle);
                }
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(11.0f, juce::Font::bold));
                g.drawText(effectiveTrackName(owner,i), mixerTitle, juce::Justification::centred, true);
            }
        }
    }

private:
    class OwnerMouseListener final : public juce::MouseListener
    {
    public:
        explicit OwnerMouseListener(TrackColourController& c) : controller(c) {}
        void mouseDown(const juce::MouseEvent& event) override
        {
            const auto relative = event.getEventRelativeTo(&controller.owner);
            if (event.mods.isPopupMenu())
            {
                controller.showColourMenu(relative);
                return;
            }
            if (event.getNumberOfClicks() >= 2)
                controller.beginRename(relative.getPosition());
        }
    private:
        TrackColourController& controller;
    };

    void beginRename(juce::Point<int> point)
    {
        if (point.x < 8 || point.x > 104) return;
        const int rowH = getLibertyTrackRowHeight();
        const int y = point.y - 76 - rulerH;
        if (y < 0) return;
        const int visibleTrack = y / rowH;
        const int track = visibleTrack + owner.getTrackScrollRows();
        const int localY = y % rowH;
        const int totalTracks=owner.getTotalArrangeTrackCount(); ensureTrackMetadata(totalTracks);
        if (track < 0 || track >= totalTracks || localY < 4 || localY > 32) return;

        if (editingTrack >= 0) finishRename(true);
        editingTrack = track;
        renameEditor.setText(effectiveTrackName(owner,track), false);
        renameEditor.setBounds(8, 76 + rulerH + (track-owner.getTrackScrollRows()) * rowH + 6, 96, 24);
        renameEditor.setVisible(true);
        renameEditor.toFront(true);
        renameEditor.grabKeyboardFocus();
        renameEditor.selectAll();
        repaint();
    }

    void finishRename(bool commit)
    {
        if (editingTrack < 0) return;
        const int track = editingTrack;
        editingTrack = -1;
        if (commit)
        {
            auto text = renameEditor.getText().trim();
            if (text.isEmpty()) text = defaultTrackName(owner,track);
            if (text.length() > 32) text = text.substring(0, 32);
            trackNames[(size_t)track] = text;
        }
        renameEditor.setVisible(false);
        owner.grabKeyboardFocus();
        repaint();
        owner.repaint();
    }

    void showColourMenu(const juce::MouseEvent& event)
    {
        const auto p = event.getPosition();
        if (p.x < 0 || p.x >= headerW || p.y >= owner.getMixerTop()) return;
        const int rowH = getLibertyTrackRowHeight();
        const int y = p.y - 76 - rulerH;
        if (y < 0) return;
        const int track = y / rowH + owner.getTrackScrollRows();
        const int totalTracks=owner.getTotalArrangeTrackCount(); ensureTrackMetadata(totalTracks);
        if (track < 0 || track >= totalTracks) return;

        owner.selectedTrack=track;
        owner.repaint();

        juce::Component::SafePointer<MainComponent> safeOwner(&owner);
        juce::Component::SafePointer<TrackColourController> safeController(this);
        showColourGrid(event.getScreenPosition(), colourIds[(size_t)track],
            [safeOwner, safeController, track](int id)
            {
                if (safeOwner == nullptr) return;
                ensureTrackMetadata(track + 1);
                colourIds[(size_t)track] = juce::jlimit(0, 32, id);
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                    "LIBERTY - Application couleur",
                    "Piste " + juce::String(track + 1) + " : couleur demandee " + juce::String(id)
                    + ", valeur stockee " + juce::String(colourIds[(size_t)track]));
                // Repaint the overlay that actually draws track colours.
                if (safeController != nullptr)
                    safeController->repaint();
                safeOwner->repaint();
            });
    }

    void resized() override
    {
        if (editingTrack >= 0)
            renameEditor.setBounds(8, 76 + rulerH + (editingTrack-owner.getTrackScrollRows()) * getLibertyTrackRowHeight() + 6, 96, 24);
    }

    void timerCallback() override
    {
        if (shutDown) return;
        const auto wantedBounds = owner.getLocalBounds();
        if (getBounds() != wantedBounds)
            setBounds(wantedBounds);
        else
            resized();
        if (editingTrack >= 0)
            renameEditor.toFront(true);
    }

    MainComponent& owner;
    OwnerMouseListener listener;
    juce::TextEditor renameEditor;
    int editingTrack = -1;
    bool shutDown = false;
};

std::map<MainComponent*, std::unique_ptr<TrackColourController>> controllers;

class TrackColourBootstrap final : private juce::Timer
{
public:
    TrackColourBootstrap() { startTimerHz(10); }
    ~TrackColourBootstrap() override { shutdown(); }

    void shutdown()
    {
        stopTimer();
        for (auto& entry : controllers)
            if (entry.second != nullptr) entry.second->shutdown();
        controllers.clear();
    }

private:
    void timerCallback() override
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* window = dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i)))
                if (auto* main = dynamic_cast<MainComponent*>(window->getContentComponent()))
                    if (controllers.find(main) == controllers.end())
                    {
                        controllers.emplace(main, std::make_unique<TrackColourController>(*main));
                        stopTimer();
                        return;
                    }
    }
};

TrackColourBootstrap trackColourBootstrap;
}

int getLibertyTrackColourId(int track)
{
    if (track < 0) return 0; ensureTrackMetadata(track+1);
    return colourIds[(size_t)track];
}

void setLibertyTrackColourId(int track, int colourId)
{
    if (track < 0) return; ensureTrackMetadata(track+1);
    colourIds[(size_t)track] = juce::jlimit(0, (int)palette.size() - 1, colourId);
}

void showLibertyTrackHeaderColourMenu(MainComponent& owner, juce::Point<int> screenPoint, int track)
{
    const int total = owner.getTotalArrangeTrackCount();
    if (track < 0 || track >= total) return;
    ensureTrackMetadata(total);
    owner.selectedTrack = track;
    owner.repaint();
    juce::Component::SafePointer<MainComponent> safeOwner(&owner);
    showColourGrid(screenPoint, colourIds[(size_t)track],
        [safeOwner, track](int id)
        {
            if (safeOwner == nullptr) return;
            ensureTrackMetadata(track + 1);
            colourIds[(size_t)track] = juce::jlimit(0, 32, id);
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                "LIBERTY - Application couleur",
                "Piste " + juce::String(track + 1) + " : couleur demandee " + juce::String(id)
                + ", valeur stockee " + juce::String(colourIds[(size_t)track]));
            auto controller = controllers.find(safeOwner.getComponent());
            if (controller != controllers.end() && controller->second != nullptr)
                controller->second->repaint();
            safeOwner->repaint();
        });
}

void resetLibertyTrackColours() { std::fill(colourIds.begin(),colourIds.end(),0); }

juce::String getLibertyTrackName(int track) { if(track<0)return {};ensureTrackMetadata(track+1);return trackNames[(size_t)track]; }

void setLibertyTrackName(int track, const juce::String& name)
{
    if (track < 0) return; ensureTrackMetadata(track+1);
    auto clean = name.trim();
    if (clean.length() > 32) clean = clean.substring(0, 32);
    trackNames[(size_t)track] = clean;
}

void resetLibertyTrackNames()
{
    for (auto& name : trackNames) name.clear();
}

void shutdownLibertyTrackColourInteraction()
{
    trackColourBootstrap.shutdown();
}
