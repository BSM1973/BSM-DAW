#include "../Source/ChannelABOverview.h"
#include <cassert>
#include <iostream>

using LibertyChannelABOverview::Counts;
using LibertyChannelABOverview::sectionFromMenu;

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

int main()
{
    Counts counts{{2,0,1,4}};
    assert(counts.total()==7);
    assert(!counts.identical());
    for(int section=0;section<LibertyChannelABOverview::sectionCount;++section)
        assert(sectionFromMenu(LibertyChannelABOverview::openSectionBase+section)==section);
    assert(sectionFromMenu(-1)==-1);
    std::cout<<"CANAL A/B overview: section counts, total and menu routing OK\n";
}
