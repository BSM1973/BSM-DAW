#include "LibertySampleAnalysis.h"
#include <iostream>
#include <cstdlib>
using namespace LibertySampleAnalysis;
void check(bool b,const char* m){if(!b){std::cerr<<"FAIL: "<<m<<'\n';std::exit(1);}}
int main(){
 constexpr double rate=12000;std::vector<float> s(12000);
 auto quiet=analyse(s,rate);check(quiet.bpm==0&&quiet.key==-1&&quiet.rms==0,"silence unknown");
 s.resize(96000);
 for(int i=0;i<(int)s.size();++i){int t=i%6000;s[size_t(i)]=t<1000?float(.8*std::exp(-t/140.)*std::sin(2*pi*120*t/rate)):0;}
 auto r=analyse(s,rate);check(r.onsets.size()>=4,"periodic transients");check(r.bpm>=65&&r.bpm<=190&&r.beatConfidence>.24,"periodic tempo");
 check(r.centroid>0&&r.rms>0&&std::isfinite(r.flatness),"spectral features");
 check(pitchDelta(11,0)==1&&pitchDelta(0,11)==-1,"pitch mapping");
 const auto identical=similarity(r,r);check(identical>99.99,"identity score");
 std::vector<float> bright(96000);for(size_t i=0;i<bright.size();++i)bright[i]=float(.4*std::sin(2*pi*3900*i/rate));
 auto other=analyse(bright,rate);check(similarity(r,other)<identical-10,"different spectrum and envelope");check(similarity(r,quiet)==0,"silent reference rejected");
 std::cout<<"PASS: sample analysis\n";
}
