#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numeric>
#include <vector>
namespace LibertySampleAnalysis {
constexpr double pi=3.14159265358979323846;
struct Result {
 double duration=0,bpm=0,beatConfidence=0,keyConfidence=0,centroid=0,flatness=0,rms=0;
 int key=-1;bool minor=false;
 std::vector<double> onsets;std::array<double,12> chroma{};
};
inline void fft(std::vector<std::complex<double>>& a){
 const size_t n=a.size();
 for(size_t i=1,j=0;i<n;++i){size_t bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(a[i],a[j]);}
 for(size_t len=2;len<=n;len*=2){auto step=std::polar(1.,-2*pi/len);for(size_t i=0;i<n;i+=len){std::complex<double> z(1,0);for(size_t j=0;j<len/2;++j){auto u=a[i+j],v=a[i+j+len/2]*z;a[i+j]=u+v;a[i+j+len/2]=u-v;z*=step;}}}
}
inline Result analyse(const std::vector<float>& s,double rate){
 Result r;if(s.empty()||rate<8000)return r;r.duration=s.size()/rate;
 double energy=0;for(float v:s)energy+=double(v)*v;r.rms=std::sqrt(energy/s.size());if(r.rms<1.e-6)return r;
 constexpr int N=4096;const int hop=std::max(1,int(rate/100));
 std::vector<std::complex<double>> a(N);std::vector<double> prev(N/2),flux;
 std::array<double,N> hann{};for(int i=0;i<N;++i)hann[size_t(i)]=.5-.5*std::cos(2*pi*i/(N-1));
 double total=0,flat=0;int valid=0;
 for(size_t start=0;start<s.size();start+=size_t(hop)){
  for(int i=0;i<N;++i){auto n=start+size_t(i);a[size_t(i)]=(n<s.size()?s[n]:0)*hann[size_t(i)];}
  fft(a);double weight=0,hzSum=0,logs=0,change=0;
  for(int k=1;k<N/2;++k){double v=std::abs(a[size_t(k)]);weight+=v;hzSum+=v*k*rate/N;logs+=std::log(v+1.e-10);change+=std::max(0.,v-prev[size_t(k)]);prev[size_t(k)]=v;}
  flux.push_back(change);if(weight<1.e-5)continue;
  total+=weight;r.centroid+=hzSum;flat+=std::exp(logs/(N/2-1))/(weight/(N/2-1)+1.e-10);++valid;
  for(int k=2;k<N/2-1;++k){double v=prev[size_t(k)],hz=k*rate/N;if(hz<55||hz>5000||v<weight*.012||v<prev[size_t(k-1)]||v<prev[size_t(k+1)])continue;
   double l=std::log(prev[size_t(k-1)]+1.e-10),m=std::log(v+1.e-10),h=std::log(prev[size_t(k+1)]+1.e-10),den=l-2*m+h;
   double offset=std::abs(den)>1.e-9?std::clamp(.5*(l-h)/den,-.5,.5):0;
   int midi=int(std::lround(69+12*std::log2((k+offset)*rate/N/440.)));r.chroma[size_t((midi%12+12)%12)]+=v*v;
  }
 }
 r.centroid=total?r.centroid/total:0;r.flatness=valid?flat/valid:0;
 const double mean=std::accumulate(flux.begin(),flux.end(),0.)/flux.size(),peak=*std::max_element(flux.begin(),flux.end());
 for(size_t i=1;i+1<flux.size();++i)if(flux[i]>std::max(mean*1.6,peak*.08)&&flux[i]>=flux[i-1]&&flux[i]>flux[i+1]){
  double at=i*hop/rate;if(r.onsets.empty()||at-r.onsets.back()>.055)r.onsets.push_back(at);}
 if(r.duration>=2.5&&r.onsets.size()>=4){
  double best=0;int bestLag=0;const double fr=rate/hop;
  for(int lag=int(fr*60/190);lag<=int(fr*60/65)&&lag<int(flux.size()/2);++lag){double d=0,x2=0,y2=0;for(size_t i=size_t(lag);i<flux.size();++i){double x=std::max(0.,flux[i]-mean),y=std::max(0.,flux[i-size_t(lag)]-mean);d+=x*y;x2+=x*x;y2+=y*y;}double score=d/std::sqrt(x2*y2+1.e-12);if(score>best){best=score;bestLag=lag;}}
  if(bestLag&&best>.24){r.bpm=60*fr/bestLag;r.beatConfidence=std::min(1.,best);}
 }
 const double sum=std::accumulate(r.chroma.begin(),r.chroma.end(),0.);
 if(sum>1.e-9&&r.flatness<.2&&r.duration>.18){
  int notes=0;for(auto& v:r.chroma){v/=sum;if(v>.055)++notes;}
  const double major[]={6.35,2.23,3.48,2.33,4.38,4.09,2.52,5.19,2.39,3.66,2.29,2.88};
  const double minor[]={6.33,2.68,3.52,5.38,2.60,3.53,2.54,4.75,3.98,2.69,3.34,3.17};
  double best=-2,second=-2;int root=-1;bool mode=false;
  for(int k=0;k<12;++k)for(int m=0;m<2;++m){auto profile=m?minor:major;double avg=std::accumulate(profile,profile+12,0.)/12,d=0,x2=0,y2=0;for(int i=0;i<12;++i){double x=r.chroma[size_t((i+k)%12)]-1./12,y=profile[i]-avg;d+=x*y;x2+=x*x;y2+=y*y;}double score=d/std::sqrt(x2*y2+1.e-12);if(score>best){second=best;best=score;root=k;mode=m!=0;}else second=std::max(second,score);}
  if(notes>=3&&best>.5&&best-second>.035){r.key=root;r.minor=mode;r.keyConfidence=std::clamp((best-second)*4.,0.,1.);}
 }
 return r;
}
inline int pitchDelta(int from,int to){return from<0||from>11||to<0||to>11?0:(to-from+18)%12-6;}
}
