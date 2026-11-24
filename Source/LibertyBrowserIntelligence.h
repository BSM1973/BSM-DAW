#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "LibertyDesignSystem.h"
#include <algorithm>
#include <array>
#include <functional>
#include <vector>

namespace LibertyBrowserIntelligence
{
inline juce::StringArray parseTags(const juce::String& text)
{
    juce::StringArray tags;
    for (auto tag : juce::StringArray::fromTokens(text, ",;", ""))
    {
        tag = tag.trim().substring(0, 40);
        if (tag.isNotEmpty() && !tags.contains(tag, true)) tags.add(tag);
        if (tags.size() == 12) break;
    }
    return tags;
}
inline bool matches(const juce::String& haystack, const juce::String& query)
{
    for (const auto& token : juce::StringArray::fromTokens(query.trim(), " \t", ""))
        if (token.isNotEmpty() && !haystack.containsIgnoreCase(token)) return false;
    return true;
}
struct Entry
{
    juce::String title, type, detail, searchText, id;
    juce::PluginDescription plugin;
    juce::File file;
    juce::StringArray tags;
    bool isPlugin = false;
    juce::String searchable() const
    {
        return title+" "+type+" "+detail+" "+searchText+" "+tags.joinIntoString(" ");
    }
};
class Panel final : public juce::Component
{
public:
    using Activate = std::function<void(const Entry&)>;
    using Save = std::function<bool(const juce::String&,const juce::StringArray&)>;
    Panel(std::vector<Entry> entriesIn, Activate action, Save save, bool writable)
        : entries(std::move(entriesIn)), activate(std::move(action)), saveTags(std::move(save)),
          canEdit(writable)
    {
        addAndMakeVisible(search);
        search.setTextToShowWhenEmpty(juce::String::fromUTF8("Rechercher un plugin, sample, MIDI, preset ou projet..."), LibertyUI::Colour::textMuted);
        search.onTextChange=[this]{page=0;selected=-1;refresh();};
        search.onReturnKey=[this]{if(selected<0&&!results.empty())select(results[0]);else runAction();};
        for (int i=0;i<6;++i)
        {
            auto& button=rows[(size_t)i];
            button.setColour(juce::TextButton::buttonColourId,LibertyUI::Colour::control);
            button.setColour(juce::TextButton::buttonOnColourId,LibertyUI::Colour::selection);
            button.onClick=[this,i]{const auto n=page*6+i;if(n<(int)results.size())select(results[(size_t)n]);};
            addAndMakeVisible(button);
        }
        for (auto* b:{&previous,&next,&actionButton,&saveButton,&closeButton}) addAndMakeVisible(*b);
        previous.setButtonText(juce::String::fromUTF8("Précédent")); next.setButtonText("Suivant");
        actionButton.setButtonText(juce::String::fromUTF8("Insérer"));saveButton.setButtonText("Enregistrer les tags");
        closeButton.setButtonText("Fermer");
        previous.onClick=[this]{if(page>0){--page;refreshRows();}};
        next.onClick=[this]{if((page+1)*6<(int)results.size()){++page;refreshRows();}};
        actionButton.onClick=[this]{runAction();};
        saveButton.onClick=[this]{
            if(selected<0||selected>=(int)entries.size()||!entries[(size_t)selected].isPlugin||!canEdit)return;
            auto& e=entries[(size_t)selected];const auto values=parseTags(tagEditor.getText());
            if(saveTags&&saveTags(e.id,values)){e.tags=values;tagEditor.setText(values.joinIntoString(", "),false);status.setText(juce::String::fromUTF8("Tags enregistrés."),juce::dontSendNotification);refresh();}
            else status.setText("Enregistrement impossible.",juce::dontSendNotification);
        };
        closeButton.onClick=[this]{close();};
        addAndMakeVisible(count);addAndMakeVisible(status);addAndMakeVisible(tagEditor);
        for(auto* label:{&count,&status}){label->setColour(juce::Label::textColourId,LibertyUI::Colour::textMuted);label->setFont(juce::Font(12.f));}
        tagEditor.setTextToShowWhenEmpty(juce::String::fromUTF8("Tags séparés par des virgules"),LibertyUI::Colour::textMuted);
        setSize(760,510);refresh();
    }
    void setInitialPlugin(const juce::String& id)
    {
        for(int i=0;i<(int)entries.size();++i)
            if(entries[(size_t)i].isPlugin&&entries[(size_t)i].id==id)
            {search.setText(entries[(size_t)i].title,false);refresh();select(i);return;}
    }
    void addFiles(std::vector<Entry> files, bool limited)
    {
        const int first=(int)entries.size();
        entries.reserve(entries.size()+files.size());
        for(auto& e:files)entries.push_back(std::move(e));
        status.setText(juce::String((int)entries.size()-first)+juce::String::fromUTF8(" fichiers indexés")+(limited?" (limite atteinte)":"."),juce::dontSendNotification);
        refresh();
    }
    void setIndexing(){status.setText("Indexation locale en cours...",juce::dontSendNotification);}
    void paint(juce::Graphics& g) override
    {
        g.fillAll(LibertyUI::Colour::canvasDeep);
        const auto area=getLocalBounds().reduced(12);
        LibertyUI::fillPanel(g,area.toFloat(),false,8.f);
        g.setColour(LibertyUI::Colour::accentLight);g.setFont(juce::Font(19.f,juce::Font::bold));
        g.drawFittedText("LIBERTY QUICK SEARCH",area.getX()+12,area.getY()+5,area.getWidth()-24,28,juce::Justification::centredLeft,1);
        const int x=getWidth()/2+10,w=getWidth()-x-24;
        g.setColour(LibertyUI::Colour::textPrimary);g.setFont(juce::Font(15.f,juce::Font::bold));
        g.drawFittedText("INSPECTEUR",x,85,w,25,juce::Justification::centredLeft,1);
        if(selected<0||selected>=(int)entries.size())return;
        const auto& e=entries[(size_t)selected];
        if(art.isValid())g.drawImageWithin(art,x,115,w,105,juce::RectanglePlacement::centred);
        else
        {
            g.setColour(LibertyUI::Colour::surfaceRaised);g.fillRoundedRectangle((float)x,115.f,(float)w,105.f,7.f);
            g.setColour(LibertyUI::Colour::accentLight);g.setFont(juce::Font(16.f,juce::Font::bold));
            g.drawFittedText(e.type,x+8,115,w-16,105,juce::Justification::centred,2);
        }
        g.setColour(LibertyUI::Colour::textPrimary);g.setFont(juce::Font(16.f,juce::Font::bold));
        g.drawFittedText(e.title,x,230,w,43,juce::Justification::centredLeft,2);
        g.setColour(LibertyUI::Colour::textMuted);g.setFont(juce::Font(12.f));
        auto details=e.detail+"\n"+e.searchText;
        if(e.isPlugin&&e.plugin.fileOrIdentifier=="liberty.native.freespirit-delay")
            details+=juce::String::fromUTF8("\n12 moteurs / 100 presets d'usine");
        g.drawFittedText(details,x,278,w,75,juce::Justification::topLeft,4);
        if(e.isPlugin){g.setColour(LibertyUI::Colour::accentLight);g.drawText("TAGS PERSONNELS",x,355,w,18,juce::Justification::centredLeft);}
    }
    void resized() override
    {
        const int left=24,split=getWidth()/2,right=split+10,w=getWidth()-right-24;
        search.setBounds(left,49,getWidth()-48,30);
        count.setBounds(left,85,split-36,22);
        for(int i=0;i<6;++i)rows[(size_t)i].setBounds(left,111+i*44,split-36,39);
        previous.setBounds(left,382,(split-44)/2,30);next.setBounds(left+(split-44)/2+8,382,(split-44)/2,30);
        tagEditor.setBounds(right,377,w,28);saveButton.setBounds(right,411,w,28);
        actionButton.setBounds(right,445,w,31);
        status.setBounds(left,420,split-36,23);closeButton.setBounds(left,445,split-36,31);
    }
private:
    void refresh()
    {
        results.clear();
        for(int i=0;i<(int)entries.size();++i)
            if(matches(entries[(size_t)i].searchable(),search.getText()))results.push_back(i);
        if(selected>=0&&std::find(results.begin(),results.end(),selected)==results.end())selected=-1;
        page=juce::jlimit(0,juce::jmax(0,((int)results.size()-1)/6),page);
        refreshRows();refreshInspector();
    }
    void refreshRows()
    {
        count.setText(juce::String((int)results.size())+juce::String::fromUTF8(" résultats  |  ")+juce::String(page+1)+"/"+juce::String(juce::jmax(1,((int)results.size()+5)/6)),juce::dontSendNotification);
        for(int i=0;i<6;++i)
        {
            const auto n=page*6+i;auto& row=rows[(size_t)i];
            const bool valid=n<(int)results.size();row.setVisible(valid);
            if(valid){const auto& e=entries[(size_t)results[(size_t)n]];row.setButtonText(e.title+"  /  "+e.type);row.setTooltip(e.title+"\n"+e.detail);row.setToggleState(results[(size_t)n]==selected,juce::dontSendNotification);}
        }
        previous.setEnabled(page>0);next.setEnabled((page+1)*6<(int)results.size());
    }
    void select(int i){selected=i;refreshRows();refreshInspector();}
    void refreshInspector()
    {
        art={};
        const bool valid=selected>=0&&selected<(int)entries.size();
        const bool plugin=valid&&entries[(size_t)selected].isPlugin;
        tagEditor.setVisible(plugin);saveButton.setVisible(plugin);
        tagEditor.setEnabled(canEdit);saveButton.setEnabled(canEdit);actionButton.setEnabled(valid);
        actionButton.setButtonText(plugin?juce::String::fromUTF8("Insérer"):juce::String("Afficher dans le navigateur"));
        if(plugin)
        {
            const auto& e=entries[(size_t)selected];tagEditor.setText(e.tags.joinIntoString(", "),false);
            const auto folder=juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Liberty").getChildFile("PluginThumbnails");
            const auto name=juce::String::toHexString((juce::int64)e.plugin.createIdentifierString().hashCode64())+".png";
            auto image=folder.getChildFile(name);if(!image.existsAsFile())image=folder.getSiblingFile("PluginArtworkCatalogue").getChildFile(name);
            if(image.existsAsFile())art=juce::ImageFileFormat::loadFrom(image);
        }
        repaint();
    }
    void close(){if(auto* dialog=findParentComponentOfClass<juce::DialogWindow>())dialog->exitModalState(0);}
    void runAction()
    {
        if(selected<0||selected>=(int)entries.size())return;
        const auto entry=entries[(size_t)selected];auto callback=activate;
        close();
        if(callback)juce::MessageManager::callAsync([callback,entry]{callback(entry);});
    }
    std::vector<Entry> entries;std::vector<int> results;int page=0,selected=-1;
    Activate activate;Save saveTags;bool canEdit=true;
    juce::TextEditor search,tagEditor;std::array<juce::TextButton,6> rows;
    juce::TextButton previous,next,actionButton,saveButton,closeButton;
    juce::Label count,status;juce::Image art;
};
}
