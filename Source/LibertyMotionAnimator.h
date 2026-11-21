#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "LibertyMotionMath.h"
#include <vector>

// Message-thread-only, bounded animations. Only repaint a control while
// its visual state is changing. No audio callbacks or persistent global map.
namespace LibertyMotion
{
class Animator final : private juce::Timer
{
    struct Entry
    {
        juce::Component::SafePointer<juce::Component> component;
        float target=0.f;
    };
    juce::Identifier property;
    std::vector<Entry> entries;
    double lastMs=0.0;
public:
    explicit Animator(const char* key):property(key){entries.reserve(32);}
    ~Animator() override {stopTimer();}
    float request(juce::Component& c,float target)
    {
        target=juce::jlimit(0.f,1.f,target);
        auto& props=c.getProperties();
        const float current=(float)(double)props.getWithDefault(property,0.0);
        if(!animationsEnabled() || !c.isShowing())
        {
            props.set(property,(double)target);
            return target;
        }
        for(auto& entry:entries)
            if(entry.component.getComponent()==&c)
            {
                entry.target=target;
                return current;
            }
        if(std::abs(current-target)<0.012f)return target;
        if(entries.size()>=32)
        {
            props.set(property,(double)target);
            return target;
        }
        entries.push_back({juce::Component::SafePointer<juce::Component>(&c),target});
        if(!isTimerRunning())
        {
            lastMs=juce::Time::getMillisecondCounterHiRes();
            startTimerHz(60);
        }
        return current;
    }
private:
    void timerCallback() override
    {
        const double now=juce::Time::getMillisecondCounterHiRes();
        const float dt=(float)((now-lastMs)/1000.0);
        lastMs=now;
        for(auto it=entries.begin();it!=entries.end();)
        {
            auto* c=it->component.getComponent();
            if(c==nullptr||!c->isShowing())
            {
                if(c!=nullptr)c->getProperties().set(property,(double)it->target);
                it=entries.erase(it);
                continue;
            }
            auto& props=c->getProperties();
            const float old=(float)(double)props.getWithDefault(property,0.0);
            const float next=animationsEnabled()?advance(old,it->target,dt):it->target;
            props.set(property,(double)next);
            c->repaint();
            if(next==it->target)it=entries.erase(it);else ++it;
        }
        if(entries.empty())stopTimer();
    }
};
}
