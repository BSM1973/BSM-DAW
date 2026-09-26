#include "OneKnobEffects.h"
#include <cmath>

namespace
{
class OneKnobEditor final : public juce::Component, private juce::Timer
{
public:
    OneKnobEditor(LibertyOneKnobManager& m, int t) : manager(m), track(t)
    {
        knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 78, 24);
        knob.setRange(0.0, 100.0, 1.0);
        knob.setValue(manager.getAmount(track) * 100.0, juce::dontSendNotification);
        knob.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff22c7ff));
        knob.setColour(juce::Slider::thumbColourId, juce::Colours::white);
        knob.onValueChange = [this] { manager.setAmount(track, (float)knob.getValue() / 100.0f); };
        addAndMakeVisible(knob);
        startTimerHz(8);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff11151b));
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(18.0f, juce::Font::bold));
        g.drawText("LIBERTY", 0, 14, getWidth(), 26, juce::Justification::centred);
        g.setColour(juce::Colour(0xff22c7ff));
        g.setFont(juce::Font(15.0f, juce::Font::bold));
        g.drawText(manager.getName(track).toUpperCase(), 0, 42, getWidth(), 24, juce::Justification::centred);
        g.setColour(juce::Colour(0xff89939e));
        g.setFont(juce::Font(11.0f));
        g.drawText("ONE KNOB", 0, getHeight() - 34, getWidth(), 18, juce::Justification::centred);
    }

    void resized() override { knob.setBounds(getLocalBounds().reduced(50, 72)); }

private:
    void timerCallback() override
    {
        const auto wanted = manager.getAmount(track) * 100.0f;
        if (std::abs((float)knob.getValue() - wanted) > 0.01f)
            knob.setValue(wanted, juce::dontSendNotification);
    }

    LibertyOneKnobManager& manager;
    int track;
    juce::Slider knob;
};

class OneKnobWindow final : public juce::DocumentWindow
{
public:
    OneKnobWindow(LibertyOneKnobManager& manager, int track)
        : juce::DocumentWindow("Liberty - " + manager.getName(track), juce::Colour(0xff11151b),
                               juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setResizable(false, false);
        auto* editor = new OneKnobEditor(manager, track);
        editor->setSize(300, 360);
        setContentOwned(editor, true);
        centreWithSize(300, 360);
        setVisible(true);
    }
    void closeButtonPressed() override { setVisible(false); }
};
}

LibertyOneKnobRack::LibertyOneKnobRack() = default;
LibertyOneKnobRack::~LibertyOneKnobRack() = default;

void LibertyOneKnobRack::prepare(double sr, int maximumBlockSize)
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32)juce::jmax(16, maximumBlockSize), 2 };
    chorus.prepare(spec); flanger.prepare(spec); phaser.prepare(spec);
    delay.prepare(spec); compressor.prepare(spec); filter.prepare(spec); doubler.prepare(spec); toneFilter.prepare(spec); deEsserCompressor.prepare(spec);
    reverb.setSampleRate(sampleRate);
    reset(); updateParameters();
}

void LibertyOneKnobRack::reset()
{
    chorus.reset(); flanger.reset(); phaser.reset(); delay.reset(); compressor.reset(); filter.reset(); doubler.reset(); toneFilter.reset(); deEsserCompressor.reset(); reverb.reset(); tremoloPhase = 0.0;
}

void LibertyOneKnobRack::setType(Type newType)
{
    type = newType; reset(); updateParameters();
}

void LibertyOneKnobRack::setAmount(float newAmount) noexcept
{
    amount = juce::jlimit(0.0f, 1.0f, newAmount);
    updateParameters();
}

juce::String LibertyOneKnobRack::getName() const
{
    switch (type)
    {
        case Type::chorus: return "One Knob Chorus";
        case Type::flanger: return "One Knob Flanger";
        case Type::phaser: return "One Knob Phaser";
        case Type::tremolo: return "One Knob Tremolo";
        case Type::reverb: return "One Knob Reverb";
        case Type::delay: return "One Knob Delay";
        case Type::drive: return "One Knob Drive";
        case Type::compressor: return "One Knob Compressor";
        case Type::saturation: return "One Knob Saturation";
        case Type::stereoWidth: return "One Knob Stereo Width";
        case Type::filter: return "One Knob Filter";
        case Type::doubler: return "One Knob Doubler";
        case Type::exciter: return "One Knob Exciter";
        case Type::deEsser: return "One Knob De-Esser";
        case Type::gate: return "One Knob Gate";
        case Type::bassBoost: return "One Knob Bass Boost";
        case Type::air: return "One Knob Air";
        case Type::punch: return "One Knob Punch";
        case Type::softClip: return "One Knob Soft Clip";
        default: return {};
    }
}

