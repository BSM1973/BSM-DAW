#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <cstdint>
#include <utility>
#include <vector>

namespace LibertyInlineVelocity
{
enum class Curve : int { linear = 0, exponential = 1, sCurve = 2 };

struct PreviewNote
{
    std::int64_t startTick = 0;
    int velocity = 100;
};

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

// Standalone JUCE window content. No Piano Roll or instrument engine state is
// stored here. The callback is invoked only by explicit user confirmation.
class DialogContent final : public juce::Component
{
public:
    explicit DialogContent(int first, int last, Curve shape,
                           std::function<void(int, int, Curve)> apply,
                           std::vector<PreviewNote> selectedNotes = {})
        : onApply(std::move(apply)), samples(std::move(selectedNotes))
    {
        setOpaque(true);
        setSize(530, 420);
        std::stable_sort(samples.begin(),samples.end(),
            [](const PreviewNote& a,const PreviewNote& b){
                return a.startTick<b.startTick;
            });

        configureNumber(start, first, juce::String::fromUTF8("Vélocité de départ (1 à 127)"));
        configureNumber(end, last, juce::String::fromUTF8("Vélocité d'arrivée (1 à 127)"));
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

    void resized() override
    {
        start.setBounds(284, 78, 214, 33);
        end.setBounds(284, 124, 214, 33);
        curve.setBounds(284, 170, 214, 34);
        cancelButton.setBounds(277, 370, 104, 33);
        applyButton.setBounds(389, 370, 109, 33);
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

        g.setColour(juce::Colour(0xffc1d7e9));
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(juce::String::fromUTF8("APERÇU  ·  ")
                       +juce::String((int)samples.size())
                       +juce::String::fromUTF8(" NOTE(S)"),
                   26, 220, 200, 20, juce::Justification::centredLeft);
        g.setFont(juce::Font(10.0f));
        g.setColour(juce::Colour(0xff8fb0c7));
        g.fillEllipse(276.0f,226.0f,7.0f,7.0f);
        g.drawText(juce::String::fromUTF8("Avant"),286,219,56,21,
                   juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xffffd28a));
        g.fillEllipse(359.0f,226.0f,7.0f,7.0f);
        g.drawText(juce::String::fromUTF8("Après"),369,219,58,21,
                   juce::Justification::centredLeft);

        const auto plot=juce::Rectangle<float>(58.0f, 246.0f, 440.0f, 100.0f);
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
        g.drawText("127", 22, 240, 32, 17, juce::Justification::centredRight);
        g.drawText("1", 22, 331, 32, 17, juce::Justification::centredRight);

        int first=64,last=100;
        if (!parseValue(start,first)) first=64;
        if (!parseValue(end,last)) last=100;
        const auto shape=selectedCurve();
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
        for (int i : {0,160})
        {
            const float t=i/160.0f;
            const float x=plot.getX()+t*plot.getWidth();
            const float y=plot.getBottom()-
                ((velocityAt(first,last,t,shape)-1)/126.0f)*plot.getHeight();
            g.setColour(juce::Colour(0xffffd28a));
            g.fillEllipse(x-4.5f,y-4.5f,9.0f,9.0f);
        }

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
                const int target=velocityAt(first,last,t,shape);
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

        if (!error.isEmpty())
        {
            g.setColour(juce::Colour(0xffffa9a1));
            g.setFont(juce::Font(11.0f));
            g.drawFittedText(error,25,348,472,18,juce::Justification::centredLeft,1);
        }
        else
        {
            g.setColour(juce::Colour(0xffa9bfd1));
            g.setFont(juce::Font(10.5f));
            g.drawText(juce::String::fromUTF8(
                "La progression suit la position musicale des notes."),
                26,348,470,17,juce::Justification::centredLeft);
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
    void updateValidation()
    {
        int a=0,b=0;
        error=(parseValue(start,a)&&parseValue(end,b))
            ? juce::String{}
            : juce::String::fromUTF8("Les deux valeurs doivent être comprises entre 1 et 127.");
        applyButton.setEnabled(error.isEmpty());
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
        if(onApply)onApply(first,last,selectedCurve());
        if(auto* window=findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState(1);
    }

    juce::TextEditor start,end;
    juce::ComboBox curve;
    std::vector<PreviewNote> samples;
    juce::TextButton applyButton,cancelButton;
    juce::String error;
    std::function<void(int,int,Curve)> onApply;
public:
    // Read-only access for automated validation; never edits any MIDI note.
    size_t getPreviewNoteCount() const noexcept { return samples.size(); }
    int getPreviewOriginalVelocity(size_t index) const
    {
        return index<samples.size()?samples[index].velocity:0;
    }
    int getPreviewResultVelocity(size_t index,int first,int last,Curve shape) const
    {
        if(index>=samples.size()||samples.empty())return 0;
        const auto t=musicalPosition(samples[index].startTick,
            samples.front().startTick,samples.back().startTick);
        return velocityAt(first,last,t,shape);
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
