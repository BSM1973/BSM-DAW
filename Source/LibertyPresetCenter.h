#pragma once
#include "PluginHost.h"
#include "LibertyDesignSystem.h"
#include "LibertyBrowserIntelligence.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <map>

namespace LibertyPresetCenter {
struct Entry {
 juce::String id,name,category,deviceId,tags;
 int factoryIndex=-1,rating=0;bool favourite=false;
 juce::MemoryBlock state;
 bool getState(juce::MemoryBlock& out) const {
  auto& host=LibertyPluginHost::instance();
  if(factoryIndex>=0)return host.getNativeFactoryPresetState(deviceId,factoryIndex,out);
  out=state;return host.validateNativePresetState(deviceId,out);
 }
};
inline std::unique_ptr<juce::XmlElement> toXml(const Entry& e){
 auto node=std::make_unique<juce::XmlElement>("Preset");node->setAttribute("id",e.id);node->setAttribute("name",e.name);node->setAttribute("category",e.category);node->setAttribute("device",e.deviceId);node->setAttribute("tags",e.tags);node->setAttribute("rating",e.rating);node->setAttribute("favourite",e.favourite?1:0);
 if(e.factoryIndex<0)node->setAttribute("state",e.state.toBase64Encoding());return node;
}
inline bool userFromXml(const juce::XmlElement& x,Entry& e){
 if(!x.hasTagName("Preset"))return false;
 e.id=x.getStringAttribute("id");e.name=x.getStringAttribute("name").trim().substring(0,80);e.category=x.getStringAttribute("category").trim().substring(0,80);e.deviceId=x.getStringAttribute("device");
 e.tags=LibertyBrowserIntelligence::parseTags(x.getStringAttribute("tags")).joinIntoString(", ");e.rating=juce::jlimit(0,5,x.getIntAttribute("rating"));e.favourite=x.getBoolAttribute("favourite");
 auto encoded=x.getStringAttribute("state");
 return e.name.isNotEmpty()&&encoded.length()<=1500000&&e.state.fromBase64Encoding(encoded)&&LibertyPluginHost::instance().validateNativePresetState(e.deviceId,e.state);
}
class Item final : public juce::TreeViewItem {
public:
 explicit Item(Entry value):entry(std::move(value)){}
 Entry entry;
 juce::String getUniqueName()const override{return entry.id;}
 bool mightContainSubItems()override{return false;}
 int getItemHeight()const override{return 34;}
 void paintItem(juce::Graphics& g,int w,int h)override{
  if(isSelected())LibertyUI::fillPanel(g,juce::Rectangle<float>(1,1,(float)juce::jmax(0,w-2),(float)juce::jmax(0,h-2)),true,5.f);
  g.setColour(LibertyUI::Colour::textPrimary);g.setFont(juce::Font(12.f,juce::Font::bold));
  g.drawFittedText(entry.name,6,2,juce::jmax(0,w-12),16,juce::Justification::centredLeft,1);
  g.setColour(LibertyUI::Colour::textMuted);g.setFont(juce::Font(10.f));
  g.drawFittedText(entry.category+" / "+(entry.factoryIndex>=0?"Usine":"Personnel")+(entry.favourite?" / FAVORI":"")+(entry.rating?" / "+juce::String(entry.rating)+"/5":""),6,18,juce::jmax(0,w-12),13,juce::Justification::centredLeft,1);
 }
};
class Root final:public juce::TreeViewItem{public:bool mightContainSubItems()override{return true;}juce::String getUniqueName()const override{return "LibertyPresetRoot";}};
class Panel final:public juce::Component {
public:
 using Apply=std::function<void(const Entry&)>;
 using Capture=std::function<void()>;
 Panel(juce::File file,Apply apply,Capture capture,Apply audition={}):storage(std::move(file)),onApply(std::move(apply)),onCapture(std::move(capture)),onAudition(std::move(audition)){
  for(auto* button:{&load,&save,&importButton,&exportButton,&favourite,&rateButton,&tagButton, &auditionButton})addAndMakeVisible(*button);
  load.setButtonText(juce::String::fromUTF8("Insérer"));save.setButtonText("Sauver slot");importButton.setButtonText("Importer");exportButton.setButtonText("Exporter");favourite.setButtonText("Favori");rateButton.setButtonText("Note");tagButton.setButtonText("Tags");
  addAndMakeVisible(search);search.setTextToShowWhenEmpty("Nom, famille, tags...",LibertyUI::Colour::textMuted);search.onTextChange=[this]{refresh();};
  addAndMakeVisible(filter);filter.addItem("Tous les presets",1);filter.addItem("Usine",2);filter.addItem("Personnels",3);filter.addItem("Favoris",4);filter.setSelectedId(1,juce::dontSendNotification);filter.onChange=[this]{refresh();};
  addAndMakeVisible(tree);tree.setRootItemVisible(false);tree.setMultiSelectEnabled(false);tree.setColour(juce::TreeView::backgroundColourId,LibertyUI::Colour::canvasDeep);
  addAndMakeVisible(status);status.setFont(juce::Font(11.f));status.setColour(juce::Label::textColourId,LibertyUI::Colour::textMuted);
  auditionButton.setButtonText("A/B");auditionButton.setTooltip(juce::String::fromUTF8("Préécouter sans modifier le projet"));auditionButton.onClick=[this]{if(auto* e=selected();e&&onAudition)onAudition(*e);};
  load.onClick=[this]{if(auto* e=selected())onApply(*e);};
  save.onClick=[this]{onCapture();};
  favourite.onClick=[this]{edit([](Entry& e){e.favourite=!e.favourite;});};
  rateButton.onClick=[this]{if(auto* e=selected()){juce::PopupMenu menu;for(int i=0;i<=5;++i)menu.addItem(i+1,i==0?juce::String("Sans note"):juce::String(i)+"/5",true,e->rating==i);auto safe=juce::Component::SafePointer<Panel>(this);const auto id=e->id;menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&rateButton),[safe,id](int n){if(safe&&n>0)safe->editId(id,[n](Entry& x){x.rating=n-1;});});}};
  tagButton.onClick=[this]{if(auto* e=selected()){auto id=e->id;auto dialog=std::make_shared<juce::AlertWindow>("Tags","Virgules : Tape, Vocal, Vintage...",juce::AlertWindow::NoIcon);dialog->addTextEditor("tags",e->tags);dialog->addButton("Enregistrer",1);dialog->addButton("Annuler",0);auto safe=juce::Component::SafePointer<Panel>(this);dialog->enterModalState(true,juce::ModalCallbackFunction::create([safe,dialog,id](int n){if(safe&&n==1){auto tags=LibertyBrowserIntelligence::parseTags(dialog->getTextEditorContents("tags")).joinIntoString(", ");safe->editId(id,[tags](Entry& x){x.tags=tags;});}}),false);}};
  importButton.onClick=[this]{chooseImport();};exportButton.onClick=[this]{chooseExport();};
  loadStorage();refresh();
 }
 ~Panel()override{tree.setRootItem(nullptr);}
 void resized()override{
  auto r=getLocalBounds().reduced(6);search.setBounds(r.removeFromTop(25));r.removeFromTop(4);filter.setBounds(r.removeFromTop(25));r.removeFromTop(4);
  status.setBounds(r.removeFromBottom(22));auto footer=r.removeFromBottom(29);layoutRow(footer,{&importButton,&exportButton,&save});r.removeFromBottom(4);auto actions=r.removeFromBottom(29);layoutRow(actions,{&load,&auditionButton,&favourite,&rateButton,&tagButton});r.removeFromBottom(4);tree.setBounds(r);
 }
 void addCaptured(juce::String device,juce::MemoryBlock state,juce::String name){
  if(readOnly||entries.size()>=1100||!LibertyPluginHost::instance().validateNativePresetState(device,state))return;
  Entry e;e.id="user:"+juce::Uuid().toString();e.deviceId=device;e.name=name.trim().substring(0,80);e.category="Personnel";e.state=std::move(state);
  if(e.name.isEmpty())return;entries.push_back(e);if(!persist()){entries.pop_back();return;}refresh(e.id);
 }
 void refreshDevices(){loadStorage();refresh();}