void LibertyOneKnobRack::updateParameters()
{
    const float x = juce::jlimit(0.0f, 1.0f, amount);
    chorus.setRate(0.15f + 1.85f * x);
    chorus.setDepth(0.10f + 0.80f * x);
    chorus.setCentreDelay(7.0f + 8.0f * x);
    chorus.setFeedback(0.02f + 0.18f * x);
    chorus.setMix(0.08f + 0.52f * x);

    flanger.setRate(0.08f + 0.92f * x);
    flanger.setDepth(0.18f + 0.80f * x);
    flanger.setCentreDelay(0.7f + 3.3f * x);
    flanger.setFeedback(0.05f + 0.72f * x);
    flanger.setMix(0.08f + 0.62f * x);

    phaser.setRate(0.08f + 1.25f * x);
    phaser.setDepth(0.18f + 0.80f * x);
    phaser.setCentreFrequency(260.0f + 1500.0f * x);
    phaser.setFeedback(0.03f + 0.68f * x);
    phaser.setMix(0.08f + 0.68f * x);

    juce::dsp::Reverb::Parameters rp;
    rp.roomSize = 0.18f + 0.72f * x;
    rp.damping = 0.72f - 0.40f * x;
    rp.wetLevel = 0.05f + 0.50f * x;
    rp.dryLevel = 1.0f - 0.18f * x;
    rp.width = 0.35f + 0.65f * x;
    reverb.setParameters(rp);

    delay.setDelay((float)(sampleRate * (0.045 + 0.34 * x)));
    compressor.setThreshold(-6.0f - 24.0f * x);
    compressor.setRatio(1.5f + 10.5f * x);
    compressor.setAttack(18.0f - 15.0f * x);
    compressor.setRelease(160.0f - 90.0f * x);

    filter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    filter.setCutoffFrequency(18000.0f - 16500.0f * x);
    filter.setResonance(0.72f + 1.8f * x);

    doubler.setRate(0.12f + 0.28f * x);
    doubler.setDepth(0.10f + 0.35f * x);
    doubler.setCentreDelay(12.0f + 12.0f * x);
    doubler.setFeedback(0.0f);
    doubler.setMix(0.10f + 0.42f * x);
    toneFilter.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    toneFilter.setCutoffFrequency(3500.0f + 4500.0f * x);
    toneFilter.setResonance(0.7f);
    deEsserCompressor.setThreshold(-10.0f - 22.0f * x);
    deEsserCompressor.setRatio(2.0f + 8.0f * x);
    deEsserCompressor.setAttack(1.5f);
    deEsserCompressor.setRelease(45.0f);
}

