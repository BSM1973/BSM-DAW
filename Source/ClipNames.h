#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <set>
#include <limits>

// Project-local identities outlive deleted clips: serials are never recycled.
class LibertyClipNames {
public:
    struct Entry {juce::String lane,manual;juce::int64 number=0;};
    std::map<juce::String,Entry> entries;
    std::map<juce::String,juce::int64> counters;
    void ensure(const juce::String& id,const juce::String& lane){
        auto found=entries.find(id);
        if(found!=entries.end()&&found->second.lane==lane)return;
        auto& next=counters[lane];if(next>=std::numeric_limits<juce::int64>::max()-1)return;
        entries[id]={lane,{},++next};
    }
    juce::String display(const juce::String& id,const juce::String& track)const {
        const auto it=entries.find(id);if(it==entries.end())return track;
        const auto& e=it->second;if(e.manual.isNotEmpty())return e.manual;
        const auto count=counters.find(e.lane);
        return count!=counters.end()&&count->second>1?track+" "+juce::String(e.number).paddedLeft('0',2):track;
    }
    bool rename(const juce::String& id,juce::String name){
        auto it=entries.find(id);name=name.trim().substring(0,128);
        if(it==entries.end()||name.isEmpty())return false;
        it->second.manual=name;return true;
    }
    void save(juce::XmlElement& root)const{
        auto* all=root.createNewChildElement("ClipNames");
        for(const auto& pair:counters){auto* e=all->createNewChildElement("Counter");e->setAttribute("lane",pair.first);e->setAttribute("last",juce::String(pair.second));}
        for(const auto& pair:entries){auto* e=all->createNewChildElement("Name");e->setAttribute("id",pair.first);e->setAttribute("lane",pair.second.lane);e->setAttribute("number",juce::String(pair.second.number));e->setAttribute("manual",pair.second.manual);}
    }
    void load(const juce::XmlElement& root){
        entries.clear();counters.clear();const auto* all=root.getChildByName("ClipNames");if(!all)return;
        int visited=0;for(const auto* e:all->getChildIterator()){
            if(++visited>1000000)break;
            const auto lane=e->getStringAttribute("lane");if(lane.isEmpty())continue;
            if(e->hasTagName("Counter")){const auto n=e->getStringAttribute("last").getLargeIntValue();if(n>=0&&n<std::numeric_limits<juce::int64>::max()-1)counters[lane]=juce::jmax(counters[lane],n);}
        }
        std::map<juce::String,std::set<juce::int64>> used;
        visited=0;for(const auto* e:all->getChildIterator()){
            if(++visited>1000000)break;if(!e->hasTagName("Name"))continue;
            const auto id=e->getStringAttribute("id"),lane=e->getStringAttribute("lane");auto n=e->getStringAttribute("number").getLargeIntValue();
            if(id.isEmpty()||lane.isEmpty()||entries.count(id))continue;
            if(n<=0||n>=std::numeric_limits<juce::int64>::max()-1||used[lane].count(n)){ensure(id,lane);n=entries[id].number;}
            counters[lane]=juce::jmax(counters[lane],n);used[lane].insert(n);
            entries[id]={lane,e->getStringAttribute("manual").trim().substring(0,128),n};
        }
    }
};
