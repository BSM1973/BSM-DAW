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
            if (!loaded.upsert(name,LibertyChannelProjectState::read(*element)))
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