void LibertyOneKnobRack::process(juce::AudioBuffer<float>& buffer)
{
    if (type == Type::none || buffer.getNumSamples() <= 0) return;
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    switch (type)
    {
        case Type::chorus: chorus.process(context); break;
        case Type::flanger: flanger.process(context); break;
        case Type::phaser: phaser.process(context); break;
        case Type::tremolo:
        {
            const float x = amount;
            const double rate = 1.0 + 8.0 * x;
            const float depth = 0.12f + 0.82f * x;
            for (int s = 0; s < buffer.getNumSamples(); ++s)
            {
                const float lfo = 0.5f + 0.5f * std::sin((float)tremoloPhase);
                const float gain = (1.0f - depth) + depth * lfo;
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch) buffer.setSample(ch, s, buffer.getSample(ch, s) * gain);
                tremoloPhase += juce::MathConstants<double>::twoPi * rate / sampleRate;
                if (tremoloPhase >= juce::MathConstants<double>::twoPi) tremoloPhase -= juce::MathConstants<double>::twoPi;
            }
            break;
        }
        case Type::reverb: reverb.processStereo(buffer.getWritePointer(0), buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : buffer.getWritePointer(0), buffer.getNumSamples()); break;
        case Type::delay:
        {
            const float wet = 0.08f + 0.55f * amount;
            const float feedback = 0.08f + 0.58f * amount;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int s = 0; s < buffer.getNumSamples(); ++s)
                {
                    const float in = buffer.getSample(ch, s);
                    const float d = delay.popSample(ch);
                    delay.pushSample(ch, in + d * feedback);
                    buffer.setSample(ch, s, in * (1.0f - wet * 0.35f) + d * wet);
                }
            break;
        }
        case Type::drive:
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int s = 0; s < buffer.getNumSamples(); ++s)
                {
                    const float driveGain = 1.0f + 14.0f * amount;
                    const float y = std::tanh(buffer.getSample(ch, s) * driveGain) / std::tanh(driveGain);
                    buffer.setSample(ch, s, y);
                }
            break;
        case Type::compressor: compressor.process(context); break;
        case Type::saturation:
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int s = 0; s < buffer.getNumSamples(); ++s)
                {
                    const float x = buffer.getSample(ch, s);
                    const float k = 1.0f + 5.0f * amount;
                    buffer.setSample(ch, s, std::atan(x * k) / std::atan(k));
                }
            break;
        case Type::stereoWidth:
            if (buffer.getNumChannels() >= 2)
                for (int s = 0; s < buffer.getNumSamples(); ++s)
                {
                    const float l = buffer.getSample(0, s), r = buffer.getSample(1, s);
                    const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * (1.0f + 1.6f * amount);
                    buffer.setSample(0, s, mid + side); buffer.setSample(1, s, mid - side);
                }
            break;
        case Type::filter: filter.process(context); break;
        case Type::doubler: doubler.process(context); break;
        case Type::exciter:
            for (int ch=0; ch<buffer.getNumChannels(); ++ch) for (int s=0; s<buffer.getNumSamples(); ++s) {
                const float v=buffer.getSample(ch,s); const float h=std::tanh(v*(2.0f+5.0f*amount));
                buffer.setSample(ch,s, v + (h-v)*(0.08f+0.25f*amount)); } break;
        case Type::deEsser:
        {
            juce::AudioBuffer<float> high(buffer.getNumChannels(), buffer.getNumSamples());
            for(int ch=0;ch<buffer.getNumChannels();++ch) high.copyFrom(ch,0,buffer,ch,0,buffer.getNumSamples());
            juce::dsp::AudioBlock<float> hb(high); juce::dsp::ProcessContextReplacing<float> hc(hb);
            toneFilter.process(hc); deEsserCompressor.process(hc);
            for(int ch=0;ch<buffer.getNumChannels();++ch) buffer.addFrom(ch,0,high,ch,0,buffer.getNumSamples(),-0.55f*amount);
            break;
        }
        case Type::gate:
            for(int ch=0;ch<buffer.getNumChannels();++ch) for(int s=0;s<buffer.getNumSamples();++s) {
                const float v=buffer.getSample(ch,s); const float threshold=0.002f+0.035f*amount;
                if(std::abs(v)<threshold) buffer.setSample(ch,s,v*(1.0f-0.92f*amount)); } break;
        case Type::bassBoost:
            for(int ch=0;ch<buffer.getNumChannels();++ch) for(int s=1;s<buffer.getNumSamples();++s) {
                const float v=buffer.getSample(ch,s); const float prev=buffer.getSample(ch,s-1);
                buffer.setSample(ch,s,v+(prev-v)*(0.08f+0.30f*amount)); } break;
        case Type::air:
            for(int ch=0;ch<buffer.getNumChannels();++ch) for(int s=1;s<buffer.getNumSamples();++s) {
                const float v=buffer.getSample(ch,s), prev=buffer.getSample(ch,s-1);
                buffer.setSample(ch,s,v+(v-prev)*(0.08f+0.35f*amount)); } break;
        case Type::punch:
            for(int ch=0;ch<buffer.getNumChannels();++ch) for(int s=1;s<buffer.getNumSamples();++s) {
                const float v=buffer.getSample(ch,s), prev=buffer.getSample(ch,s-1);
                buffer.setSample(ch,s,juce::jlimit(-1.2f,1.2f,v+(v-prev)*(0.15f+0.65f*amount))); } break;
        case Type::softClip:
            for(int ch=0;ch<buffer.getNumChannels();++ch) for(int s=0;s<buffer.getNumSamples();++s) {
                const float gain=1.0f+6.0f*amount; buffer.setSample(ch,s,std::tanh(buffer.getSample(ch,s)*gain)/std::tanh(gain)); } break;
        default: break;
    }
}

