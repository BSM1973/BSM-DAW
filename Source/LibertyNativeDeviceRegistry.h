#pragma once
// Les appareils natifs sont des classes AudioProcessor construites directement
// dans Liberty. Ici : aucune analyse ni ouverture de fichiers AU/VST3.
#include "LibertyDAWEditions.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <memory>
#include <string_view>
namespace LibertyEditions
{
using NativeCreator = std::unique_ptr<juce::AudioProcessor>(*)();
class NativeDeviceRegistry final
{
public:
    // Enregistrement au demarrage sur le thread UI, jamais dans l'audio callback.
    bool registerFactory(std::string_view id,NativeCreator creator) noexcept
    {
        if(!creator)return false;
        for(std::size_t i=0;i<catalogue.size();++i)
            if(catalogue[i].id==id)
            {
                creators[i]=creator;
                return true;
            }
        return false;
    }
    bool isAvailable(Edition edition,std::string_view id) const noexcept
    {
        for(std::size_t i=0;i<catalogue.size();++i)
            if(catalogue[i].id==id)
                return belongsToPlan(edition,catalogue[i])&&creators[i]!=nullptr;
        return false;
    }
    // Instancier seulement hors thread audio. Le proprietaire prepare,
    // route MIDI/audio, edite l'UI et sauvegarde l'etat du processeur.
    std::unique_ptr<juce::AudioProcessor> create(Edition edition,
                                                  std::string_view id) const
    {
        for(std::size_t i=0;i<catalogue.size();++i)
            if(catalogue[i].id==id)
                return belongsToPlan(edition,catalogue[i])&&creators[i]
                    ? creators[i]() : nullptr;
        return nullptr;
    }
private:
    std::array<NativeCreator,catalogue.size()> creators{};
};
}
