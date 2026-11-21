#pragma once
#include "LibertyMasteringMeter.h"
#include "LibertyReferenceAnalysis.h"
#include "LibertyReferencePdf.h"
#include "AudioEngine.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include "LibertyDesignSystem.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <thread>
#include <mutex>
#include <vector>
#include <memory>
namespace LibertyMastering
{
struct OfflineResult
{
    juce::String name,error;
    juce::File file;
    LibertyReference::Profile profile;
    Reading reading{};
    double duration=0.,rate=0.;
    int channels=0;
    double maxM=unavailable,maxS=unavailable;
    std::vector<double> timeline;
};
class OfflineJob
{
    std::thread worker;
    std::mutex mutex;
    std::unique_ptr<OfflineResult> result;
    std::atomic<bool> cancelled{false},done{false};
    std::atomic<float> progress{0.f};
public:
    explicit OfflineJob(juce::File file):worker()
    {
        worker=std::thread([this,file]
        {
            auto out=std::make_unique<OfflineResult>();out->name=file.getFileName();out->file=file;
            juce::AudioFormatManager formats;formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
            if(!reader)out->error=juce::String::fromUTF8("Fichier audio illisible.");
            else if(reader->numChannels<1||reader->numChannels>2)out->error=juce::String::fromUTF8("Seuls les fichiers mono et stéréo sont pris en charge.");
            else if(reader->sampleRate<8000.||reader->sampleRate>384000.||reader->lengthInSamples<=0||double(reader->lengthInSamples)/reader->sampleRate>43200.)out->error=juce::String::fromUTF8("Durée ou fréquence non prise en charge (maximum 12 h).");
            else
            {
                out->rate=reader->sampleRate;out->channels=(int)reader->numChannels;out->duration=double(reader->lengthInSamples)/out->rate;
                auto meter=std::make_unique<Meter>();meter->configure(out->rate);
                auto analyzer=std::make_unique<LibertyReference::Analyzer>(out->rate);
                const int block=static_cast<int>(std::round(out->rate*.1));
                juce::AudioBuffer<float> pcm(2,block);std::vector<LibertyImmersive::Frame> frames((size_t)block);
                out->timeline.reserve((size_t)std::ceil(out->duration*10.));
                for(juce::int64 pos=0;pos<reader->lengthInSamples&&!cancelled.load();)
                {
                    const int n=(int)std::min<juce::int64>(block,reader->lengthInSamples-pos);
                    pcm.clear();if(!reader->read(&pcm,0,n,pos,true,reader->numChannels>1)){out->error=juce::String::fromUTF8("Erreur de lecture audio.");break;}
                    for(int i=0;i<n;++i){const float l=pcm.getSample(0,i),r=reader->numChannels>1?pcm.getSample(1,i):0.f;frames[(size_t)i]={std::isfinite(l)?l:0.f,std::isfinite(r)?r:0.f};}
                    meter->push(frames.data(),(size_t)n);if(reader->numChannels==1)for(int i=0;i<n;++i)frames[(size_t)i].r=frames[(size_t)i].l;
                    analyzer->push(frames.data(),(size_t)n);pos+=n;
                    const auto r=meter->get();if(n==block){out->timeline.push_back(r.momentary);out->maxM=std::max(out->maxM,r.momentary);out->maxS=std::max(out->maxS,r.shortTerm);}
                    progress.store(float(double(pos)/double(reader->lengthInSamples)));
                }
                if(cancelled.load())out->error=juce::String::fromUTF8("Analyse annulée");
                if(out->error.isEmpty()){meter->finish();out->reading=meter->get();out->profile=analyzer->get();}
            }
            {std::lock_guard<std::mutex> lock(mutex);result=std::move(out);}
            done.store(true,std::memory_order_release);
        });
    }
    ~OfflineJob(){cancelled.store(true);if(worker.joinable())worker.join();}
    void cancel() noexcept {cancelled.store(true);}
    bool finished() const noexcept {return done.load(std::memory_order_acquire);}
    float fraction() const noexcept {return progress.load();}
    std::unique_ptr<OfflineResult> take(){if(!finished())return {};std::lock_guard<std::mutex> lock(mutex);return std::move(result);}
};
class ReferencePreview final:public juce::AudioSource
{
    AudioEngine& engine;
public:
    juce::AudioTransportSource transport;
    explicit ReferencePreview(AudioEngine& e):engine(e){}
    void prepareToPlay(int n,double rate) override{transport.prepareToPlay(n,rate);}
    void releaseResources() override{transport.releaseResources();}
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override
    {if(engine.isPlaying()||engine.isStepPreviewPlaying())info.clearActiveBufferRegion();else transport.getNextAudioBlock(info);}
};
class OfflineView final:public juce::Component,private juce::Timer
{
    juce::TextButton loadA{"IMPORT A"},loadB{"IMPORT B"},cancel{"ANNULER"},exportButton{"EXPORT TXT"};
    AudioEngine& engine;
    juce::TextButton playA{"LIRE A"},playB{"LIRE B"},stop{"STOP"},clearMarkers{"EFFACER REP."};
    juce::TextButton pdfButton{"EXPORT PDF"};
    juce::TextButton libraryAdd{"FAVORI B"},libraryRemove{"RETIRER"},libraryLoad{"CHARGER B"};
    juce::ComboBox graphMode,library;
    juce::StringArray references;
    std::vector<double> markers;
    ReferencePreview preview;
    juce::AudioSourcePlayer player;
    juce::TimeSliceThread readAhead{"Liberty reference"};
    std::unique_ptr<juce::AudioFormatReaderSource> previewReader;
    bool attached=false;
    int playingSlot=-1;
    double seekSeconds=0.;
    juce::Slider target,ceiling;
    juce::Label targetLabel,ceilingLabel;
    std::array<std::unique_ptr<OfflineResult>,2> results;
    std::unique_ptr<OfflineJob> job;
    std::unique_ptr<juce::FileChooser> chooser;
    int destination=0;
    juce::String status=juce::String::fromUTF8("Importer un master A et un fichier de référence B.");
    static juce::String number(double value){return std::isfinite(value)?juce::String(value,1):juce::String("--");}
    void drawLabel(juce::Graphics& g,juce::String s,juce::Rectangle<int> r,juce::Colour c=LibertyUI::Colour::textMuted,bool bold=false)
    {g.setColour(c);g.setFont(juce::Font(bold?14.f:12.f,bold?juce::Font::bold:juce::Font::plain));g.drawFittedText(s,r,juce::Justification::centred,1);}
    juce::File libraryFile() const {return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Liberty").getChildFile("reference-library.xml");}
    void syncLibrary()
    {library.clear(juce::dontSendNotification);for(int i=0;i<references.size();++i)library.addItem(juce::File(references[i]).getFileName(),i+1);if(references.size())library.setSelectedId(1,juce::dontSendNotification);}
    void saveLibrary(){juce::XmlElement xml("References");for(const auto& p:references){auto* e=xml.createNewChildElement("File");e->setAttribute("path",p);}const auto file=libraryFile();file.getParentDirectory().createDirectory();if(!xml.writeTo(file))status=juce::String::fromUTF8("Impossible d'enregistrer la bibliothèque.");}
    void stopPreview(){preview.transport.stop();if(attached){engine.getDeviceManager().removeAudioCallback(&player);attached=false;}player.setSource(nullptr);preview.transport.setSource(nullptr);previewReader.reset();playingSlot=-1;}
    void audition(int slot)
    {
        if(engine.isPlaying()||engine.isStepPreviewPlaying()||!results[0]||!results[1])return;
        const double position=playingSlot>=0?preview.transport.getCurrentPosition():seekSeconds;
        stopPreview();const auto& a=*results[(size_t)slot];const auto& b=*results[(size_t)(1-slot)];
        if(!std::isfinite(a.reading.integrated)||!std::isfinite(b.reading.integrated)||!std::isfinite(a.reading.truePeak)||!std::isfinite(b.reading.truePeak)){status=juce::String::fromUTF8("Mesures insuffisantes pour égaliser la sonie.");refresh();return;}
        juce::AudioFormatManager formats;formats.registerBasicFormats();auto* reader=formats.createReaderFor(a.file);if(!reader){status=juce::String::fromUTF8("Fichier de lecture indisponible.");refresh();return;}
        const auto sr=reader->sampleRate;previewReader=std::make_unique<juce::AudioFormatReaderSource>(reader,true);
        preview.transport.setSource(previewReader.get(),32768,&readAhead,sr);
        const auto gain=LibertyReference::matchGain(a.reading.integrated+(a.channels==1?3.0102999566:0.),b.reading.integrated+(b.channels==1?3.0102999566:0.),a.reading.truePeak,b.reading.truePeak);
        preview.transport.setGain((float)std::pow(10.,gain/20.));preview.transport.setPosition(position<a.duration?std::max(0.,position):0.);
        player.setSource(&preview);engine.getDeviceManager().addAudioCallback(&player);attached=true;playingSlot=slot;preview.transport.start();status=juce::String::fromUTF8("Écoute ")+juce::String(slot?"B":"A")+" | "+number(gain)+" dB";refresh();
    }
    void refresh()
    {
        loadA.setEnabled(!job&&!chooser);loadB.setEnabled(!job&&!chooser);cancel.setEnabled((bool)job);exportButton.setEnabled(!job&&!chooser&&(results[0]||results[1]));pdfButton.setEnabled(exportButton.isEnabled());
        const bool canPlay=results[0]&&results[1]&&!job&&!engine.isPlaying()&&!engine.isStepPreviewPlaying();playA.setEnabled(canPlay);playB.setEnabled(canPlay);stop.setEnabled(playingSlot>=0);libraryAdd.setEnabled((bool)results[1]);libraryLoad.setEnabled(!job&&!chooser&&library.getSelectedId()>0);libraryRemove.setEnabled(library.getSelectedId()>0);repaint();
    }
    void choose(int slot)
    {
        if(job||chooser)return;
        stopPreview();destination=slot;
        chooser=std::make_unique<juce::FileChooser>("Importer un fichier audio",juce::File::getSpecialLocation(juce::File::userMusicDirectory),"*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a");
        chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe=juce::Component::SafePointer<OfflineView>(this)](const juce::FileChooser& fc)
        {if(!safe)return;const auto file=fc.getResult();if(file.existsAsFile()){safe->job=std::make_unique<OfflineJob>(file);safe->status="Analyse "+file.getFileName();}juce::MessageManager::callAsync([safe]{if(safe){safe->chooser.reset();safe->refresh();}});});
        refresh();
    }
    juce::String report() const
    {
        juce::String s="LIBERTY OBSIDIAN MASTERING PRO\nAnalyse hors ligne mono/stereo - True Peak estime 8x, non certifie\n";
        s+="Date: "+juce::Time::getCurrentTime().toString(true,true)+"\nCible LUFS: "+number(target.getValue())+"\nPlafond dBTP: "+number(ceiling.getValue())+"\n";
        for(int i=0;i<2;++i)if(results[(size_t)i])
        {
            const auto& a=*results[(size_t)i];const auto r=a.reading;
            s+="\n"+juce::String(i?"B":"A")+" : "+a.name+"\nDuree s: "+number(a.duration)+"\nHz: "+juce::String(a.rate,0)+"\nCanaux: "+juce::String(a.channels)+"\nLUFS-I: "+number(r.integrated)+"\nLUFS-M max: "+number(a.maxM)+"\nLUFS-S max: "+number(a.maxS)+"\nLRA LU: "+number(r.lra)+"\nTrue peak estime dBTP: "+number(r.truePeak)+"\nEcart cible LU: "+number(r.integrated-target.getValue())+"\nMarge dBTP: "+number(ceiling.getValue()-r.truePeak)+"\nHistorique t(s);LUFS-M\n";
            for(size_t j=0;j<a.timeline.size();++j)s+=juce::String(double(j+1)*std::round(a.rate*.1)/a.rate,3)+";"+number(a.timeline[j])+"\n";
        }
        if(results[0]&&results[1])s+="\nA moins B LUFS-I: "+number(results[0]->reading.integrated-results[1]->reading.integrated)+"\nA moins B dBTP: "+number(results[0]->reading.truePeak-results[1]->reading.truePeak)+"\n";
        s+="\n";for(const auto& line:summaryLines())s+=juce::String::fromUTF8(line.c_str())+"\n";
        return s;
    }
    std::vector<std::string> summaryLines() const
    {
        std::vector<std::string> lines;
        const auto append=[&](const juce::String& text){for(int offset=0;offset<text.length();offset+=80)lines.push_back(text.substring(offset,offset+80).toStdString());if(text.isEmpty())lines.emplace_back();};
        append(juce::String::fromUTF8("Rapport A/B - mesures hors ligne non certifiées"));append(juce::Time::getCurrentTime().toString(true,true));append("Cible: "+number(target.getValue())+" LUFS | Plafond: "+number(ceiling.getValue())+" dBTP");
        for(int j=0;j<2;++j)if(const auto* a=results[(size_t)j].get()){
            append("");append(juce::String(j?"B : ":"A : ")+a->name);append(juce::String::fromUTF8("Durée : ")+number(a->duration)+" s | "+juce::String(a->rate,0)+" Hz | "+juce::String(a->channels)+" ch");
            append("LUFS-I: "+number(a->reading.integrated)+" | LUFS-M max: "+number(a->maxM)+" | LUFS-S max: "+number(a->maxS));append("LRA: "+number(a->reading.lra)+" LU | True Peak 8x: "+number(a->reading.truePeak)+" dBTP");append(juce::String::fromUTF8("Écart cible : ")+number(a->reading.integrated-target.getValue())+" LU | Marge TP: "+number(ceiling.getValue()-a->reading.truePeak)+" dB");
            append(juce::String::fromUTF8("Bande (Hz) | Énergie Side (%) | Max / moyenne (dB)"));
            for(int i=0;i<LibertyReference::bands;++i){const auto w=a->profile.width(i),d=a->profile.dynamics(i);append(juce::String(LibertyReference::edges[(size_t)i],0)+" - "+juce::String(LibertyReference::edges[(size_t)i+1],0)+" | "+(w<0.?juce::String("--"):number(w))+" | "+(d<0.?juce::String("--"):number(d)));}
        }
        if(results[0]&&results[1]){append("");append("A - B : "+number(results[0]->reading.integrated-results[1]->reading.integrated)+" LU | "+number(results[0]->reading.truePeak-results[1]->reading.truePeak)+" dBTP");}
        if(!markers.empty()){append("");append(juce::String::fromUTF8("Repères temporels (secondes)"));for(size_t i=0;i<markers.size();++i)append(juce::String((int)i+1)+" : "+number(markers[i]));}
        return lines;
    }
    void savePdf()
    {
        if(chooser||job)return;const auto content=LibertyReference::makePdf(summaryLines());
        chooser=std::make_unique<juce::FileChooser>("Exporter le rapport PDF",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Liberty-Reference-Studio.pdf"),"*.pdf");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[safe=juce::Component::SafePointer<OfflineView>(this),content](const juce::FileChooser& fc){if(!safe)return;const auto file=fc.getResult();if(file!=juce::File{})safe->status=file.replaceWithData(content.data(),content.size())?juce::String::fromUTF8("PDF enregistré."):juce::String::fromUTF8("Échec de l'export PDF.");juce::MessageManager::callAsync([safe]{if(safe){safe->chooser.reset();safe->refresh();}});});refresh();
    }
    void save()
    {
        if(chooser||job)return;
        const auto content=report();chooser=std::make_unique<juce::FileChooser>("Exporter le rapport A/B",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Liberty-Mastering-Pro.txt"),"*.txt");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[safe=juce::Component::SafePointer<OfflineView>(this),content](const juce::FileChooser& fc)
        {if(!safe)return;const auto file=fc.getResult();if(file!=juce::File{})safe->status=file.replaceWithText(content)?juce::String::fromUTF8("Rapport enregistré."):juce::String::fromUTF8("Échec de l'enregistrement.");juce::MessageManager::callAsync([safe]{if(safe){safe->chooser.reset();safe->refresh();}});});
        refresh();
    }
    void timerCallback() override
    {
        if(playingSlot>=0&&(engine.isPlaying()||engine.isStepPreviewPlaying()||preview.transport.hasStreamFinished()))stopPreview();
        if(!job){refresh();return;}
        if(job->finished()){auto r=job->take();job.reset();if(r&&r->error.isEmpty()){status=juce::String::fromUTF8("Analyse terminée : ")+r->name;results[(size_t)destination]=std::move(r);}else status=r?r->error:"Analyse impossible";refresh();}
        else repaint();
    }
public:
    explicit OfflineView(AudioEngine& e):engine(e),preview(e)
    {
        readAhead.startThread();
        if(auto xml=juce::parseXML(libraryFile()))for(auto* child=xml->getFirstChildElement();child;child=child->getNextElement())if(child->hasTagName("File"))references.addIfNotAlreadyThere(child->getStringAttribute("path"));
        syncLibrary();library.onChange=[this]{refresh();};
        libraryAdd.onClick=[this]{if(results[1]){references.addIfNotAlreadyThere(results[1]->file.getFullPathName());saveLibrary();syncLibrary();refresh();}};
        libraryRemove.onClick=[this]{const int i=library.getSelectedId()-1;if(i>=0&&i<references.size()){references.remove(i);saveLibrary();syncLibrary();refresh();}};
        libraryLoad.onClick=[this]{const int i=library.getSelectedId()-1;if(!job&&i>=0&&i<references.size()){stopPreview();destination=1;job=std::make_unique<OfflineJob>(juce::File(references[i]));status=juce::String::fromUTF8("Analyse de la référence");refresh();}};
        playA.onClick=[this]{audition(0);};playB.onClick=[this]{audition(1);};stop.onClick=[this]{stopPreview();seekSeconds=0.;refresh();};clearMarkers.onClick=[this]{markers.clear();seekSeconds=0.;repaint();};
        graphMode.addItem(juce::String::fromUTF8("SONIE / REPÈRES"),1);graphMode.addItem("SPECTRE A/B",2);graphMode.addItem(juce::String::fromUTF8("STÉRÉO / BANDES"),3);graphMode.addItem("DYNAMIQUE / BANDES",4);graphMode.setSelectedId(1,juce::dontSendNotification);graphMode.onChange=[this]{repaint();};
        for(auto* b:{&playA,&playB,&stop,&clearMarkers,&libraryAdd,&libraryRemove,&libraryLoad})addAndMakeVisible(*b);addAndMakeVisible(graphMode);addAndMakeVisible(library);
        loadA.onClick=[this]{choose(0);};loadB.onClick=[this]{choose(1);};cancel.onClick=[this]{if(job)job->cancel();};exportButton.onClick=[this]{save();};pdfButton.onClick=[this]{savePdf();};addAndMakeVisible(pdfButton);
        for(auto* b:{&loadA,&loadB,&cancel,&exportButton})addAndMakeVisible(*b);
        targetLabel.setText("CIBLE LUFS",juce::dontSendNotification);ceilingLabel.setText("PLAFOND dBTP",juce::dontSendNotification);
        for(auto* l:{&targetLabel,&ceilingLabel}){l->setJustificationType(juce::Justification::centredRight);addAndMakeVisible(*l);}
        target.setRange(-36.,-5.,.1);target.setValue(-14.,juce::dontSendNotification);ceiling.setRange(-6.,0.,.1);ceiling.setValue(-1.,juce::dontSendNotification);
        for(auto* s:{&target,&ceiling}){s->setSliderStyle(juce::Slider::LinearHorizontal);s->setTextBoxStyle(juce::Slider::TextBoxRight,false,55,23);s->onValueChange=[this]{repaint();};addAndMakeVisible(*s);}
        setSize(800,582);refresh();startTimerHz(10);
    }
    ~OfflineView() override{stopTimer();stopPreview();readAhead.stopThread(1000);chooser.reset();job.reset();}
    void hideAndStop(){stopPreview();refresh();}
    void mouseDown(const juce::MouseEvent& e) override
    {if(graphMode.getSelectedId()!=1||e.x<58||e.x>getWidth()-28||e.y<356||e.y>468)return;double length=0.;for(const auto& r:results)if(r)length=std::max(length,r->duration);if(length<=0.)return;seekSeconds=length*(e.x-58.)/double(getWidth()-86);if(markers.size()<64)markers.push_back(seekSeconds);if(playingSlot>=0&&results[(size_t)playingSlot]&&seekSeconds<results[(size_t)playingSlot]->duration)preview.transport.setPosition(seekSeconds);repaint();}
    void resized() override
    {
        auto b=getLocalBounds().reduced(18).withTrimmedTop(42).removeFromTop(32);
        for(auto* button:{&loadA,&loadB,&cancel,&exportButton,&pdfButton})button->setBounds(b.removeFromLeft(130).reduced(3,1));
        auto r=getLocalBounds().reduced(18).withTrimmedTop(80).removeFromTop(30);
        targetLabel.setBounds(r.removeFromLeft(100));target.setBounds(r.removeFromLeft(235));ceilingLabel.setBounds(r.removeFromLeft(118));ceiling.setBounds(r.removeFromLeft(235));
        auto controls=juce::Rectangle<int>(18,326,getWidth()-36,22);graphMode.setBounds(controls.removeFromLeft(174).reduced(1));for(auto* b:{&playA,&playB,&stop,&clearMarkers})b->setBounds(controls.removeFromLeft(90).reduced(1));
        auto lib=juce::Rectangle<int>(18,540,getWidth()-36,29);library.setBounds(lib.removeFromLeft(352).reduced(2));for(auto* b:{&libraryAdd,&libraryLoad,&libraryRemove})b->setBounds(lib.removeFromLeft(120).reduced(2));
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(LibertyUI::Colour::canvasDeep);
        drawLabel(g,"OBSIDIAN REFERENCE STUDIO - ANALYSE A/B",{18,10,getWidth()-36,28},LibertyUI::Colour::textPrimary,true);
        const juce::String metrics[]={ "LUFS-I","LUFS-M MAX","LUFS-S MAX","LRA LU","dBTP EST.","CIBLE LU","MARGE TP"};
        for(int i=0;i<2;++i)
        {
            auto b=juce::Rectangle<int>(18+i*(getWidth()-36)/2,132,(getWidth()-36)/2-8,192);
            LibertyUI::fillPanel(g,b.toFloat(),false,8.f);
            const auto c=i?LibertyUI::Colour::accentPurple:LibertyUI::Colour::accent;
            const auto* a=results[(size_t)i].get();
            drawLabel(g,juce::String(i?"B  |  ":"A  |  ")+(a?a->name:juce::String("AUCUN FICHIER")),b.removeFromTop(26),c,true);
            if(!a)continue;
            const double values[]={a->reading.integrated,a->maxM,a->maxS,a->reading.lra,a->reading.truePeak,a->reading.integrated-target.getValue(),ceiling.getValue()-a->reading.truePeak};
            for(int k=0;k<7;++k){auto row=b.removeFromTop(20).reduced(12,0);drawLabel(g,metrics[k],row.removeFromLeft(row.getWidth()/2));drawLabel(g,number(values[k]),row,c,true);}
            drawLabel(g,number(a->duration)+" s | "+juce::String(a->rate,0)+" Hz | "+juce::String(a->channels)+" ch",b.removeFromTop(20));
        }
        auto graph=juce::Rectangle<float>(58.f,356.f,(float)getWidth()-86.f,112.f);LibertyUI::fillPanel(g,graph.expanded(8.f),false,7.f);
        if(graphMode.getSelectedId()!=1)
        {
            const int mode=graphMode.getSelectedId();
            if(mode==2){for(int j=0;j<2;++j)if(const auto* r=results[(size_t)j].get()){juce::Path p;bool first=true;for(int i=0;i<LibertyReference::points;++i){const double value=r->profile.spectralDb(i);if(!std::isfinite(value)){first=true;continue;}const float x=graph.getX()+graph.getWidth()*i/float(LibertyReference::points-1),y=graph.getBottom()-graph.getHeight()*(float)std::clamp((value+100.)/100.,0.,1.);if(first){p.startNewSubPath(x,y);first=false;}else p.lineTo(x,y);}g.setColour(j?LibertyUI::Colour::accentPurple:LibertyUI::Colour::accent);g.strokePath(p,juce::PathStrokeType(1.5f));}drawLabel(g,"20 Hz | Spectre moyen par bandes logarithmiques (dBFS) | 20 kHz",{58,474,getWidth()-86,18});}
            else {const char* labels[]={"20-120","120-350","350-1k","1-3k","3-8k","8-20k"};for(int i=0;i<6;++i){auto b=graph.withWidth(graph.getWidth()/6.f).translated(graph.getWidth()*i/6.f,0.f);drawLabel(g,labels[i],b.withHeight(16).toNearestInt());for(int j=0;j<2;++j)if(const auto* r=results[(size_t)j].get()){const double value=mode==3?r->profile.width(i):r->profile.dynamics(i);const float h=(b.getHeight()-37.f)*(float)std::clamp(value/(mode==3?100.:30.),0.,1.);g.setColour(j?LibertyUI::Colour::accentPurple:LibertyUI::Colour::accent);const float x=b.getX()+b.getWidth()*(j?.56f:.13f);if(value>=0.)g.fillRoundedRectangle(x,b.getBottom()-18.f-h,b.getWidth()*.30f,h,2.f);drawLabel(g,value<0.?juce::String("--"):number(value),{(int)x,(int)b.getBottom()-16,(int)(b.getWidth()*.36f),16});}}drawLabel(g,mode==3?juce::String::fromUTF8("Part d'énergie Side (%) par bande"):juce::String::fromUTF8("Écart maximum / moyenne de puissance (dB) par bande"),{58,474,getWidth()-86,18});}
            drawLabel(g,job?status+"  "+juce::String(job->fraction()*100.f,0)+" %":status,{18,500,getWidth()-36,24});return;
        }
        const auto y=[&](double x){return graph.getBottom()-graph.getHeight()*(float)std::clamp((x+60.)/60.,0.,1.);};
        for(int i=-60;i<=0;i+=15){g.setColour(LibertyUI::Colour::divider);g.drawHorizontalLine((int)y(i),graph.getX(),graph.getRight());drawLabel(g,juce::String(i),{20,(int)y(i)-7,34,14});}
        g.setColour(LibertyUI::Colour::accentAmber);g.drawHorizontalLine((int)y(target.getValue()),graph.getX(),graph.getRight());
        double maxDuration=0.;for(const auto& r:results)if(r)maxDuration=std::max(maxDuration,r->duration);
        if(maxDuration>0.)for(int j=0;j<2;++j)if(const auto* r=results[(size_t)j].get())
        {
            juce::Path p;bool first=true;const auto n=r->timeline.size();
            const auto stride=std::max<size_t>(1,n/(size_t)std::max(1.f,graph.getWidth()));
            for(size_t i=0;i<n;i+=stride){double v=unavailable;for(size_t k=i;k<std::min(n,i+stride);++k)v=std::max(v,r->timeline[k]);if(!std::isfinite(v)){first=true;continue;}const float x=graph.getX()+graph.getWidth()*(float)std::clamp((double(i+1)*std::round(r->rate*.1)/r->rate)/maxDuration,0.,1.);if(first){p.startNewSubPath(x,y(v));first=false;}else p.lineTo(x,y(v));}
            g.setColour(j?LibertyUI::Colour::accentPurple:LibertyUI::Colour::accent);g.strokePath(p,juce::PathStrokeType(1.4f));
        }
        if(maxDuration>0.){g.setColour(LibertyUI::Colour::accentAmber);for(double time:markers){const float x=graph.getX()+graph.getWidth()*(float)std::clamp(time/maxDuration,0.,1.);g.drawVerticalLine((int)x,graph.getY(),graph.getBottom());}}
        drawLabel(g,"0 s  |  LUFS-M A / B  |  "+number(maxDuration)+" s",{58,474,getWidth()-86,18});
        drawLabel(g,job?status+"  "+juce::String(job->fraction()*100.f,0)+" %":status,{18,500,getWidth()-36,24});
    }
};
class OfflineWindow final:public juce::DocumentWindow
{
public:
    explicit OfflineWindow(AudioEngine& engine):juce::DocumentWindow("Liberty - Mastering Pro A/B",LibertyUI::Colour::canvasDeep,juce::DocumentWindow::closeButton)
    {setUsingNativeTitleBar(true);setResizable(false,false);setContentOwned(new OfflineView(engine),true);centreWithSize(800,582);}
    void closeButtonPressed() override{if(auto* view=dynamic_cast<OfflineView*>(getContentComponent()))view->hideAndStop();setVisible(false);}
};
}
