#pragma once
#include "LibertyMasteringMeter.h"
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
            auto out=std::make_unique<OfflineResult>();out->name=file.getFileName();
            juce::AudioFormatManager formats;formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
            if(!reader)out->error=juce::String::fromUTF8("Fichier audio illisible.");
            else if(reader->numChannels<1||reader->numChannels>2)out->error=juce::String::fromUTF8("Seuls les fichiers mono et stéréo sont pris en charge.");
            else if(reader->sampleRate<8000.||reader->sampleRate>384000.||reader->lengthInSamples<=0||double(reader->lengthInSamples)/reader->sampleRate>43200.)out->error=juce::String::fromUTF8("Durée ou fréquence non prise en charge (maximum 12 h).");
            else
            {
                out->rate=reader->sampleRate;out->channels=(int)reader->numChannels;out->duration=double(reader->lengthInSamples)/out->rate;
                auto meter=std::make_unique<Meter>();meter->configure(out->rate);
                const int block=static_cast<int>(std::round(out->rate*.1));
                juce::AudioBuffer<float> pcm(2,block);std::vector<LibertyImmersive::Frame> frames((size_t)block);
                out->timeline.reserve((size_t)std::ceil(out->duration*10.));
                for(juce::int64 pos=0;pos<reader->lengthInSamples&&!cancelled.load();)
                {
                    const int n=(int)std::min<juce::int64>(block,reader->lengthInSamples-pos);
                    pcm.clear();if(!reader->read(&pcm,0,n,pos,true,reader->numChannels>1)){out->error=juce::String::fromUTF8("Erreur de lecture audio.");break;}
                    for(int i=0;i<n;++i){const float l=pcm.getSample(0,i),r=reader->numChannels>1?pcm.getSample(1,i):0.f;frames[(size_t)i]={std::isfinite(l)?l:0.f,std::isfinite(r)?r:0.f};}
                    meter->push(frames.data(),(size_t)n);pos+=n;
                    const auto r=meter->get();if(n==block){out->timeline.push_back(r.momentary);out->maxM=std::max(out->maxM,r.momentary);out->maxS=std::max(out->maxS,r.shortTerm);}
                    progress.store(float(double(pos)/double(reader->lengthInSamples)));
                }
                if(cancelled.load())out->error=juce::String::fromUTF8("Analyse annulée");
                if(out->error.isEmpty()){meter->finish();out->reading=meter->get();}
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
class OfflineView final:public juce::Component,private juce::Timer
{
    juce::TextButton loadA{"IMPORT A"},loadB{"IMPORT B"},cancel{"ANNULER"},exportButton{"EXPORT TXT"};
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
    void refresh()
    {
        loadA.setEnabled(!job&&!chooser);loadB.setEnabled(!job&&!chooser);cancel.setEnabled((bool)job);exportButton.setEnabled(!job&&!chooser&&(results[0]||results[1]));repaint();
    }
    void choose(int slot)
    {
        if(job||chooser)return;
        destination=slot;
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
        return s;
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
        if(!job)return;
        if(job->finished()){auto r=job->take();job.reset();if(r&&r->error.isEmpty()){status=juce::String::fromUTF8("Analyse terminée : ")+r->name;results[(size_t)destination]=std::move(r);}else status=r?r->error:"Analyse impossible";refresh();}
        else repaint();
    }
public:
    OfflineView()
    {
        loadA.onClick=[this]{choose(0);};loadB.onClick=[this]{choose(1);};cancel.onClick=[this]{if(job)job->cancel();};exportButton.onClick=[this]{save();};
        for(auto* b:{&loadA,&loadB,&cancel,&exportButton})addAndMakeVisible(*b);
        targetLabel.setText("CIBLE LUFS",juce::dontSendNotification);ceilingLabel.setText("PLAFOND dBTP",juce::dontSendNotification);
        for(auto* l:{&targetLabel,&ceilingLabel}){l->setJustificationType(juce::Justification::centredRight);addAndMakeVisible(*l);}
        target.setRange(-36.,-5.,.1);target.setValue(-14.,juce::dontSendNotification);ceiling.setRange(-6.,0.,.1);ceiling.setValue(-1.,juce::dontSendNotification);
        for(auto* s:{&target,&ceiling}){s->setSliderStyle(juce::Slider::LinearHorizontal);s->setTextBoxStyle(juce::Slider::TextBoxRight,false,55,23);s->onValueChange=[this]{repaint();};addAndMakeVisible(*s);}
        setSize(800,540);refresh();startTimerHz(10);
    }
    ~OfflineView() override{stopTimer();chooser.reset();job.reset();}
    void resized() override
    {
        auto b=getLocalBounds().reduced(18).withTrimmedTop(42).removeFromTop(32);
        for(auto* button:{&loadA,&loadB,&cancel,&exportButton})button->setBounds(b.removeFromLeft(130).reduced(3,1));
        auto r=getLocalBounds().reduced(18).withTrimmedTop(80).removeFromTop(30);
        targetLabel.setBounds(r.removeFromLeft(100));target.setBounds(r.removeFromLeft(235));ceilingLabel.setBounds(r.removeFromLeft(118));ceiling.setBounds(r.removeFromLeft(235));
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(LibertyUI::Colour::canvasDeep);
        drawLabel(g,"OBSIDIAN MASTERING PRO - ANALYSE A/B",{18,10,getWidth()-36,28},LibertyUI::Colour::textPrimary,true);
        const juce::String metrics[]={ "LUFS-I","LUFS-M MAX","LUFS-S MAX","LRA LU","dBTP EST.","CIBLE LU","MARGE TP"};
        for(int i=0;i<2;++i)
        {
            auto b=juce::Rectangle<int>(18+i*(getWidth()-36)/2,132,(getWidth()-36)/2-8,196);
            LibertyUI::fillPanel(g,b.toFloat(),false,8.f);
            const auto c=i?LibertyUI::Colour::accentPurple:LibertyUI::Colour::accent;
            const auto* a=results[(size_t)i].get();
            drawLabel(g,juce::String(i?"B  |  ":"A  |  ")+(a?a->name:juce::String("AUCUN FICHIER")),b.removeFromTop(26),c,true);
            if(!a)continue;
            const double values[]={a->reading.integrated,a->maxM,a->maxS,a->reading.lra,a->reading.truePeak,a->reading.integrated-target.getValue(),ceiling.getValue()-a->reading.truePeak};
            for(int k=0;k<7;++k){auto row=b.removeFromTop(20).reduced(12,0);drawLabel(g,metrics[k],row.removeFromLeft(row.getWidth()/2));drawLabel(g,number(values[k]),row,c,true);}
            drawLabel(g,number(a->duration)+" s | "+juce::String(a->rate,0)+" Hz | "+juce::String(a->channels)+" ch",b.removeFromTop(20));
        }
        auto graph=juce::Rectangle<float>(58.f,352.f,(float)getWidth()-86.f,116.f);LibertyUI::fillPanel(g,graph.expanded(8.f),false,7.f);
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
        drawLabel(g,"0 s  |  LUFS-M A / B  |  "+number(maxDuration)+" s",{58,474,getWidth()-86,18});
        drawLabel(g,job?status+"  "+juce::String(job->fraction()*100.f,0)+" %":status,{18,500,getWidth()-36,24});
    }
};
class OfflineWindow final:public juce::DocumentWindow
{
public:
    OfflineWindow():juce::DocumentWindow("Liberty - Mastering Pro A/B",LibertyUI::Colour::canvasDeep,juce::DocumentWindow::closeButton)
    {setUsingNativeTitleBar(true);setResizable(false,false);setContentOwned(new OfflineView(),true);centreWithSize(800,540);}
    void closeButtonPressed() override{setVisible(false);}
};
}
