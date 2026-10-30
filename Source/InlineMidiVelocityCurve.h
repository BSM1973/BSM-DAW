#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <cstdint>
#include <utility>
#include <vector>
#include <map>

namespace LibertyInlineVelocity
{
enum class Curve : int { linear = 0, exponential = 1, sCurve = 2 };
enum class Mode : int { absolute = 0, relative = 1 };

struct PreviewNote
{
    std::int64_t startTick = 0;
    int velocity = 100;
};

// Derive initial values from the actual selected notes. A chord is
// represented by its average velocity, not an arbitrary pitch index.
inline std::pair<int,int> boundaryVelocities(
    const std::vector<PreviewNote>& selected)
{
    if(selected.empty())return {64,100};
    std::int64_t firstTick=selected.front().startTick;
    std::int64_t lastTick=firstTick;
    for(const auto& note:selected){
        firstTick=std::min(firstTick,note.startTick);
        lastTick=std::max(lastTick,note.startTick);
    }
    std::int64_t firstSum=0,lastSum=0;
    int firstCount=0,lastCount=0;
    for(const auto& note:selected){
        const int velocity=juce::jlimit(1,127,note.velocity);
        if(note.startTick==firstTick){firstSum+=velocity;++firstCount;}
        if(note.startTick==lastTick){lastSum+=velocity;++lastCount;}
    }
    const int first=juce::jlimit(1,127,(int)std::lround(
        (double)firstSum/std::max(1,firstCount)));
    const int last=juce::jlimit(1,127,(int)std::lround(
        (double)lastSum/std::max(1,lastCount)));
    return {first,last};
}

// Real preview positions use the same musical-time interpolation as Apply.
inline double musicalPosition(std::int64_t tick,
                              std::int64_t first,
                              std::int64_t last) noexcept
{
    if(last<=first)return 0.0;
    return juce::jlimit(0.0,1.0,
        ((double)tick-(double)first)/((double)last-(double)first));
}

inline double progress(double position, Curve curve) noexcept
{
    const double t = juce::jlimit(0.0, 1.0, position);
    switch (curve)
    {
        case Curve::exponential: return t * t;
        case Curve::sCurve:      return t * t * (3.0 - 2.0 * t);
        case Curve::linear:
        default:                 return t;
    }
}
inline int velocityAt(int first, int last, double position, Curve curve) noexcept
{
    const double curved = progress(position, curve);
    return juce::jlimit(1, 127, (int)std::lround(first + (last - first) * curved));
}

// Shared by the non-destructive preview and the actual MIDI operation.
// Absolute replaces velocities with the envelope. Relative offsets the old
// velocities around their original (linear) phrase envelope, retaining local
// accents and chord dynamics. A common clamp per onset keeps chord intervals.
inline std::vector<int> transformVelocities(
    const std::vector<PreviewNote>& selected, int first, int last,
    Curve curve, Mode mode, int intensityPercent=100)
{
    std::vector<int> result;
    result.reserve(selected.size());
    if(selected.empty())return result;
    intensityPercent=juce::jlimit(0,100,intensityPercent);
    if(intensityPercent==0){
        for(const auto& note:selected)
            result.push_back(juce::jlimit(1,127,note.velocity));
        return result;
    }
    first=juce::jlimit(1,127,first);
    last=juce::jlimit(1,127,last);
    auto minTick=selected.front().startTick;
    auto maxTick=minTick;
    struct Range { int lowest=127, highest=1; };
    std::map<std::int64_t,Range> chordRanges;
    for(const auto& note:selected)
    {
        minTick=std::min(minTick,note.startTick);
        maxTick=std::max(maxTick,note.startTick);
        const int vel=juce::jlimit(1,127,note.velocity);
        auto& range=chordRanges[note.startTick];
        range.lowest=std::min(range.lowest,vel);
        range.highest=std::max(range.highest,vel);
    }
    const auto originalEndpoints=boundaryVelocities(selected);
    for(const auto& note:selected)
    {
        const double t=musicalPosition(note.startTick,minTick,maxTick);
        const int target=velocityAt(first,last,t,curve);
        if(mode==Mode::absolute)
        {
            const int oldValue=juce::jlimit(1,127,note.velocity);
            const int value=oldValue+(int)std::lround(
                ((double)target-oldValue)*intensityPercent/100.0);
            result.push_back(juce::jlimit(1,127,value));
            continue;
        }
        const int originalEnvelope=velocityAt(originalEndpoints.first,
            originalEndpoints.second,t,Curve::linear);
        const int requested=target-originalEnvelope;
        const auto& range=chordRanges.at(note.startTick);
        const int delta=juce::jlimit(1-range.lowest,127-range.highest,
                                   requested);
        // Chord notes share one rounded intensity-scaled delta.
        const int scaled=(int)std::lround((double)delta*intensityPercent/100.0);
        result.push_back(juce::jlimit(1,127,note.velocity+scaled));
    }
    return result;
}

// Standalone JUCE window content. No Piano Roll or instrument engine state is
// stored here. The callback is invoked only by explicit user confirmation.
class DialogContent final : public juce::Component
{
public:
    explicit DialogContent(int first, int last, Curve shape,
                           std::function<void(int, int, Curve, Mode, int)> apply,
                           std::vector<PreviewNote> selectedNotes = {})
        : onApply(std::move(apply)), samples(std::move(selectedNotes))
    {
        setOpaque(true);
        setSize(530, 538);
        std::stable_sort(samples.begin(),samples.end(),
            [](const PreviewNote& a,const PreviewNote& b){
                return a.startTick<b.startTick;
            });

        initialFirst=juce::jlimit(1,127,first);
        initialLast=juce::jlimit(1,127,last);
        initialCurve=shape;
        configureNumber(start, initialFirst, juce::String::fromUTF8("Vélocité de départ (1 à 127)"));
        configureNumber(end, initialLast, juce::String::fromUTF8("Vélocité d'arrivée (1 à 127)"));
        // Both controls must belong to the JUCE component tree: assigning
        // bounds alone does not display a TextEditor in a dialog.
        addAndMakeVisible(start);
        addAndMakeVisible(end);

        curve.addItem(juce::String::fromUTF8("Linéaire"), 1);
        curve.addItem(juce::String::fromUTF8("Exponentielle"), 2);
        curve.addItem(juce::String::fromUTF8("En S — lissage progressif"), 3);
        curve.setSelectedId((int)shape + 1, juce::dontSendNotification);
        curve.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff233747));
        curve.setColour(juce::ComboBox::textColourId, juce::Colour(0xfff2f7fc));
        curve.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff426278));
        curve.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xffffd28a));
        curve.onChange = [this] { repaint(); };
        addAndMakeVisible(curve);

        applicationMode.addItem(juce::String::fromUTF8(
            "Absolu — nouvelles vélocités"),1);
        applicationMode.addItem(juce::String::fromUTF8(
            "Relatif — préserver les nuances"),2);
        applicationMode.setSelectedId(1,juce::dontSendNotification);
        applicationMode.setTooltip(juce::String::fromUTF8(
            "Absolu remplace les vélocités. Relatif conserve les accents et écarts des accords."));
        applicationMode.setColour(juce::ComboBox::backgroundColourId,
                                  juce::Colour(0xff233747));
        applicationMode.setColour(juce::ComboBox::textColourId,
                                  juce::Colour(0xfff2f7fc));
        applicationMode.setColour(juce::ComboBox::outlineColourId,
                                  juce::Colour(0xff426278));
        applicationMode.setColour(juce::ComboBox::arrowColourId,
                                  juce::Colour(0xffffd28a));
        applicationMode.onChange=[this]{repaint();};
        addAndMakeVisible(applicationMode);

        intensity.setSliderStyle(juce::Slider::LinearHorizontal);
        intensity.setTextBoxStyle(juce::Slider::TextBoxRight,false,69,27);
        intensity.setRange(0.0,100.0,1.0);
        intensity.setValue(100.0,juce::dontSendNotification);
        intensity.setTextValueSuffix(" %");
        intensity.setTooltip(juce::String::fromUTF8(
            "0 % : notes originales ; 100 % : transformation complète"));
        intensity.setColour(juce::Slider::backgroundColourId,juce::Colour(0xff27445a));
        intensity.setColour(juce::Slider::trackColourId,juce::Colour(0xff65c4d6));
        intensity.setColour(juce::Slider::thumbColourId,juce::Colour(0xffffcc73));
        intensity.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour(0xff233747));
        intensity.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xffffe6b5));
        intensity.setColour(juce::Slider::textBoxOutlineColourId,juce::Colour(0xff426278));
        intensity.onValueChange=[this]{repaint();};
        addAndMakeVisible(intensity);

        resetButton.setButtonText(juce::String::fromUTF8("RÉINITIALISER"));
        resetButton.setTooltip(juce::String::fromUTF8(
            "Retrouver les vélocités d'origine et la courbe initiale"));
        resetButton.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff253949));
        resetButton.setColour(juce::TextButton::textColourOffId,juce::Colour(0xffbcd5e8));
        resetButton.onClick=[this]{restoreInitialSettings();};
        addAndMakeVisible(resetButton);
        invertButton.setButtonText(juce::String::fromUTF8("INVERSER"));
        invertButton.setTooltip(juce::String::fromUTF8(
            "Échanger le départ et l'arrivée sans modifier les notes MIDI"));
        invertButton.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff3f5470));
        invertButton.setColour(juce::TextButton::textColourOffId,juce::Colour(0xffffe0a2));
        invertButton.onClick=[this]{invertEndpoints();};
        addAndMakeVisible(invertButton);
        applyButton.setButtonText(juce::String::fromUTF8("APPLIQUER"));
        applyButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff25697e));
        applyButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffffffff));
        cancelButton.setButtonText(juce::String::fromUTF8("ANNULER"));
        cancelButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff263746));
        cancelButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe9f3fc));
        applyButton.onClick = [this] { confirm(); };
        cancelButton.onClick = [this]
        {
            if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
                window->exitModalState(0);
        };
        addAndMakeVisible(cancelButton);
        addAndMakeVisible(applyButton);
        updateValidation();
    }

    // Endpoints are the only mouse-active part of the preview. Moving a
    // handle updates the same numeric editors as manual typing and never
    // invokes the MIDI callback until APPLIQUER is pressed.
    static juce::Rectangle<float> previewPlot() noexcept
    {
        return {58.0f,336.0f,440.0f,100.0f};
    }
    juce::Point<float> endpointPosition(int which) const
    {
        int value=which==0?64:100;
        parseValue(which==0?start:end,value);
        const auto rect=previewPlot();
        return {which==0?rect.getX():rect.getRight(),
            rect.getBottom()-((float)(value-1)/126.0f)*rect.getHeight()};
    }
    int endpointAt(juce::Point<float> p) const
    {
        // A compact 15 px circular hit target does not block the plot.
        if(!error.isEmpty())return -1;
        for(int which=0;which<2;++which)
            if(p.getDistanceFrom(endpointPosition(which))<=15.0f)return which;
        return -1;
    }
    bool beginEndpointDragAt(juce::Point<float> p)
    {
        draggedEndpoint=endpointAt(p);
        if(draggedEndpoint<0)return false;
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        return true;
    }
    bool dragEndpointTo(float y)
    {
        if(draggedEndpoint<0)return false;
        const auto rect=previewPlot();
        const int value=juce::jlimit(1,127,(int)std::lround(
            1.0+(rect.getBottom()-y)*126.0/rect.getHeight()));
        auto& editor=draggedEndpoint==0?start:end;
        if(editor.getText()!=juce::String(value))
            editor.setText(juce::String(value),juce::sendNotificationSync);
        updateValidation();
        return true;
    }
    void endEndpointDrag()
    {
        draggedEndpoint=-1;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        beginEndpointDragAt(e.position);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if(draggedEndpoint>=0)dragEndpointTo(e.position.y);
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if(draggedEndpoint>=0)endEndpointDrag();
    }
    void mouseMove(const juce::MouseEvent& e) override
    {
        setMouseCursor(endpointAt(e.position)>=0
            ?juce::MouseCursor::UpDownResizeCursor
            :juce::MouseCursor::NormalCursor);
    }
    void mouseExit(const juce::MouseEvent&) override
    {
        if(draggedEndpoint<0)
            setMouseCursor(juce::MouseCursor::NormalCursor);
    }
    // These actions change only the UI preview. No engine callbacks occur
    // until the user explicitly presses APPLIQUER.
    void invertEndpoints()
    {
        int first=0,last=0;
        if(!parseValue(start,first)||!parseValue(end,last))return;
        start.setText(juce::String(last),juce::sendNotificationSync);
        end.setText(juce::String(first),juce::sendNotificationSync);
        updateValidation();
    }
    void restoreInitialSettings()
    {
        start.setText(juce::String(initialFirst),juce::sendNotificationSync);
        end.setText(juce::String(initialLast),juce::sendNotificationSync);
        curve.setSelectedId((int)initialCurve+1,juce::sendNotificationSync);
        applicationMode.setSelectedId(1,juce::sendNotificationSync);
        intensity.setValue(100.0,juce::sendNotificationSync);
        updateValidation();
    }
    void resized() override
    {
        start.setBounds(284, 78, 214, 33);
        end.setBounds(284, 124, 214, 33);
        curve.setBounds(284, 170, 214, 34);
        applicationMode.setBounds(284, 216, 214, 34);
        intensity.setBounds(278, 259, 220, 34);
        resetButton.setBounds(26, 491, 117, 33);
        invertButton.setBounds(151, 491, 117, 33);
        cancelButton.setBounds(277, 491, 104, 33);
        applyButton.setBounds(389, 491, 109, 33);
    }

    void paint(juce::Graphics& g) override
    {
        const auto canvas = juce::Colour(0xff101c2b);
        g.fillAll(canvas);
        g.setColour(juce::Colour(0xff3b647e));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(9.0f), 11.0f, 1.2f);

        g.setColour(juce::Colour(0xffffd28a));
        g.setFont(juce::Font(18.0f, juce::Font::bold));
        g.drawText(juce::String::fromUTF8("COURBE DE VÉLOCITÉ MIDI"),
                   26, 17, 470, 27, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff9eb8cd));
        g.setFont(juce::Font(11.5f));
        g.drawText(juce::String::fromUTF8("Arrangement  /  notes sélectionnées"),
                   26, 45, 470, 20, juce::Justification::centredLeft);

        g.setColour(juce::Colour(0xffe6f1fa));
        g.setFont(juce::Font(13.0f));
        g.drawText(juce::String::fromUTF8("Vélocité de départ"), 26, 78, 241, 33,
                   juce::Justification::centredLeft);
        g.drawText(juce::String::fromUTF8("Vélocité d'arrivée"), 26, 124, 241, 33,
                   juce::Justification::centredLeft);
        g.drawText(juce::String::fromUTF8("Courbe"), 26, 170, 241, 33,
                   juce::Justification::centredLeft);
        g.drawText(juce::String::fromUTF8("Application"), 26, 216, 241, 33,
                   juce::Justification::centredLeft);
        g.drawText(juce::String::fromUTF8("Intensité"), 26, 259, 241, 34,
                   juce::Justification::centredLeft);

        g.setColour(juce::Colour(0xffc1d7e9));
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(juce::String::fromUTF8("APERÇU  ·  ")
                       +juce::String((int)samples.size())
                       +juce::String::fromUTF8(" NOTE(S)"),
                   26, 310, 200, 20, juce::Justification::centredLeft);
        g.setFont(juce::Font(10.0f));
        g.setColour(juce::Colour(0xff8fb0c7));
        g.fillEllipse(276.0f,316.0f,7.0f,7.0f);
        g.drawText(juce::String::fromUTF8("Avant"),286,309,56,21,
                   juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xffffd28a));
        g.fillEllipse(359.0f,316.0f,7.0f,7.0f);
        g.drawText(juce::String::fromUTF8("Après"),369,309,58,21,
                   juce::Justification::centredLeft);

        const auto plot=previewPlot();
        g.setColour(juce::Colour(0xff172b3e));
        g.fillRoundedRectangle(plot.expanded(3.0f), 5.0f);
        g.setColour(juce::Colour(0xff4c687d));
        for (int i=0;i<=4;++i)
        {
            const float y=plot.getY()+plot.getHeight()*i/4.0f;
            g.drawHorizontalLine((int)std::lround(y),plot.getX(),plot.getRight());
        }
        for (int i=0;i<=4;++i)
        {
            const float x=plot.getX()+plot.getWidth()*i/4.0f;
            g.drawVerticalLine((int)std::lround(x),plot.getY(),plot.getBottom());
        }
        g.setColour(juce::Colour(0xff9cb5c9));
        g.setFont(juce::Font(10.0f));
        g.drawText("127", 22, 330, 32, 17, juce::Justification::centredRight);
        g.drawText("1", 22, 421, 32, 17, juce::Justification::centredRight);

        int first=64,last=100;
        if (!parseValue(start,first)) first=64;
        if (!parseValue(end,last)) last=100;
        const auto shape=selectedCurve();
        const auto mode=selectedMode();
        const auto transformed=transformVelocities(
            samples,first,last,shape,mode,getIntensity());
        juce::Path line;
        for (int i=0;i<=160;++i)
        {
            const double t=i/160.0;
            const float x=plot.getX()+(float)t*plot.getWidth();
            const float y=plot.getBottom()-
                ((velocityAt(first,last,t,shape)-1)/126.0f)*plot.getHeight();
            if (i==0)line.startNewSubPath(x,y);
            else line.lineTo(x,y);
        }
        g.setColour(juce::Colour(0xff09111d));
        g.strokePath(line,juce::PathStrokeType(5.0f));
        g.setColour(juce::Colour(0xff7fe0eb));
        g.strokePath(line,juce::PathStrokeType(2.6f));
        // Real note positions and their current / prospective velocities.
        // Cap on-screen markers to 120 for very dense selections; the
        // underlying note selection and Apply continue to use every note.
        if(!samples.empty())
        {
            const auto firstTick=samples.front().startTick;
            const auto lastTick=samples.back().startTick;
            const size_t stride=juce::jmax<size_t>(1,(samples.size()+119)/120);
            for(size_t i=0;i<samples.size();i+=stride)
            {
                const double t=musicalPosition(samples[i].startTick,
                                               firstTick,lastTick);
                const float x=plot.getX()+(float)t*plot.getWidth();
                const float oldY=plot.getBottom()-
                    ((juce::jlimit(1,127,samples[i].velocity)-1)/126.0f)*plot.getHeight();
                const int target=transformed[i];
                const float newY=plot.getBottom()-
                    ((target-1)/126.0f)*plot.getHeight();
                g.setColour(juce::Colour(0xff8fb0c7).withAlpha(.72f));
                g.drawLine(x-2.0f,plot.getBottom(),x-2.0f,oldY,1.2f);
                g.fillEllipse(x-5.0f,oldY-3.0f,6.0f,6.0f);
                g.setColour(juce::Colour(0xffffd28a).withAlpha(.91f));
                g.drawLine(x+2.0f,plot.getBottom(),x+2.0f,newY,1.5f);
                g.fillEllipse(x-1.0f,newY-3.0f,6.0f,6.0f);
            }
        }

        // Large gold endpoint handles stay above the note markers so they
        // remain draggable even when a note lies exactly at the boundary.
        for(int which=0;which<2;++which)
        {
            const auto p=endpointPosition(which);
            g.setColour(juce::Colour(0xff091827).withAlpha(.95f));
            g.fillEllipse(p.x-9.0f,p.y-9.0f,18.0f,18.0f);
            g.setColour(juce::Colour(0xfff4f9ff));
            g.fillEllipse(p.x-7.0f,p.y-7.0f,14.0f,14.0f);
            g.setColour(juce::Colour(0xffffc95e));
            g.fillEllipse(p.x-5.2f,p.y-5.2f,10.4f,10.4f);
        }

        if (!error.isEmpty())
        {
            g.setColour(juce::Colour(0xffffa9a1));
            g.setFont(juce::Font(11.0f));
            g.drawFittedText(error,25,446,472,18,juce::Justification::centredLeft,1);
        }
        else
        {
            g.setColour(juce::Colour(0xffa9bfd1));
            g.setFont(juce::Font(10.5f));
            g.drawText(getIntensity()==0
                ?juce::String::fromUTF8("0 % : aucune vélocité modifiée.")
                :mode==Mode::relative
                    ?juce::String::fromUTF8("Relatif : nuances et accents conservés.")
                    :juce::String::fromUTF8("Absolu : dosage progressif de la courbe."),
                26,446,470,17,juce::Justification::centredLeft);
        }
    }

