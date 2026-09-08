#include "MainComponent.h"

MainComponent::MainComponent()
{
    setSize(1440, 820);
    startTimerHz(30);
}

void MainComponent::paint(juce::Graphics& g)
{
    auto b = getLocalBounds();
    g.fillAll(juce::Colour(0xff0b0d10));
    auto transport = b.removeFromTop(76);
    auto mixer = b.removeFromBottom(210);
    drawTransport(g, transport);
    drawTrackArea(g, b);
    drawMixer(g, mixer);
}

void MainComponent::drawTransport(juce::Graphics& g, juce::Rectangle<int> a)
{
    g.setColour(juce::Colour(0xff15181d)); g.fillRect(a);
    g.setColour(juce::Colour(0xff30353d)); g.drawHorizontalLine(a.getBottom()-1, 0.0f, (float)getWidth());
    g.setColour(juce::Colours::white); g.setFont(juce::Font(24.0f, juce::Font::bold));
    g.drawText("BSM DAW", 22, 10, 170, 28, juce::Justification::left);
    const char* labels[]={"|<","<","PLAY",">","|>"};
    for(int i=0;i<5;++i){ auto r=juce::Rectangle<int>(215+i*62,38,56,28); g.setColour(i==2&&isPlaying?juce::Colour(0xff2d965e):juce::Colour(0xff252a31)); g.fillRoundedRectangle(r.toFloat(),5.0f); g.setColour(juce::Colour(0xff454b54)); g.drawRoundedRectangle(r.toFloat(),5.0f,1.0f); g.setColour(juce::Colours::white); g.setFont(juce::Font(11.0f,juce::Font::bold)); g.drawText(i==2&&isPlaying?"STOP":labels[i],r,juce::Justification::centred); }
    g.setFont(juce::Font(14.0f)); g.setColour(juce::Colour(0xffc9cdd3));
    g.drawText("120.00 BPM",560,40,110,24,juce::Justification::centred); g.drawText("4/4",680,40,50,24,juce::Justification::centred); g.drawText("00:00:00.000",750,40,160,24,juce::Justification::centred);
    g.setFont(juce::Font(12.0f)); g.setColour(juce::Colour(0xff858c96)); g.drawText("PROJECT  •  Untitled",getWidth()-230,40,205,24,juce::Justification::right);
}

void MainComponent::drawTrackArea(juce::Graphics& g, juce::Rectangle<int> area)
{
    const int headerW=210,rulerH=32,rowH=78; auto ruler=area.removeFromTop(rulerH); auto rows=area;
    g.setColour(juce::Colour(0xff12151a)); g.fillRect(ruler); g.setColour(juce::Colour(0xff20242b)); g.fillRect(rows.withWidth(headerW)); g.setColour(juce::Colour(0xff111419)); g.fillRect(rows.withTrimmedLeft(headerW));
    g.setColour(juce::Colour(0xff353b44)); for(int x=headerW;x<getWidth();x+=120) g.drawVerticalLine(x,(float)ruler.getY(),(float)rows.getBottom());
    g.setColour(juce::Colour(0xff777f89)); g.setFont(juce::Font(11.0f)); for(int i=0;i<12;++i) g.drawText(juce::String(i+1),headerW+i*120+6,ruler.getY()+7,35,18,juce::Justification::left);
    const char* names[]={"Audio 1","Audio 2","MIDI 1","Instrument 1"}; const char* types[]={"AUDIO","AUDIO","MIDI","INSTRUMENT"};
    for(int i=0;i<4;++i){ auto row=rows.removeFromTop(rowH); g.setColour(i%2?juce::Colour(0xff14171c):juce::Colour(0xff171a1f)); g.fillRect(row); auto h=row.removeFromLeft(headerW); g.setColour(juce::Colour(0xff1e232a)); g.fillRect(h); g.setColour(juce::Colours::white); g.setFont(juce::Font(14.0f,juce::Font::bold)); g.drawText(names[i],h.getX()+14,h.getY()+10,150,22,juce::Justification::left); g.setColour(juce::Colour(0xff747b85)); g.setFont(juce::Font(10.0f)); g.drawText(types[i],h.getX()+14,h.getY()+37,150,16,juce::Justification::left); auto clip=row.withTrimmedLeft(20+i*130).withWidth(250+i*35).reduced(4); g.setColour(juce::Colour(0xff31506a)); g.fillRoundedRectangle(clip.toFloat(),5.0f); g.setColour(juce::Colour(0xff709fc5)); g.drawRoundedRectangle(clip.toFloat(),5.0f,1.0f); g.setColour(juce::Colours::white); g.setFont(juce::Font(11.0f)); g.drawText(i==2?"MIDI Region":"Audio Clip",clip.reduced(10),juce::Justification::centredLeft); }
    const float px=headerW+(float)playheadSeconds*80.0f; g.setColour(juce::Colours::white); g.drawLine(px,(float)ruler.getY(),px,(float)area.getBottom(),2.0f);
}

void MainComponent::drawMixer(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(juce::Colour(0xff101318)); g.fillRect(area); const char* names[]={"Audio 1","Audio 2","MIDI 1","Instrument","MASTER"};
    for(int i=0;i<5;++i){ auto c=juce::Rectangle<int>(220+i*125,area.getY()+12,116,area.getHeight()-22); g.setColour(i==4?juce::Colour(0xff1b2027):juce::Colour(0xff171b20)); g.fillRoundedRectangle(c.toFloat(),5.0f); g.setColour(juce::Colour(0xff343a44)); g.drawRoundedRectangle(c.toFloat(),5.0f,1.0f); g.setColour(juce::Colours::white); g.setFont(juce::Font(12.0f,juce::Font::bold)); g.drawText(names[i],c.getX(),c.getY()+8,c.getWidth(),20,juce::Justification::centred); g.setColour(juce::Colour(0xff090b0e)); g.fillRoundedRectangle((float)c.getCentreX()-7,(float)c.getY()+38,14.0f,90.0f,3.0f); g.setColour(juce::Colour(0xffd6d9de)); g.fillRoundedRectangle((float)c.getCentreX()-5,(float)c.getY()+74,10.0f,20.0f,3.0f); g.setColour(juce::Colour(0xff858c96)); g.setFont(juce::Font(10.0f)); g.drawText("PAN",c.getX(),c.getBottom()-40,c.getWidth(),16,juce::Justification::centred); g.drawText(i==4?"0.0 dB":"-6.0 dB",c.getX(),c.getBottom()-22,c.getWidth(),16,juce::Justification::centred); }
}

void MainComponent::resized() {}

void MainComponent::mouseDown(const juce::MouseEvent& event)
{
    if (event.y >= 38 && event.y <= 66 && event.x >= 339 && event.x <= 395)
    {
        isPlaying = !isPlaying;
        repaint();
    }
}

void MainComponent::timerCallback()
{
    if(isPlaying){ playheadSeconds+=1.0/30.0; if(playheadSeconds>14.0) playheadSeconds=0.0; repaint(); }
}
