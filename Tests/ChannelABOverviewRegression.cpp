#include "../Source/ChannelABOverview.h"
#include <cassert>
#include <iostream>

using LibertyChannelABOverview::Counts;
using LibertyChannelABOverview::sectionFromMenu;
using LibertyChannelABOverview::CommandKind;
using LibertyChannelABOverview::decodeCommand;

static_assert(Counts{}.total()==0);
static_assert(Counts{}.identical());
static_assert(Counts{{2,1,3,4}}.total()==10);
static_assert(!Counts{{2,1,3,4}}.identical());
static_assert(Counts{{0,0,-1,0}}.identical());
static_assert(sectionFromMenu(5210)==0);
static_assert(sectionFromMenu(5211)==1);
static_assert(sectionFromMenu(5212)==2);
static_assert(sectionFromMenu(5213)==3);
static_assert(sectionFromMenu(5200)==-1);
static_assert(sectionFromMenu(5201)==-1);
static_assert(sectionFromMenu(5214)==-1);
static_assert(Counts{{1,0,2,0}}.canCopySection(0));
static_assert(!Counts{{1,0,2,0}}.canCopySection(1));
static_assert(Counts{{1,0,2,0}}.canCopySection(2));
static_assert(!Counts{{1,0,2,0}}.canCopySection(3));
static_assert(!Counts{{1,0,2,0}}.canCopySection(-1));
static_assert(!Counts{{1,0,2,0}}.canCopySection(4));
static_assert(decodeCommand(5210).kind==CommandKind::details);
static_assert(decodeCommand(5213).section==3);
static_assert(decodeCommand(5220).kind==CommandKind::sectionAToB);
static_assert(decodeCommand(5223).section==3);
static_assert(decodeCommand(5230).kind==CommandKind::sectionBToA);
static_assert(decodeCommand(5233).section==3);
static_assert(decodeCommand(5240).kind==CommandKind::allAToB);
static_assert(decodeCommand(5241).kind==CommandKind::allBToA);
static_assert(decodeCommand(5240).section==-1);
static_assert(decodeCommand(5200).kind==CommandKind::none);
static_assert(decodeCommand(5201).kind==CommandKind::none);
static_assert(decodeCommand(5224).kind==CommandKind::none);
static_assert(decodeCommand(5234).kind==CommandKind::none);
static_assert(decodeCommand(5242).kind==CommandKind::none);
static_assert(LibertyChannelABOverview::needsFullCopyConfirmation(CommandKind::allAToB));
static_assert(LibertyChannelABOverview::needsFullCopyConfirmation(CommandKind::allBToA));
static_assert(!LibertyChannelABOverview::needsFullCopyConfirmation(CommandKind::details));
static_assert(!LibertyChannelABOverview::needsFullCopyConfirmation(CommandKind::sectionAToB));
static_assert(!LibertyChannelABOverview::needsFullCopyConfirmation(CommandKind::sectionBToA));
static_assert(!LibertyChannelABOverview::needsFullCopyConfirmation(CommandKind::none));
static_assert(Counts{}.affectedSections()==0);
static_assert(Counts{{2,0,3,0}}.affectedSections()==2);
static_assert(Counts{{0,1,0,4}}.affectedSections()==2);
static_assert(Counts{{2,1,3,4}}.affectedSections()==4);


int main()
{
    Counts counts{{2,0,1,4}};
    assert(counts.total()==7);
    assert(counts.affectedSections()==3);
    assert(LibertyChannelABOverview::needsFullCopyConfirmation(
        decodeCommand(LibertyChannelABOverview::copyAllAToB).kind));
    assert(LibertyChannelABOverview::needsFullCopyConfirmation(
        decodeCommand(LibertyChannelABOverview::copyAllBToA).kind));
    assert(!counts.identical());
    for(int section=0;section<LibertyChannelABOverview::sectionCount;++section)
        assert(sectionFromMenu(LibertyChannelABOverview::openSectionBase+section)==section);
    assert(sectionFromMenu(-1)==-1);
    for(int section=0;section<LibertyChannelABOverview::sectionCount;++section)
    {
        const auto ab=decodeCommand(LibertyChannelABOverview::copySectionAToBBase+section);
        const auto ba=decodeCommand(LibertyChannelABOverview::copySectionBToABase+section);
        assert(ab.kind==CommandKind::sectionAToB && ab.section==section);
        assert(ba.kind==CommandKind::sectionBToA && ba.section==section);
        assert(counts.canCopySection(section)==(counts.bySection[static_cast<unsigned>(section)]>0));
    }
    assert(decodeCommand(-1).kind==CommandKind::none);
    std::cout<<"CANAL A/B overview: section counts, total and menu routing OK\n";
}