private:
    static void configureNumber(juce::TextEditor& box,int initial,
                                const juce::String& tooltip)
    {
        box.setInputRestrictions(3,"0123456789");
        box.setText(juce::String(juce::jlimit(1,127,initial)),
                    juce::dontSendNotification);
        box.setJustification(juce::Justification::centred);
        box.setColour(juce::TextEditor::backgroundColourId,juce::Colour(0xff233747));
        box.setColour(juce::TextEditor::outlineColourId,juce::Colour(0xff426278));
        box.setColour(juce::TextEditor::textColourId,juce::Colour(0xffffe6b5));
        box.setTooltip(tooltip);
    }
    static bool parseValue(const juce::TextEditor& box,int& value)
    {
        const auto raw=box.getText().trim();
        if(raw.isEmpty()||raw.length()>3)return false;
        for(int i=0;i<raw.length();++i)
            if(!juce::CharacterFunctions::isDigit(raw[i]))return false;
        const int candidate=raw.getIntValue();
        if(candidate<1||candidate>127)return false;
        value=candidate;
        return true;
    }
    Curve selectedCurve() const noexcept
    {
        switch(curve.getSelectedId())
        {
            case 2:return Curve::exponential;
            case 3:return Curve::sCurve;
            default:return Curve::linear;
        }
    }
    Mode selectedMode() const noexcept
    {
        return applicationMode.getSelectedId()==2
            ?Mode::relative:Mode::absolute;
    }
    void updateValidation()
    {
        int a=0,b=0;
        error=(parseValue(start,a)&&parseValue(end,b))
            ? juce::String{}
            : juce::String::fromUTF8("Les deux valeurs doivent être comprises entre 1 et 127.");
        applyButton.setEnabled(error.isEmpty());
        invertButton.setEnabled(error.isEmpty());
        repaint();
    }
    void confirm()
    {
        int first=0,last=0;
        if(!parseValue(start,first)||!parseValue(end,last))
        {
            updateValidation();
            return;
        }
        if(onApply)onApply(first,last,selectedCurve(),selectedMode(),
                           getIntensity());
        if(auto* window=findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState(1);
    }

    juce::TextEditor start,end;
    juce::ComboBox curve,applicationMode;
    juce::Slider intensity;
    int draggedEndpoint=-1;
    std::vector<PreviewNote> samples;
    juce::TextButton applyButton,cancelButton,resetButton,invertButton;
    int initialFirst=64,initialLast=100;
    Curve initialCurve=Curve::linear;
    juce::String error;
    std::function<void(int,int,Curve,Mode,int)> onApply;
public:
    // Read-only access for automated validation; never edits any MIDI note.
    size_t getPreviewNoteCount() const noexcept { return samples.size(); }
    int getStartValue() const
    {
        int value=0;
        return parseValue(start,value)?value:0;
    }
    int getEndValue() const
    {
        int value=0;
        return parseValue(end,value)?value:0;
    }
    int getCurrentPreviewVelocity(size_t index) const
    {
        return getPreviewResultVelocity(index,getStartValue(),getEndValue(),
                                        selectedCurve(),selectedMode(),getIntensity());
    }
    void setIntensity(int percent)
    {
        intensity.setValue(juce::jlimit(0,100,percent),juce::sendNotificationSync);
    }
    int getIntensity() const noexcept
    {
        return juce::jlimit(0,100,(int)std::lround(intensity.getValue()));
    }
    void setMode(Mode mode)
    {
        applicationMode.setSelectedId((int)mode+1,juce::sendNotificationSync);
    }
    Mode getMode() const noexcept { return selectedMode(); }
    bool hasVisibleActionButtons() const noexcept
    {
        return resetButton.isVisible()&&invertButton.isVisible()
            &&applicationMode.isVisible()
            &&resetButton.getWidth()>0&&invertButton.getWidth()>0
            &&applicationMode.getWidth()>0
            &&intensity.isVisible()&&intensity.getWidth()>0;
    }
    int getPreviewOriginalVelocity(size_t index) const
    {
        return index<samples.size()?samples[index].velocity:0;
    }
    int getPreviewResultVelocity(size_t index,int first,int last,Curve shape,
                                 Mode mode=Mode::absolute,int percent=100) const
    {
        if(index>=samples.size())return 0;
        const auto transformed=transformVelocities(
            samples,first,last,shape,mode,percent);
        return transformed[index];
    }
    bool hasVisibleNumberFields() const noexcept
    {
        return start.isVisible()&&end.isVisible()
            &&start.getWidth()>0&&end.getWidth()>0;
    }
    // Attach callbacks only once all child components have been created.
    void bindInputs()
    {
        start.onTextChange=[this]{updateValidation();};
        end.onTextChange=[this]{updateValidation();};
        start.onReturnKey=[this]{confirm();};
        end.onReturnKey=[this]{confirm();};
    }
};
} // namespace LibertyInlineVelocity