private:
 static void layoutRow(juce::Rectangle<int> r,std::initializer_list<juce::TextButton*> buttons){int remaining=(int)buttons.size();for(auto* button:buttons){int width=remaining>0?r.getWidth()/remaining:0;button->setBounds(r.removeFromLeft(width).reduced(2,0));--remaining;}}
 const Entry* selected()const{if(auto* item=dynamic_cast<Item*>(tree.getSelectedItem(0)))return &item->entry;return nullptr;}
 void refresh(juce::String selectId={}){
  if(selectId.isEmpty())if(auto* e=selected())selectId=e->id;
  tree.setRootItem(nullptr);root=std::make_unique<Root>();int count=0;
  const auto mode=filter.getSelectedId();
  for(const auto& e:entries){
   juce::PluginDescription d;if(!LibertyPluginHost::instance().findKnownPluginByIdentifier(e.deviceId,d))continue;
   if((mode==2&&e.factoryIndex<0)||(mode==3&&e.factoryIndex>=0)||(mode==4&&!e.favourite))continue;
   if(!LibertyBrowserIntelligence::matches(e.name+" "+e.category+" "+e.tags+" "+d.name+" "+d.manufacturerName+" "+(d.isInstrument?"Instrument":"Effet"),search.getText()))continue;
   root->addSubItem(new Item(e));++count;
  }
  tree.setRootItem(root.get());root->setOpen(true);
  for(int i=0;i<root->getNumSubItems();++i)if(root->getSubItem(i)->getUniqueName()==selectId){root->getSubItem(i)->setSelected(true,true);break;}
  status.setText(juce::String(count)+" presets"+(readOnly?" / lecture seule":""),juce::dontSendNotification);
 }
 void loadStorage(){
  entries.clear();readOnly=false;
  for(const auto& p:LibertyPluginHost::instance().getNativeFactoryPresets()){Entry e;e.id=p.id;e.name=p.name;e.category=p.category;e.deviceId=p.deviceId;e.factoryIndex=p.factoryIndex;entries.push_back(std::move(e));}
  if(!storage.existsAsFile())return;if(storage.getSize()>64000000){readOnly=true;return;}auto xml=juce::XmlDocument::parse(storage);
  if(!xml||!xml->hasTagName("LibertyPresetCenter")||xml->getIntAttribute("version")!=1){readOnly=true;return;}
  for(auto* x:xml->getChildIterator()){
   auto id=x->getStringAttribute("id");auto found=std::find_if(entries.begin(),entries.end(),[&](const Entry& e){return e.id==id;});
   if(found!=entries.end()&&found->factoryIndex>=0){found->tags=LibertyBrowserIntelligence::parseTags(x->getStringAttribute("tags")).joinIntoString(", ");found->rating=juce::jlimit(0,5,x->getIntAttribute("rating"));found->favourite=x->getBoolAttribute("favourite");}
   else{Entry e;if(userFromXml(*x,e)&&entries.size()<1100){if(!e.id.startsWith("user:"))e.id="user:"+juce::Uuid().toString();if(std::none_of(entries.begin(),entries.end(),[&](const Entry& old){return old.id==e.id;}))entries.push_back(std::move(e));}else{readOnly=true;}}
  }
 }
 bool persist(){
  if(readOnly)return false;juce::XmlElement xml("LibertyPresetCenter");xml.setAttribute("version",1);for(const auto& e:entries)xml.addChildElement(toXml(e).release());
  auto data=xml.toString();if(data.getNumBytesAsUTF8()>64000000)return false;juce::TemporaryFile temp(storage);return temp.getFile().replaceWithText(data)&&temp.overwriteTargetFileWithTemporary();
 }
 void edit(std::function<void(Entry&)> fn){if(auto* e=selected())editId(e->id,std::move(fn));}
 void editId(juce::String id,std::function<void(Entry&)> fn){if(readOnly)return;for(auto& e:entries)if(e.id==id){auto old=e;fn(e);if(!persist())e=old;refresh(id);return;}}
 void chooseImport(){
  if(readOnly)return;
  chooser=std::make_unique<juce::FileChooser>("Importer preset Liberty",juce::File{},"*.libertypreset");
  auto safe=juce::Component::SafePointer<Panel>(this);
  chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser& fc){if(!safe||safe->readOnly||safe->entries.size()>=1100)return;auto file=fc.getResult();if(!file.existsAsFile()||file.getSize()>2000000)return;auto xml=juce::XmlDocument::parse(file);Entry e;if(!xml||!userFromXml(*xml,e)){juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,"Preset Center","Preset incompatible.");return;}e.id="user:"+juce::Uuid().toString();safe->entries.push_back(e);if(!safe->persist())safe->entries.pop_back();safe->refresh(e.id);});
 }
 void chooseExport(){
  auto* e=selected();if(!e)return;auto copy=*e;juce::MemoryBlock state;if(!copy.getState(state))return;copy.state=state;copy.factoryIndex=-1;
  chooser=std::make_unique<juce::FileChooser>("Exporter preset Liberty",juce::File{},"*.libertypreset");
  auto safe=juce::Component::SafePointer<Panel>(this);
  chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[safe,copy](const juce::FileChooser& fc){if(!safe||fc.getResult()==juce::File{})return;auto file=fc.getResult().withFileExtension("libertypreset");if(!file.replaceWithText(toXml(copy)->toString()))juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,"Preset Center","Export impossible.");});
 }
 juce::File storage;Apply onApply;Capture onCapture;Apply onAudition;bool readOnly=false;std::vector<Entry> entries;
 juce::TextEditor search;juce::ComboBox filter;juce::TreeView tree;std::unique_ptr<Root> root;juce::Label status;
 juce::TextButton auditionButton,load,save,importButton,exportButton,favourite,rateButton,tagButton;std::unique_ptr<juce::FileChooser> chooser;
};
}
