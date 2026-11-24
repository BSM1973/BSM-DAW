#pragma once
#include "LibertySampleAnalysis.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "LibertySampleStretch.h"
#include <cstdint>
#include <atomic>
#include <map>
namespace LibertySampleLab {
using Result=LibertySampleAnalysis::Result;
inline bool read(const juce::File& file,juce::AudioBuffer<float>& audio,double& rate,double limit){
 juce::AudioFormatManager fm;fm.registerBasicFormats();
 std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
 if(!reader||reader->sampleRate<8000||reader->sampleRate>192000||reader->lengthInSamples<=0)return false;
 rate=reader->sampleRate;int n=(int)juce::jmin(reader->lengthInSamples,(juce::int64)(rate*limit));
 audio.setSize(juce::jlimit(1,2,(int)reader->numChannels),n);return reader->read(&audio,0,n,0,true,true);
}
inline Result analyseFile(const juce::File& file){
 juce::AudioBuffer<float> a;double rate=0;if(!read(file,a,rate,30))return {};
 int stride=juce::jmax(1,int(rate/12000));std::vector<float> mono;
 mono.reserve((size_t)a.getNumSamples()/stride);
 for(int i=0;i+stride<=a.getNumSamples();i+=stride){double sum=0;for(int c=0;c<a.getNumChannels();++c)for(int j=0;j<stride;++j)sum+=a.getSample(c,i+j);mono.push_back(float(sum/(stride*a.getNumChannels())));}
 return LibertySampleAnalysis::analyse(mono,rate/stride);
}
inline juce::String note(int k){const char* names[]={"Do","Do#","Ré","Ré#","Mi","Fa","Fa#","Sol","Sol#","La","La#","Si"};return k>=0&&k<12?juce::String::fromUTF8(names[k]):juce::String("-");}
inline juce::String classify(const juce::File& file,const Result& r){
 auto n=file.getFileNameWithoutExtension().toLowerCase();if(n.contains("kick"))return "Kick";if(n.contains("snare")||n.contains("clap"))return "Snare / Clap";if(n.contains("hihat")||n.contains("hi-hat"))return "Hi-Hat";if(n.contains("riser")||n.contains("impact")||n.contains("fx"))return "FX";
 if(r.bpm>0)return "Loop probable";if(r.duration<.6&&r.centroid>0&&r.centroid<450)return "Percussion grave probable";if(r.duration<.6&&r.centroid>4000)return juce::String::fromUTF8("Percussion aiguë probable");return r.key>=0?juce::String("Son tonal probable"):juce::String::fromUTF8("Non classé");
}
inline juce::String describe(const juce::File& file,const Result& r){
 if(r.duration<=0)return "Analyse indisponible.";
 return file.getFileName()+"\nClassification : "+classify(file,r)
 +"\nBPM : "+(r.bpm>0?juce::String(r.bpm,1)+" ("+juce::String(r.beatConfidence*100,0)+" %)":juce::String::fromUTF8("Indéterminé"))
 +juce::String::fromUTF8("\nTonalité : ")+(r.key>=0?note(r.key)+(r.minor?" mineur":" majeur"):juce::String::fromUTF8("Indéterminée"))
 +"\nTransitoires : "+juce::String((int)r.onsets.size())
 +juce::String::fromUTF8("\nCentroïde spectral : ")+juce::String(r.centroid,0)+" Hz"
 +"\nPlatitude : "+juce::String(r.flatness,3)+"\nRMS : "+juce::String(20*std::log10(std::max(1.e-9,r.rms)),1)+" dBFS"
 +"\nAnalyse : "+juce::String(r.duration,2)+" s.";
}
inline std::unique_ptr<juce::AudioBuffer<float>> preview(const juce::File& file,double ratio,int pitch,double& rate){
 juce::AudioBuffer<float> in;if(!read(file,in,rate,20)||ratio<.5||ratio>2||std::abs(pitch)>6||in.getNumSamples()<1024)return {};
 int n=in.getNumSamples(),channels=in.getNumChannels(),length=juce::jmax(1,(int)std::lround(n/ratio));
 auto out=std::make_unique<juce::AudioBuffer<float>>(channels,length);out->clear();
 if(!renderStretch(in.getArrayOfReadPointers(),n,out->getArrayOfWritePointers(),length,channels,rate,pitch))return {};
 for(int c=0;c<channels;++c)for(int i=0;i<length;++i){float v=out->getSample(c,i);if(!std::isfinite(v))return {};out->setSample(c,i,juce::jlimit(-1.f,1.f,v));}
 return out;
}
inline juce::String fingerprint(const juce::File& file){
 auto in=file.createInputStream();if(!in||!in->openedOk())return {};
 std::uint64_t h=14695981039346656037ull;std::array<unsigned char,32768> data{};
 while(!in->isExhausted()){int n=in->read(data.data(),(int)data.size());if(n<=0)break;for(int i=0;i<n;++i){h^=data[size_t(i)];h*=1099511628211ull;}}
 return in->getPosition()==file.getSize()?juce::String::toHexString((juce::int64)h):juce::String{};
}
inline bool exact(const juce::File& a,const juce::File& b){
 if(a==b||a.getSize()!=b.getSize())return false;auto x=a.createInputStream(),y=b.createInputStream();if(!x||!y||!x->openedOk()||!y->openedOk())return false;
 std::array<char,32768> m{},n{};while(!x->isExhausted()){int k=x->read(m.data(),(int)m.size());if(k<=0||y->read(n.data(),k)!=k||!std::equal(m.begin(),m.begin()+k,n.begin()))return false;}return y->isExhausted();
}
inline juce::String index(const juce::File& root,const juce::File& destination,const std::atomic<bool>& stop){
 juce::XmlElement xml("LibertySampleIndex");xml.setAttribute("root",root.getFullPathName());xml.setAttribute("version",1);
 std::map<juce::String,juce::File> seen;int scanned=0,count=0,hashed=0,duplicates=0;juce::int64 bytes=0;bool limited=false;
 for(const auto& entry:juce::RangedDirectoryIterator(root,true,"*",juce::File::findFiles)){
  if(stop.load())return "Indexation interrompue.";
  if(++scanned>12000){limited=true;break;}
  const auto file=entry.getFile();if(!file.hasFileExtension("wav;aif;aiff;mp3;flac"))continue;
  auto* item=xml.createNewChildElement("Sample");item->setAttribute("path",file.getFullPathName());item->setAttribute("size",juce::String(file.getSize()));item->setAttribute("modified",juce::String(file.getLastModificationTime().toMilliseconds()));++count;
  if(file.getSize()>32*1024*1024||bytes+file.getSize()>512ll*1024*1024)continue;
  bytes+=file.getSize();auto hash=fingerprint(file);if(hash.isEmpty())continue;++hashed;item->setAttribute("hash",hash);
  auto key=juce::String(file.getSize())+":"+hash;auto old=seen.find(key);
  if(old!=seen.end()&&exact(old->second,file)){item->setAttribute("duplicateOf",old->second.getFullPathName());++duplicates;}else seen[key]=file;
 }
 juce::TemporaryFile temp(destination);if(!temp.getFile().replaceWithText(xml.toString())||!temp.overwriteTargetFileWithTemporary())return "Index non sauvegarde.";
 return juce::String(count)+juce::String::fromUTF8(" samples indexés / ")+juce::String(hashed)+juce::String::fromUTF8(" vérifiés / ")+juce::String(duplicates)+juce::String::fromUTF8(" doublons confirmés.")+(limited?" Limite de parcours atteinte.":"");
}
}
