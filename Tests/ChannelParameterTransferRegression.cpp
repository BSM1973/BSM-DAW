#include "../Source/ChannelParameterTransfer.h"
#include <array>
#include <cassert>
#include <iostream>

struct Settings
{
    float inputGainDb=0,highPassHz=0,gateThresholdDb=0;
    float compressorThresholdDb=0,compressorRatio=0;
    float eqLowDb=0,eqMidDb=0,eqHighDb=0;
    bool gateEnabled=false,compressorEnabled=false,eqEnabled=false;
};

using Values=std::array<double,11>;
Values values(const Settings& s)
{
    return {{s.inputGainDb,s.highPassHz,s.gateEnabled?1.0:0.0,
             s.gateThresholdDb,s.compressorEnabled?1.0:0.0,
             s.compressorThresholdDb,s.compressorRatio,s.eqEnabled?1.0:0.0,
             s.eqLowDb,s.eqMidDb,s.eqHighDb}};
}

int main()
{
    Settings a,b;
    a.inputGainDb=1.5f;a.highPassHz=70.f;
    a.gateEnabled=true;a.gateThresholdDb=-40.f;
    a.compressorEnabled=true;a.compressorThresholdDb=-15.f;
    a.compressorRatio=6.f;a.eqEnabled=true;
    a.eqLowDb=-3.f;a.eqMidDb=5.f;a.eqHighDb=9.f;
    const auto source=values(a),initial=values(b);
    struct Case{int section,row,field;};
    constexpr Case cases[]={{0,0,0},{0,1,1},{1,0,2},{1,1,3},
                            {2,0,4},{2,1,5},{2,2,6},
                            {3,0,7},{3,1,8},{3,2,9},{3,3,10}};
    for(const auto c:cases)
    {
        b=Settings{};
        assert(LibertyChannelParameterTransfer::copy(b,a,c.section,c.row));
        const auto after=values(b);
        for(std::size_t i=0;i<after.size();++i)
            assert(after[i]==(static_cast<int>(i)==c.field?source[i]:initial[i]));
    }
    // Exercise the inverse direction as well: only the chosen field can reset.
    for(const auto c:cases)
    {
        Settings destination=a;
        const Settings sourceB{};
        assert(LibertyChannelParameterTransfer::copy(
            destination,sourceB,c.section,c.row));
        const auto after=values(destination);
        for(std::size_t i=0;i<after.size();++i)
            assert(after[i]==(static_cast<int>(i)==c.field?initial[i]:source[i]));
        assert(values(sourceB)==initial);
    }
    for(const auto invalid:std::array<std::array<int,2>,8>{{
        {{-1,0}},{{4,0}},{{0,-1}},{{0,2}},
        {{1,2}},{{2,3}},{{3,4}},{{3,999}}}})
    {
        b=Settings{};
        assert(!LibertyChannelParameterTransfer::copy(b,a,invalid[0],invalid[1]));
        assert(values(b)==initial);
    }
    std::cout<<"CANAL A/B single-parameter copy: 11 rows, invalid indexes, isolation OK\n";
}
