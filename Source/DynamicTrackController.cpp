#include "MainComponent.h"
#include "NoOverlapLayout.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>

int getLibertyTrackHeightPreset() noexcept;
void refreshLibertyRecordingTrackLayout(MainComponent*);
void refreshLibertyTrackHeaderMixLayout(MainComponent*);
void cycleLibertyTrackHeightPreset();
void showLibertyMixConsolePage(MainComponent*, bool);

namespace
{
class TrackHeightIcon final : public juce::Component
{
public:
    explicit TrackHeightIcon(MainComponent& main) : owner(main) { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    void paint(juce::Graphics& g) override
    {
        const int preset = getLibertyTrackHeightPreset();
        g.setColour(juce::Colour(0xff80d8e8));
        const int bars[] = { 3, 6, 10 };
        for (int i = 0; i < 3; ++i)
        {
            const int h = bars[i];
            g.setColour(i == preset ? juce::Colour(0xff54d6dc) : juce::Colour(0xff647c8c));
            g.fillRoundedRectangle(1.0f + i * 6.0f, 18.0f - h, 5.0f, (float)h, 1.5f);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isLeftButtonDown()) { cycleLibertyTrackHeightPreset(); repaint(); owner.applyArrangeTrackHeightPreset(); owner.resized(); }
    }
private:
    MainComponent& owner;
};

class DynamicTrackController final : public juce::Component, private juce::Timer
{
public:
    explicit DynamicTrackController(MainComponent& o) : owner(o), heightIcon(o)
    {
        addAudio.setButtonText("+ AUDIO");
        addAudio.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff245b70));
        addAudio.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        addAudio.setMouseClickGrabsKeyboardFocus(false);
        addAudio.onClick = [this]
        {
            owner.addAudioTrack();
            countLabel.setVisible(false);
        };
        addMidi.setButtonText("+ MIDI");
        addInstrument.setButtonText("+ INSTRUMENT");
        addBus.setButtonText("+ BUS");
        addBus.setTooltip(juce::String::fromUTF8(
            "Créer une piste BUS stéréo dans la Console de mixage."));
        for (auto* b : { &addAudio, &addMidi, &addInstrument, &addBus })
        {
            b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff245b70));
            b->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            b->setMouseClickGrabsKeyboardFocus(false);
            addAndMakeVisible(*b);
        }
        addMidi.onClick = [this] { owner.addMidiTrack(); };
        addInstrument.onClick = [this] { owner.addInstrumentTrack(); };
        addBus.onClick = [this]
        {
            if (owner.addMixerBusTrack() >= 0)
                showLibertyMixConsolePage(&owner, true);
        };
        countLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9fc7e8));
        countLabel.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(countLabel);
        addAndMakeVisible(heightIcon);
        setInterceptsMouseClicks(false, true);
        owner.addAndMakeVisible(this);
        setBounds(owner.getLocalBounds());
        startTimerHz(8);
    }
    void paint(juce::Graphics& g) override
    {
        // A single reserved creation row, distinct from the wordmark above
        // and the mixer captions to its right, on every Liberty page.
        const auto c = LibertyNoOverlap::creationZone;
        g.setColour(juce::Colour(0xff122332));
        g.fillRoundedRectangle((float)c.x+3.f,(float)c.y+1.f,
            (float)c.w-6.f,(float)c.h-3.f,6.f);
        g.setColour(juce::Colour(0xff405c72));
        g.drawRoundedRectangle((float)c.x+3.f,(float)c.y+1.f,
            (float)c.w-6.f,(float)c.h-3.f,6.f,1.f);
    }
    void resized() override
    {
        auto put=[](juce::Component& component,LibertyNoOverlap::Rect r)
        {
            component.setBounds(r.x,r.y,r.w,r.h);
        };
        put(addAudio,LibertyNoOverlap::audioButton);
        put(addMidi,LibertyNoOverlap::midiButton);
        put(addInstrument,LibertyNoOverlap::instrumentButton);
        put(addBus,LibertyNoOverlap::busButton);
        put(heightIcon,LibertyNoOverlap::heightIcon);
        countLabel.setVisible(false);
    }
public:
    void resizeToOwner() { setBounds(owner.getLocalBounds()); }
    void timerCallback() override
    {
        if (getBounds() != owner.getLocalBounds()) setBounds(owner.getLocalBounds());
        // Other full-window overlay controllers can obscure these controls.
        // Restore their z-order without intercepting clicks outside the controls.
        toFront(false);
    }
private:
    MainComponent& owner;
    juce::TextButton addAudio, addMidi, addInstrument, addBus;
    juce::Label countLabel;
    TrackHeightIcon heightIcon;
};

std::map<MainComponent*, std::unique_ptr<DynamicTrackController>> controllers;

class Bootstrap final : private juce::Timer
{
public:
    Bootstrap() { startTimerHz(4); }
    ~Bootstrap() override { stopTimer(); controllers.clear(); }
private:
    void timerCallback() override
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* window = dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i)))
                if (auto* main = dynamic_cast<MainComponent*>(window->getContentComponent()))
                    if (!controllers.count(main))
                    {
                        controllers.emplace(main, std::make_unique<DynamicTrackController>(*main));
                        stopTimer();
                        return;
                    }
    }
};
Bootstrap bootstrap;
}

void resizeLibertyDynamicTrackController(MainComponent* owner)
{
    if (owner == nullptr) return;
    if (controllers.find(owner) == controllers.end())
        controllers.emplace(owner, std::make_unique<DynamicTrackController>(*owner));
    auto it = controllers.find(owner);
    if (it != controllers.end() && it->second)
        it->second->resizeToOwner();
}
