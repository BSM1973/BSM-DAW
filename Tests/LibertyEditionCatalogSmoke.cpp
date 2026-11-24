#include "LibertyDAWEditions.h"
#include <cassert>
#include <iostream>
int main()
{
    using namespace LibertyEditions;
    assert(editionName(Edition::Base)=="Liberty DAW");
    assert(editionName(Edition::Standard)=="Liberty DAW Standard");
    assert(editionName(Edition::Ultimate)=="Liberty DAW Ultimate");
    int count=0,synth=0,effects=0;
    for(std::size_t i=0;i<catalogue.size();++i)
    {
        const auto& d=catalogue[i];
        assert(d.id.substr(0,15)=="liberty.native.");
        assert(!d.name.empty());
        assert(findDevice(d.id)==&d);
        assert(!belongsToPlan(Edition::Base,d));
        assert(belongsToPlan(Edition::Ultimate,d));++count;
        if(belongsToPlan(Edition::Standard,d))
        {
            assert(!d.sourceRepository.empty());
            if(d.kind==Kind::Instrument)++synth;
            else ++effects;
        }
        for(std::size_t j=i+1;j<catalogue.size();++j)
            assert(catalogue[i].id!=catalogue[j].id);
    }
    assert(findDevice("liberty.native.nonexistent")==nullptr);
    assert(synth==2&&effects==3&&count==21);
    std::cout<<"PASS edition plans: BASE 0, STANDARD 2+3, ULTIMATE 21; native factories remain mandatory"<<std::endl;
}
