#pragma once
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

// Original Liberty factory MIDI grooves. 10 musical styles x 10 variations.
// Each preset has its own note distribution; nothing is downloaded or licensed.
namespace LibertyDrumPatterns
{
constexpr int styleCount=10, variationsPerStyle=10, instrumentCount=16, steps=16;
using Grid=std::array<std::uint8_t,instrumentCount*steps>;
inline constexpr const char* styleNames[styleCount]={
    "HIP-HOP","ACOUSTIQUE","ROCK","MÉTAL","ÉLECTRO",
    "REGGAE","POP","ORIENTAL","FUNK","TRAP"
};
inline constexpr const char* variationNames[styleCount][variationsPerStyle]={
    {"Boom Bap","Old School","Half Time","Breaks 90","Lo-Fi","Head Nod","East Coast","West Coast","Jazz Dust","Turnaround"},
    {"Ballade","Folk Drive","Demi-temps","Brosses","Intimiste","Train Beat","Session","Country","Percussions","Fill acoustique"},
    {"Classic Beat","Stadium","Half Rock","Garage","Ballade Rock","Hard Drive","Break Rock","Indie","Tom Groove","Rock Fill"},
    {"Heavy Groove","Double Kick","Half Metal","Gallop","Doom Pulse","Thrash","Breakdown","Speed Metal","Tom Attack","Double Fill"},
    {"Four On Floor","Tech House","Half Electro","Breakbeat","Minimal Tech","Club Drive","Synth Funk","Progressive","Machine Perc","Build Up"},
    {"One Drop","Rockers","Dub Half","Steppers","Roots","Ska Push","Reggae Break","Dancehall","Nyabinghi","Dub Fill"},
    {"Radio Pop","Dance Pop","Half Pop","Synth Pop","Soft Pop","Upbeat","Pop Break","Disco Pop","Clap Groove","Pop Fill"},
    {"Maqsum","Baladi","Half Oriental","Saidi","Malfuf","Ayoub","Fallahi","Wahda","Darbuka Perc","Riq Fill"},
    {"Pocket Funk","New Orleans","Half Funk","Syncopated","Dry Funk","Sixteenth Funk","Break Funk","P-Funk","Perc Funk","Fill Funk"},
    {"808 Classic","Trap Bounce","Half Trap","Drill","Sparse 808","Trap Drive","Rolling Hats","Dark Trap","808 Perc","Trap Fill"}
};
constexpr std::uint16_t beat(std::initializer_list<int> positions) noexcept
{
    std::uint16_t mask=0;
    for(const auto step:positions)if(step>=0&&step<steps)mask|=(std::uint16_t)(1u<<step);
    return mask;
}
struct Style
{
    std::uint16_t kick,snare,closedHat,openHat,clap,percussion;
    std::uint8_t percussionPad;
};
inline const std::array<Style,styleCount>& styles() noexcept
{
    static const std::array<Style,styleCount> library={{
        {beat({0,7,10}),beat({4,12}),beat({0,2,4,6,8,10,12,14}),beat({15}),beat({12}),beat({3,11}),9},
        {beat({0,8}),beat({4,12}),beat({0,2,4,6,8,10,12,14}),beat({}),beat({}),beat({7,15}),10},
        {beat({0,8,10}),beat({4,12}),beat({0,2,4,6,8,10,12,14}),beat({15}),beat({}),beat({0}),12},
        {beat({0,2,4,6,8,10,12,14}),beat({4,12}),beat({0,2,4,6,8,10,12,14}),beat({}),beat({}),beat({0,8}),12},
        {beat({0,4,8,12}),beat({}),beat({2,6,10,14}),beat({15}),beat({4,12}),beat({3,11}),11},
        {beat({8}),beat({8}),beat({2,6,10,14}),beat({}),beat({}),beat({4,12}),8},
        {beat({0,8,10}),beat({4,12}),beat({0,2,4,6,8,10,12,14}),beat({15}),beat({4,12}),beat({3,11}),9},
        {beat({0,5,8,11}),beat({3,7,12,15}),beat({2,6,10,14}),beat({}),beat({}),beat({1,4,9,13}),10},
        {beat({0,3,8,11}),beat({4,12,15}),beat({0,1,2,3,4,6,8,9,10,11,12,14}),beat({7}),beat({}),beat({2,6,10,14}),8},
        {beat({0,6,11}),beat({8}),beat({0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}),beat({7,15}),beat({8}),beat({3,14}),15}
    }};
    return library;
}
inline Grid make(int style,int variation) noexcept
{
    Grid result{};
    if(style<0||style>=styleCount||variation<0||variation>=variationsPerStyle)return result;
    const auto& base=styles()[(std::size_t)style];
    auto kick=base.kick,snare=base.snare,hat=base.closedHat;
    auto open=base.openHat,clap=base.clap,percussion=base.percussion;
    // Every variation is structurally distinct, not a velocity-only copy.
    switch(variation)
    {
        case 0:break;
        case 1:kick|=beat({3,10,14});hat|=beat({7,15});break;
        case 2:kick=beat({0,10})|(kick&beat({6,14}));snare=beat({8});clap=beat({8});hat&=beat({0,2,4,6,8,10,12,14});break;
        case 3:kick|=beat({3,7,11,15});snare|=beat({14});hat|=beat({1,5,9,13});break;
        case 4:kick=kick&beat({0,4,8,12});if(!kick)kick=beat({0});snare&=beat({4,8,12});hat=beat({2,6,10,14});percussion=0;break;
        case 5:kick|=beat({0,4,8,12});hat|=beat({1,3,5,7,9,11,13,15});break;
        case 6:kick|=beat({6,14});snare|=beat({7,15});hat^=beat({1,3,9,11});break;
        case 7:kick|=beat({5,13});snare|=beat({3,11});hat|=beat({3,7,11,15});break;
        case 8:percussion|=beat({1,3,6,9,11,14});kick|=beat({10});open|=beat({7});break;
        case 9:percussion|=beat({12,13,14,15});kick|=beat({14});snare|=beat({13,15});hat|=beat({15});break;
    }
    // Genre-aware events: specialised cymbals, rim/stick and 808 low kicks.
    if(style==3&&variation==1)kick=beat({0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15});
    if(style==5){percussion|=beat({4,12});if(variation==5)kick|=beat({0,4,12});if(variation==4)hat=beat({6,14});}
    if(style==7){percussion|=beat({0,5,10,15});if(variation==8)snare|=beat({2,6,10,14});}
    if(style==8 && (variation==3||variation==7))kick|=beat({2,9,15});
    if(style==9 && (variation==1||variation==6))hat|=beat({0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15});
    auto put=[&](int pad,std::uint16_t mask,int velocity)
    {
        for(int step=0;step<steps;++step)if(mask&(1u<<step))
        {
            const int pulse=(step%4==0?10:step%2==0?3:-12);
            const int variationAccent=((style*7+variation*11+step*5+pad*3)%13)-6;
            const int v=std::clamp(velocity+pulse+variationAccent,30,127);
            result[(std::size_t)(pad*steps+step)]=(std::uint8_t)v;
        }
    };
    put(0,kick,108);put(1,snare,106);put(3,hat,75);put(4,open,86);
    put(2,clap,98);put(base.percussionPad,percussion,81);
    if(style==3&&variation==9)put(15,beat({12,13,14,15}),117); // double kick fill
    if(style==7){put(11,beat({4,12}),75);put(9,beat({2,6,10,14}),62);}
    if(style==9)put(15,kick,106); // sustained 808 low-end articulation
    if(variation==9){put(5,beat({12,14}),90);put(6,beat({13}),93);put(7,beat({15}),103);}
    return result;
}
inline const char* name(int style,int variation) noexcept
{
    return style>=0&&style<styleCount&&variation>=0&&variation<variationsPerStyle?
        variationNames[style][variation]:"";
}
inline const char* styleName(int style) noexcept
{
    return style>=0&&style<styleCount?styleNames[style]:"";
}
}
