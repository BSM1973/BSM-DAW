#pragma once
#include "PluginHost.h"
#include "LibertyDesignSystem.h"
#include "LibertyBrowserIntelligence.h"
#include "LibertyObsidianMenuStyle.h"
#include <juce_gui_extra/juce_gui_extra.h>
namespace LibertyDeviceChains {
struct Entry{
 juce::String id,name,category,tags;bool factory=false,favourite=false;int rating=0;
 LibertyPluginHost::NativeChain devices;
 bool needsInstrument()const{for(const auto& d:devices)if(d.slot==-1)return true;return false;}
};
inline std::unique_ptr<juce::XmlElement> encode(const Entry& e){
 auto x=std::make_unique<juce::XmlElement>("Chain");x->setAttribute("id",e.id);x->setAttribute("name",e.name);x->setAttribute("category",e.category);x->setAttribute("tags",e.tags);x->setAttribute("favourite",e.favourite?1:0);x->setAttribute("rating",e.rating);
 for(const auto& d:e.devices){auto* n=x->createNewChildElement("Device");n->setAttribute("slot",d.slot);n->setAttribute("id",d.deviceId);n->setAttribute("state",d.state.toBase64Encoding());}return x;
}
inline bool decode(const juce::XmlElement& x,Entry& e){
 if(!x.hasTagName("Chain"))return false;
 e.id=x.getStringAttribute("id");e.name=x.getStringAttribute("name").trim().substring(0,80);e.category=x.getStringAttribute("category").trim().substring(0,80);e.tags=LibertyBrowserIntelligence::parseTags(x.getStringAttribute("tags")).joinIntoString(", ");e.rating=juce::jlimit(0,5,x.getIntAttribute("rating"));e.favourite=x.getBoolAttribute("favourite");
 std::array<bool,9> used{};
 for(auto* n:x.getChildIterator()){
  if(!n->hasTagName("Device")||e.devices.size()>=9)return false;
  LibertyPluginHost::ChainDevice d;d.slot=n->getIntAttribute("slot",-2);d.deviceId=n->getStringAttribute("id");auto state=n->getStringAttribute("state");
  if(d.slot<-1||d.slot>7||used[(size_t)(d.slot+1)]||state.length()>1500000||!d.state.fromBase64Encoding(state)||!LibertyPluginHost::instance().validateNativePresetState(d.deviceId,d.state))return false;
  used[(size_t)(d.slot+1)]=true;e.devices.push_back(std::move(d));
 }
 std::sort(e.devices.begin(),e.devices.end(),[](const auto& a,const auto& b){return a.slot<b.slot;});
 return e.name.isNotEmpty()&&!e.devices.empty();
}
inline std::vector<Entry> factoryChains(){
 std::vector<Entry> out;auto& host=LibertyPluginHost::instance();
 struct Recipe{const char* name;const char* cat;int a,b;};
 const Recipe recipes[]={{"Vocal Slap & Air","Vocals",1,0},{"Guitar Slap & Tape","Guitar",10,12},{"Drum Slap & Bounce","Drums",18,20},{"Bass Slap & Echo","Bass",29,27},{"Keys Tape & Reflections","Keys",37,36},{"Ambient Dual Space","Ambient",74,72}};
 for(int i=0;i<6;++i){const auto& r=recipes[i];Entry e;e.id="factory:fsd:"+juce::String(i);e.name=r.name;e.category=r.cat;e.factory=true;
  for(int slot=0;slot<2;++slot){LibertyPluginHost::ChainDevice d;d.slot=slot;d.deviceId="liberty.native.freespirit-delay";if(host.getNativeFactoryPresetState(d.deviceId,slot==0?r.a:r.b,d.state))e.devices.push_back(std::move(d));}
  if(e.devices.size()==2)out.push_back(std::move(e));
 }return out;
}
class Item final:public juce::TreeViewItem{
public:
 explicit Item(Entry e):entry(std::move(e)){}
 Entry entry;bool mightContainSubItems()override{return false;}
 juce::String getUniqueName()const override{return entry.id;}int getItemHeight()const override{return 36;}
 void paintItem(juce::Graphics& g,int w,int h)override{
  if(isSelected())LibertyUI::fillPanel(g,juce::Rectangle<float>(1,1,(float)juce::jmax(0,w-2),(float)juce::jmax(0,h-2)),true,5.f);
  g.setColour(LibertyUI::Colour::textPrimary);g.setFont(juce::Font(12.f,juce::Font::bold));g.drawFittedText(entry.name,6,2,juce::jmax(0,w-12),17,juce::Justification::centredLeft,1);
  g.setColour(LibertyUI::Colour::textMuted);g.setFont(juce::Font(10.f));g.drawFittedText(entry.category+" / "+juce::String((int)entry.devices.size())+" FX"+(entry.favourite?" / FAVORI":"")+(entry.rating?" / "+juce::String(entry.rating)+"/5":""),6,20,juce::jmax(0,w-12),13,juce::Justification::centredLeft,1);
 }
};
class Root final:public juce::TreeViewItem{public:bool mightContainSubItems()override{return true;}juce::String getUniqueName()const override{return "LibertyChainsRoot";}};
class Panel final:public juce::Component{
public:
 using Apply=std::function<void(const Entry&)>;using Capture=std::function<void()>;
 Panel(juce::File file,Apply a,Capture c):storage(std::move(file)),apply(std::move(a)),capture(std::move(c)){
  addAndMakeVisible(search);search.setTextToShowWhenEmpty(juce::String::fromUTF8("Rechercher une chaîne..."),LibertyUI::Colour::textMuted);search.onTextChange=[this]{refresh();};
  addAndMakeVisible(filter);filter.addItem("Toutes",1);filter.addItem("Usine",2);filter.addItem("Personnelles",3);filter.addItem("Favoris",4);filter.setSelectedId(1,juce::dontSendNotification);filter.onChange=[this]{refresh();};
  addAndMakeVisible(tree);tree.setRootItemVisible(false);tree.setMultiSelectEnabled(false);tree.setColour(juce::TreeView::backgroundColourId,LibertyUI::Colour::canvasDeep);
  addAndMakeVisible(status);status.setFont(juce::Font(11.f));status.setColour(juce::Label::textColourId,LibertyUI::Colour::textMuted);
  for(auto* b:{&load,&save,&edit,&importButton,&exportButton,&details})addAndMakeVisible(*b);
  load.setButtonText(juce::String::fromUTF8("Insérer"));save.setButtonText("Capturer");edit.setButtonText("Tags / note");importButton.setButtonText("Importer");exportButton.setButtonText("Exporter");details.setButtonText(juce::String::fromUTF8("Détails"));
  load.onClick=[this]{if(auto* e=selected())apply(*e);};save.onClick=[this]{capture();};edit.onClick=[this]{editSelected();};details.onClick=[this]{showDetails();};importButton.onClick=[this]{chooseImport();};exportButton.onClick=[this]{chooseExport();};
  loadStorage();refresh();
 }
 ~Panel()override{tree.setRootItem(nullptr);}
 void resized()override{
  auto r=getLocalBounds().reduced(6);search.setBounds(r.removeFromTop(25));r.removeFromTop(4);filter.setBounds(r.removeFromTop(25));r.removeFromTop(4);status.setBounds(r.removeFromBottom(22));
  row(r.removeFromBottom(29),{&importButton,&exportButton,&save});r.removeFromBottom(4);row(r.removeFromBottom(29),{&load,&details,&edit});r.removeFromBottom(4);tree.setBounds(r);
 }
 void addCaptured(Entry e){if(readOnly||e.name.trim().isEmpty()||e.devices.empty()||entries.size()>=500)return;e.name=e.name.trim().substring(0,80);e.id="user:"+juce::Uuid().toString();e.factory=false;entries.push_back(e);if(!persist()){entries.pop_back();return;}refresh(e.id);}
private:
 static void row(juce::Rectangle<int> r,std::initializer_list<juce::TextButton*> bs){int n=(int)bs.size();for(auto* b:bs){b->setBounds(r.removeFromLeft(r.getWidth()/n).reduced(2,0));--n;}}
 const Entry* selected()const{if(auto* item=dynamic_cast<Item*>(tree.getSelectedItem(0)))return &item->entry;return nullptr;}
 void refresh(juce::String id={}){
  if(id.isEmpty())if(auto* e=selected())id=e->id;
  tree.setRootItem(nullptr);root=std::make_unique<Root>();int count=0;const auto mode=filter.getSelectedId();
  for(const auto& e:entries){if((mode==2&&!e.factory)||(mode==3&&e.factory)||(mode==4&&!e.favourite))continue;if(!LibertyBrowserIntelligence::matches(e.name+" "+e.category+" "+e.tags,search.getText()))continue;root->addSubItem(new Item(e));++count;}
  tree.setRootItem(root.get());root->setOpen(true);for(int i=0;i<root->getNumSubItems();++i)if(root->getSubItem(i)->getUniqueName()==id){root->getSubItem(i)->setSelected(true,true);break;}
  status.setText(juce::String(count)+juce::String::fromUTF8(" chaînes")+(readOnly?" / lecture seule":""),juce::dontSendNotification);
 }
 void loadStorage(){
  entries=factoryChains();if(!storage.existsAsFile())return;if(storage.getSize()>64000000){readOnly=true;return;}auto xml=juce::XmlDocument::parse(storage);
  if(!xml||!xml->hasTagName("LibertyDeviceChains")||xml->getIntAttribute("version")!=1){readOnly=true;return;}
  for(auto* n:xml->getChildIterator()){
   auto id=n->getStringAttribute("id");auto it=std::find_if(entries.begin(),entries.end(),[&](const Entry& e){return e.id==id;});
   if(it!=entries.end()&&it->factory){it->tags=LibertyBrowserIntelligence::parseTags(n->getStringAttribute("tags")).joinIntoString(", ");it->rating=juce::jlimit(0,5,n->getIntAttribute("rating"));it->favourite=n->getBoolAttribute("favourite");}
   else{Entry e;if(!decode(*n,e)||entries.size()>=500){readOnly=true;continue;}if(!e.id.startsWith("user:"))e.id="user:"+juce::Uuid().toString();if(std::none_of(entries.begin(),entries.end(),[&](const Entry& old){return old.id==e.id;}))entries.push_back(std::move(e));}
  }
 }
 bool persist(){if(readOnly)return false;juce::XmlElement xml("LibertyDeviceChains");xml.setAttribute("version",1);for(const auto& e:entries){auto x=encode(e);if(e.factory)x->deleteAllChildElements();xml.addChildElement(x.release());}juce::TemporaryFile temp(storage);return temp.getFile().replaceWithText(xml.toString())&&temp.overwriteTargetFileWithTemporary();}
 void showDetails(){
  if(auto* e=selected()){juce::String text=e->name+"\n";for(const auto& d:e->devices){juce::PluginDescription desc;LibertyPluginHost::instance().findKnownPluginByIdentifier(d.deviceId,desc);text+=(d.slot<0?juce::String("Instrument"):"FX "+juce::String(d.slot+1))+" : "+desc.name+"\n";}juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,"Device Chains",text);}
 }
 void editSelected(){
  auto* e=selected();if(!e||readOnly)return;auto id=e->id;auto dlg=std::make_shared<juce::AlertWindow>("Device Chains",e->name,juce::AlertWindow::NoIcon);dlg->addTextEditor("tags",e->tags,"Tags");
  juce::StringArray ratings{"Sans note","1/5","2/5","3/5","4/5","5/5"};dlg->addComboBox("rating",ratings,"Note");dlg->getComboBoxComponent("rating")->setSelectedItemIndex(e->rating,juce::dontSendNotification);dlg->addComboBox("fav",juce::StringArray{"Non","Oui"},"Favori");dlg->getComboBoxComponent("fav")->setSelectedItemIndex(e->favourite?1:0,juce::dontSendNotification);dlg->addButton("Enregistrer",1);dlg->addButton("Annuler",0);
  auto safe=juce::Component::SafePointer<Panel>(this);dlg->enterModalState(true,juce::ModalCallbackFunction::create([safe,dlg,id](int n){if(!safe||n!=1)return;for(auto& entry:safe->entries)if(entry.id==id){auto old=entry;entry.tags=LibertyBrowserIntelligence::parseTags(dlg->getTextEditorContents("tags")).joinIntoString(", ");entry.rating=juce::jlimit(0,5,dlg->getComboBoxComponent("rating")->getSelectedItemIndex());entry.favourite=dlg->getComboBoxComponent("fav")->getSelectedItemIndex()==1;if(!safe->persist())entry=old;safe->refresh(id);return;}}),false);
 }
 void chooseImport(){
  if(readOnly)return;chooser=std::make_unique<juce::FileChooser>("Importer Device Chain",juce::File{},"*.libertychain");auto safe=juce::Component::SafePointer<Panel>(this);
  chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser& fc){if(!safe)return;auto file=fc.getResult();if(!file.existsAsFile()||file.getSize()>15000000)return;auto xml=juce::XmlDocument::parse(file);Entry e;if(!xml||!decode(*xml,e)){juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,"Device Chains",juce::String::fromUTF8("Chaîne incompatible."));return;}safe->addCaptured(std::move(e));});
 }
 void chooseExport(){
  auto* e=selected();if(!e)return;auto copy=*e;chooser=std::make_unique<juce::FileChooser>("Exporter Device Chain",juce::File{},"*.libertychain");auto safe=juce::Component::SafePointer<Panel>(this);
  chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[safe,copy](const juce::FileChooser& fc){if(!safe||fc.getResult()==juce::File{})return;auto file=fc.getResult().withFileExtension("libertychain");if(!file.replaceWithText(encode(copy)->toString()))juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,"Device Chains","Export impossible.");});
 }
 juce::File storage;Apply apply;Capture capture;bool readOnly=false;std::vector<Entry> entries;juce::TextEditor search;juce::ComboBox filter;juce::TreeView tree;std::unique_ptr<Root> root;juce::Label status;juce::TextButton load,save,edit,importButton,exportButton,details;std::unique_ptr<juce::FileChooser> chooser;
};
}
