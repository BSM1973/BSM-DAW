#pragma once
#include "ChannelProjectState.h"
#include <algorithm>
#include <memory>
#include <vector>

// Local, named CANAL presets shared by every project, independent of DSP.
namespace LibertyChannelUserPresets
{
using Settings = AudioEngine::ChannelProcessingSettings;
struct Preset
{
    juce::String name;
    Settings settings;
    bool favorite = false;
};
class Library final
{
public:
    static constexpr int maxPresets = 24;
    static bool validName(juce::String name)
    {
        name = name.trim();
        return name.isNotEmpty() && name.length() <= 48
            && !name.containsAnyOf("\r\n");
    }
    // JUCE equalsIgnoreCase does not reliably fold accented uppercase letters
    // on every platform. Normalise French/Latin uppercase characters before
    // comparing user-preset names, while preserving the name shown in the UI.
    static juce::String comparisonKey(juce::String name)
    {
        return name.trim()
            .replaceCharacters(
                juce::String::fromUTF8("ÀÂÄÇÉÈÊËÎÏÔÖÙÛÜŸÆŒ"),
                juce::String::fromUTF8("àâäçéèêëîïôöùûüÿæœ"))
            .toLowerCase();
    }
    const std::vector<Preset>& items() const noexcept { return presets; }
    // Preserve the status when a preset is renamed or overwritten; callers
    // explicitly choose whether to add or remove a favorite.
    bool setFavorite(const juce::String& name,bool isFavorite)
    {
        const auto key=comparisonKey(name);
        for(auto& preset:presets)
            if(comparisonKey(preset.name)==key)
            {
                preset.favorite=isFavorite;
                return true;
            }
        return false;
    }

    bool upsert(juce::String name, const Settings& settings)
    {
        name = name.trim();
        if (!validName(name)) return false;
        const auto key=comparisonKey(name);
        for (auto& preset : presets)
            if (comparisonKey(preset.name)==key)
            {
                preset.name=name;
                preset.settings=settings;
                return true;
            }
        if ((int)presets.size() >= maxPresets) return false;
        presets.push_back({name,settings});
        return true;
    }
    // Rename preserves the original DSP settings and list order. Never
    // overwrite a different preset, even when its name differs only by case.
    bool rename(const juce::String& currentName,juce::String replacement)
    {
        replacement=replacement.trim();
        if(!validName(replacement))return false;
        const auto fromKey=comparisonKey(currentName);
        const auto toKey=comparisonKey(replacement);
        auto it=std::find_if(presets.begin(),presets.end(),
            [&](const Preset& p){return comparisonKey(p.name)==fromKey;});
        if(it==presets.end())return false;
        for(const auto& item:presets)
            if(&item!=&*it && comparisonKey(item.name)==toKey)return false;
        it->name=replacement;
        return true;
    }
    // Duplication creates a distinct entry and must not alter the original
    // preset or silently replace an existing named entry.
    bool duplicate(const juce::String& sourceName,juce::String newName)
    {
        newName=newName.trim();
        if(!validName(newName) || (int)presets.size()>=maxPresets)return false;
        const auto sourceKey=comparisonKey(sourceName);
        const auto newKey=comparisonKey(newName);
        const auto it=std::find_if(presets.begin(),presets.end(),
            [&](const Preset& p){return comparisonKey(p.name)==sourceKey;});
        if(it==presets.end())return false;
        for(const auto& item:presets)
            if(comparisonKey(item.name)==newKey)return false;
        // Copy settings before push_back to avoid invalidating the iterator.
        const auto settings=it->settings;
        presets.push_back({newName,settings});
        return true;
    }
    // Merge all imported items atomically. Name collisions create new entries
    // with a distinct French suffix, leaving existing values untouched.
    // Reject a full import if 24 slots would be exceeded (no partial import).
    bool mergeFrom(const Library& incoming)
    {
        if ((int)presets.size()+(int)incoming.presets.size()>maxPresets)
            return false;
        Library merged=*this;
        for (const auto& imported:incoming.presets)
        {
            auto candidate=imported.name;
            const auto collides=[&](const juce::String& name) {
                const auto key=comparisonKey(name);
                return std::any_of(merged.presets.begin(),merged.presets.end(),
                    [&](const Preset& current) {
                        return comparisonKey(current.name)==key;
                    });
            };
            if (collides(candidate))
            {
                for(int number=1;number<=maxPresets+1;++number)
                {
                    const auto suffix=juce::String::fromUTF8(" (importé")
                        +(number>1?" "+juce::String(number):juce::String{})+")";
                    candidate=imported.name.substring(0,48-suffix.length())
                        .trimEnd()+suffix;
                    if(!collides(candidate))break;
                }
            }
            if (!validName(candidate) || collides(candidate))
                return false;
            merged.presets.push_back({candidate,imported.settings,imported.favorite});
        }
        presets.swap(merged.presets);
        return true;
    }
    bool erase(const juce::String& name)
    {
        const auto oldSize=presets.size();
        const auto key=comparisonKey(name);
        presets.erase(std::remove_if(presets.begin(),presets.end(),
            [&](const Preset& p){return comparisonKey(p.name)==key;}),presets.end());
        return oldSize != presets.size();
    }
    std::unique_ptr<juce::XmlElement> toXml() const
    {
        auto root=std::make_unique<juce::XmlElement>("LibertyChannelUserPresets");
        root->setAttribute("version",1);
        for (const auto& p : presets)
        {
            auto* entry=root->createNewChildElement("Preset");
            entry->setAttribute("name",p.name);
            entry->setAttribute("favorite",p.favorite);
            LibertyChannelProjectState::write(*entry,p.settings);
        }
        return root;
    }
    bool fromXml(const juce::XmlElement& root)
    {
        if (!root.hasTagName("LibertyChannelUserPresets")) return false;
        Library loaded;
        for (const auto* element : root.getChildIterator())
        {
            if (!element->hasTagName("Preset")) continue;
            const auto name=element->getStringAttribute("name");
            if (!loaded.upsert(name,LibertyChannelProjectState::read(*element))
                || !loaded.setFavorite(name,element->getBoolAttribute("favorite",false)))
                return false;
        }
        presets.swap(loaded.presets);
        return true;
    }
    static juce::File defaultFile()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Liberty").getChildFile("CANAL-Presets.xml");
    }
    bool load(const juce::File& file)
    {
        if (!file.existsAsFile())
        {
            presets.clear();
            return true;
        }
        auto xml=juce::parseXML(file);
        return xml != nullptr && fromXml(*xml);
    }
    bool save(const juce::File& file) const
    {
        if (!file.getParentDirectory().createDirectory()) return false;
        juce::TemporaryFile temp(file);
        const auto xml=toXml();
        return temp.getFile().replaceWithText(xml->toString())
            && temp.overwriteTargetFileWithTemporary();
    }
private:
    std::vector<Preset> presets;
};
}
