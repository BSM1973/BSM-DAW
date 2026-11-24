#pragma once
#include "LibertySampleLab.h"
namespace LibertySoundMatch {
using Result=LibertySampleAnalysis::Result;
struct Hit{juce::File file;Result result;double score=0;};
struct Catalog{std::vector<Hit> hits;int cached=0,analysed=0,pending=0;bool limited=false,saved=false;};
inline Result analyseBuffer(const juce::AudioBuffer<float>& a,double rate){
 if(rate<8000||rate>192000||a.getNumChannels()==0)return {};
 const int n=juce::jmin(a.getNumSamples(),int(rate*8)),stride=juce::jmax(1,int(rate/12000));
 std::vector<float> mono;mono.reserve(size_t(n/stride));
 for(int i=0;i+stride<=n;i+=stride){double v=0;for(int c=0;c<a.getNumChannels();++c)for(int j=0;j<stride;++j)v+=a.getSample(c,i+j);mono.push_back(float(v/(stride*a.getNumChannels())));}
 return LibertySampleAnalysis::analyse(mono,rate/stride);
}
inline Result analyseFile(const juce::File& file){juce::AudioBuffer<float> a;double rate=0;if(!LibertySampleLab::read(file,a,rate,8))return {};return analyseBuffer(a,rate);}
template<class Array> inline juce::String encode(const Array& a){juce::StringArray values;for(auto v:a)values.add(juce::String(v,8));return values.joinIntoString(",");}
template<class Array> inline bool decode(const juce::String& value,Array& a){auto values=juce::StringArray::fromTokens(value,",","");if(values.size()!=(int)a.size())return false;for(size_t i=0;i<a.size();++i){a[i]=values[(int)i].getDoubleValue();if(!std::isfinite(a[i])||a[i]<0||a[i]>1.01)return false;}return true;}
inline void write(juce::XmlElement& x,const Result& r){
 x.setAttribute("duration",r.duration);x.setAttribute("rms",r.rms);x.setAttribute("bpm",r.bpm);x.setAttribute("key",r.key);x.setAttribute("minor",r.minor);x.setAttribute("centroid",r.centroid);x.setAttribute("onsets",(int)r.onsets.size());x.setAttribute("spectrum",encode(r.spectrum));x.setAttribute("envelope",encode(r.envelope));
}
inline bool read(const juce::XmlElement& x,Result& r){
 r.duration=x.getDoubleAttribute("duration");r.rms=x.getDoubleAttribute("rms");r.bpm=x.getDoubleAttribute("bpm");r.key=x.getIntAttribute("key",-1);r.minor=x.getBoolAttribute("minor");r.centroid=x.getDoubleAttribute("centroid");
 r.onsets.resize((size_t)juce::jlimit(0,200,x.getIntAttribute("onsets")));
 return std::isfinite(r.duration)&&r.duration>0&&r.duration<=8.1&&std::isfinite(r.rms)&&r.rms>=0&&std::isfinite(r.centroid)&&r.centroid>=0&&std::isfinite(r.bpm)&&r.bpm>=0&&r.key>=-1&&r.key<12&&decode(x.getStringAttribute("spectrum"),r.spectrum)&&decode(x.getStringAttribute("envelope"),r.envelope);
}
inline Catalog search(const juce::File& root,const juce::File& cache,const juce::File& referenceFile,const Result& reference,const std::atomic<bool>& stop){
 Catalog out;auto old=juce::XmlDocument::parse(cache);
 std::map<juce::String,const juce::XmlElement*> entries;
 if(old&&old->hasTagName("LibertyAcousticIndex")&&old->getIntAttribute("version")==1&&old->getStringAttribute("root")==root.getFullPathName())
  for(auto* item:old->getChildIterator())entries[item->getStringAttribute("path")]=item;
 juce::XmlElement next("LibertyAcousticIndex");next.setAttribute("version",1);next.setAttribute("root",root.getFullPathName());
 int visited=0;
 for(const auto& item:juce::RangedDirectoryIterator(root,true,"*",juce::File::findFiles)){
  if(stop.load())return out;
  if(++visited>12000){out.limited=true;break;}
  const auto file=item.getFile();if(!file.hasFileExtension("wav;aif;aiff;mp3;flac"))continue;
  const auto path=file.getFullPathName();const auto modified=file.getLastModificationTime().toMilliseconds(),size=file.getSize();
  Result r;auto found=entries.find(path);
  bool valid=found!=entries.end()&&found->second->getStringAttribute("size").getLargeIntValue()==size&&found->second->getStringAttribute("modified").getLargeIntValue()==modified&&read(*found->second,r);
  if(valid)++out.cached;else{
   if(out.analysed>=128){++out.pending;continue;}
   ++out.analysed;r=analyseFile(file);if(r.duration<=0)continue;
   if(file.getSize()!=size||file.getLastModificationTime().toMilliseconds()!=modified)continue;
  }
  auto* node=next.createNewChildElement("Sample");node->setAttribute("path",path);node->setAttribute("size",juce::String(size));node->setAttribute("modified",juce::String(modified));write(*node,r);
  if(file!=referenceFile&&r.rms>1.e-6&&reference.rms>1.e-6)out.hits.push_back({file,r,LibertySampleAnalysis::similarity(reference,r)});
 }
 if(!stop.load()){juce::TemporaryFile temp(cache);out.saved=temp.getFile().replaceWithText(next.toString())&&temp.overwriteTargetFileWithTemporary();}
 std::sort(out.hits.begin(),out.hits.end(),[](const Hit& a,const Hit& b){return a.score==b.score?a.file.getFullPathName()<b.file.getFullPathName():a.score>b.score;});
 return out;
}
}
