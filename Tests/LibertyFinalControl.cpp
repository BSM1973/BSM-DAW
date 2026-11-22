#include "LibertyMasteringMeter.h"
#include "LibertyReferencePdf.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
int main(int argc,char** argv)
{
    using namespace LibertyMastering;
    using namespace LibertyImmersive;
    constexpr double p=3.14159265358979323846;
    // Steady tones corresponding to the documented phase/frequency tests.
    // Regenerated signals, NOT the original EBU audio archive.
    for(const auto sr:{44100.,48000.,96000.,192000.})
        for(const auto shape:std::array<std::array<double,2>,4>{{{{.25,0.}},{{.25,p/4}},{{1./6.,p/3}},{{.125,3*p/8}}}})
        {
            TruePeak meter;const auto count=(int)(sr*.12),fade=(int)(sr*.02);
            for(int i=0;i<count;++i){const double ramp=std::clamp(double(std::min(i,count-1-i))/fade,0.,1.);const float s=float(std::pow(10.,-6./20.)*std::sin(2*p*shape[0]*i+shape[1])*(.5-.5*std::cos(p*ramp)));meter.push({s,-s});}
            for(int i=0;i<32;++i)meter.push({});
            assert(meter.dbtp()>=-6.4&&meter.dbtp()<=-5.8);
            std::cout<<"synthetic TP "<<sr<<" Hz "<<shape[0]<<" : "<<meter.dbtp()<<" dBTP\n";
        }
    // 60 seconds continuous 48 kHz stereo processing, uneven transfer sizes.
    auto meter=std::make_unique<Meter>();meter->configure(48000.);
    auto tap=std::make_unique<Tap>();tap->setEnabled(true);
    std::array<float,480> l{},r{};std::array<Frame,480> frames{};
    for(size_t i=0;i<l.size();++i)l[i]=r[i]=float(.1*std::sin(2*p*1000*i/48000.));
    float* channels[]={l.data(),r.data()};
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<6000;++i){tap->capture(channels,2,480);const auto n=tap->read(frames.data(),frames.size());assert(n==480);meter->push(frames.data(),n);}
    const auto time=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    assert(tap->dropped()==0);assert(std::abs(meter->duration()-60.)<1e-6);assert(std::abs(meter->get().integrated+20.)<.15);
    assert(meter->historyCount()==600);assert(meter->get().lra>=0.&&meter->get().lra<.1);
    std::cout<<"60 s synthetic stereo analysis, "<<time<<" s CPU wall time, no FIFO losses in sequential transfer\n";
    std::vector<std::string> rows{"Rapport A/B","Synthèse (A) et référence B","True Peak estimé : -6 dBTP"};
    for(int i=0;i<60;++i)rows.push_back("Repere "+std::to_string(i+1));
    const auto pdf=LibertyReference::makePdf(rows);
    assert(pdf.find("/Count 2")!=std::string::npos&&pdf.find("startxref")!=std::string::npos);
    if(argc==2){std::ofstream file(argv[1],std::ios::binary);file.write(pdf.data(),(std::streamsize)pdf.size());assert(file.good());}
    std::cout<<"PASS multi-page PDF generation\n";
}