LibertyOneKnobManager& LibertyOneKnobManager::instance(){static LibertyOneKnobManager manager;return manager;}
LibertyOneKnobManager::LibertyOneKnobManager()=default;
void LibertyOneKnobManager::ensureTrack(int i){while((int)racks.size()<=i){auto r=std::make_unique<LibertyOneKnobRack>();r->prepare(preparedSampleRate,preparedBlockSize);racks.push_back(std::move(r));baselines.push_back(std::make_unique<juce::AudioBuffer<float>>());workBuffers.push_back(std::make_unique<juce::AudioBuffer<float>>());editors.push_back(nullptr);}}
void LibertyOneKnobManager::prepare(double sr,int bs){const juce::ScopedLock s(lock);preparedSampleRate=sr>0?sr:48000.0;preparedBlockSize=juce::jmax(16,bs);for(auto&r:racks)if(r)r->prepare(preparedSampleRate,preparedBlockSize);}
void LibertyOneKnobManager::setEffect(int i,LibertyOneKnobRack::Type t){if(!validTrack(i))return;const juce::ScopedLock s(lock);ensureTrack(i);racks[(size_t)i]->setType(t);if(editors[(size_t)i])editors[(size_t)i].reset();}
void LibertyOneKnobManager::clearEffect(int i){setEffect(i,LibertyOneKnobRack::Type::none);}
LibertyOneKnobRack::Type LibertyOneKnobManager::getEffect(int i)const{if(!validTrack(i))return LibertyOneKnobRack::Type::none;const juce::ScopedLock s(lock);return i<(int)racks.size()&&racks[(size_t)i]?racks[(size_t)i]->getType():LibertyOneKnobRack::Type::none;}
void LibertyOneKnobManager::setAmount(int i,float a){if(!validTrack(i))return;const juce::ScopedLock s(lock);ensureTrack(i);racks[(size_t)i]->setAmount(a);}
float LibertyOneKnobManager::getAmount(int i)const{const juce::ScopedLock s(lock);return i>=0&&i<(int)racks.size()&&racks[(size_t)i]?racks[(size_t)i]->getAmount():0;}
juce::String LibertyOneKnobManager::getName(int i)const{const juce::ScopedLock s(lock);return i>=0&&i<(int)racks.size()&&racks[(size_t)i]?racks[(size_t)i]->getName():juce::String{};}
bool LibertyOneKnobManager::hasEffect(int i)const{return getEffect(i)!=LibertyOneKnobRack::Type::none;}
void LibertyOneKnobManager::process(int i,juce::AudioBuffer<float>&b){if(!validTrack(i)||!lock.tryEnter())return;if(i<(int)racks.size()&&racks[(size_t)i])racks[(size_t)i]->process(b);lock.exit();}
void LibertyOneKnobManager::beginAudioTrackBlock(int i,float*const*d,int ch,int n){if(!validTrack(i)||!hasEffect(i)||n<=0||!lock.tryEnter())return;ensureTrack(i);int cc=juce::jlimit(1,2,ch);auto&b=*baselines[(size_t)i];b.setSize(cc,n,false,false,true);for(int c=0;c<cc;++c)d[c]?b.copyFrom(c,0,d[c],n):b.clear(c,0,n);lock.exit();}
void LibertyOneKnobManager::endAudioTrackBlock(int i,float*const*d,int ch,int n){if(!validTrack(i)||!hasEffect(i)||n<=0||!lock.tryEnter())return;ensureTrack(i);int cc=juce::jlimit(1,2,ch);auto&b=*baselines[(size_t)i];auto&w=*workBuffers[(size_t)i];if(b.getNumSamples()!=n||b.getNumChannels()<cc){lock.exit();return;}w.setSize(cc,n,false,false,true);w.clear();for(int c=0;c<cc;++c)if(d[c]){w.copyFrom(c,0,d[c],n);w.addFrom(c,0,b,c,0,n,-1);}racks[(size_t)i]->process(w);for(int c=0;c<cc;++c)if(d[c]){juce::FloatVectorOperations::copy(d[c],b.getReadPointer(c),n);juce::FloatVectorOperations::add(d[c],w.getReadPointer(c),n);}lock.exit();}
void LibertyOneKnobManager::processInstrumentBlock(float*const*d,int ch,int n){processInstrumentBlock(0,d,ch,n);}
void LibertyOneKnobManager::processInstrumentBlock(int instrumentTrack,float*const*d,int ch,int n){const int slot=100000+instrumentTrack;if(!hasEffect(slot)||n<=0||ch<=0||!lock.tryEnter())return;ensureTrack(slot);int cc=juce::jlimit(1,2,ch);auto&w=*workBuffers[(size_t)slot];w.setSize(cc,n,false,false,true);for(int c=0;c<cc;++c)if(d[c])w.copyFrom(c,0,d[c],n);racks[(size_t)slot]->process(w);for(int c=0;c<cc;++c)if(d[c])juce::FloatVectorOperations::copy(d[c],w.getReadPointer(c),n);lock.exit();}
void LibertyOneKnobManager::showEditor(int i){if(!validTrack(i)||!hasEffect(i))return;const juce::ScopedLock s(lock);ensureTrack(i);auto&w=editors[(size_t)i];if(!w)w=std::make_unique<OneKnobWindow>(*this,i);else{w->setVisible(true);w->toFront(true);}}
