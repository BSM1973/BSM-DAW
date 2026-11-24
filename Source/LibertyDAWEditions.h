#pragma once
// Catalogue planifie des editions Liberty. Un candidat n'est pas un moteur
// disponible : seule une vraie fabrique C++ liee au binaire l'autorise.
#include <array>
#include <cstddef>
#include <string_view>
namespace LibertyEditions
{
enum class Edition { Base, Standard, Ultimate };
enum class Kind { Instrument, Effect };
struct Device
{
    std::string_view id;
    std::string_view name;
    Kind kind;
    bool candidateForStandard;
    std::string_view sourceRepository;
};
// Boutique Shopify : 21 plugins actifs + le produit DAW, au 24/11/2026.
// Chaque ID est propre au DAW : jamais un identifiant AU/VST3 ni un fichier.
inline constexpr std::array<Device,21> catalogue{{
    {"liberty.native.boom", "BOOM", Kind::Effect, false, "BSM1973/Liberty-BOOM"},
    {"liberty.native.echo86", "ECHO 86", Kind::Instrument, true, "BSM1973/Liberty-ECHO-86"},
    {"liberty.native.epcalm", "EPCalm", Kind::Instrument, false, ""},
    {"liberty.native.equilibrium-grain", "Equilibrium Grain", Kind::Instrument, false, ""},
    {"liberty.native.evasion-convoverb", "Evasion ConvoVerb", Kind::Effect, false, ""},
    {"liberty.native.fm-emancipation", "FM Emancipation", Kind::Instrument, true, "BSM1973/FM-Emancipation"},
    {"liberty.native.force-multi-compressor", "FORCE MULTI COMPRESSOR", Kind::Effect, false, ""},
    {"liberty.native.freedom", "FREEDOM", Kind::Instrument, false, ""},
    {"liberty.native.freespirit-delay", "FreeSpirit Delay", Kind::Effect, true, "BSM1973/Liberty-FreeSpirit-Delay"},
    {"liberty.native.guitar-autonomy", "Guitar Autonomy", Kind::Effect, false, ""},
    {"liberty.native.guitar-biosphere", "GUITAR BIOSPHERE", Kind::Effect, false, "BSM1973/Liberty-Guitar-Biosphere"},
    {"liberty.native.harmonyvoices", "HarmonyVoices", Kind::Effect, false, "BSM1973/HARMONYVOICES"},
    {"liberty.native.horizon-reverb", "Horizon Reverb", Kind::Effect, false, ""},
    {"liberty.native.love-eq", "LOVE EQ", Kind::Effect, true, "BSM1973/Liberty-LOVE-EQ"},
    {"liberty.native.openmind-chorus", "Openmind Chorus", Kind::Effect, false, ""},
    {"liberty.native.organix", "ORGANIX", Kind::Effect, false, "BSM1973/Liberty-Organix"},
    {"liberty.native.pads-independence", "PADS Independence", Kind::Instrument, false, ""},
    {"liberty.native.pedal-legends", "PEDAL LEGENDS", Kind::Effect, true, "BSM1973/Liberty-Pedal-Legends"},
    {"liberty.native.serenity-wavetable", "Serenity Wavetable", Kind::Instrument, false, "BSM1973/Liberty-Serenity-Wavetable"},
    {"liberty.native.unchained-drive", "Unchained Drive", Kind::Effect, false, ""},
    {"liberty.native.voices-audacity", "VOICES AUDACITY", Kind::Effect, false, ""}
}};
constexpr const Device* findDevice(std::string_view id) noexcept
{
    for(const auto& d:catalogue)if(d.id==id)return &d;
    return nullptr;
}
// Appartenance au PLAN, sans presumer du DSP natif existant.
constexpr bool belongsToPlan(Edition edition,const Device& d) noexcept
{
    return edition==Edition::Ultimate ||
        (edition==Edition::Standard&&d.candidateForStandard);
}
constexpr int plannedCount(Edition edition,Kind kind) noexcept
{
    int count=0;
    for(const auto& d:catalogue)
        if(d.kind==kind&&belongsToPlan(edition,d))++count;
    return count;
}
constexpr std::string_view editionName(Edition edition) noexcept
{
    switch(edition)
    {
        case Edition::Base:return "Liberty DAW";
        case Edition::Standard:return "Liberty DAW Standard";
        case Edition::Ultimate:return "Liberty DAW Ultimate";
    }
    return {};
}
static_assert(catalogue.size()==21,"Complete Shopify plugin catalogue");
static_assert(plannedCount(Edition::Base,Kind::Instrument)==0);
static_assert(plannedCount(Edition::Base,Kind::Effect)==0);
static_assert(plannedCount(Edition::Standard,Kind::Instrument)==2);
static_assert(plannedCount(Edition::Standard,Kind::Effect)==3);
static_assert(plannedCount(Edition::Ultimate,Kind::Instrument)==7);
static_assert(plannedCount(Edition::Ultimate,Kind::Effect)==14);
}
