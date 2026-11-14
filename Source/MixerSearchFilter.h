#pragma once
#include <cstddef>
#include <string_view>

// Pure C++17 parser shared by mixer search and CI tests. Only ASCII
// prefixes are interpreted. The remaining UTF-8 track name is passed
// unchanged to JUCE's Unicode-aware containsIgnoreCase().
namespace LibertyMixerSearch {
enum class Kind {audio,midi,instrument,bus,master};
enum class Category {all,audio,midi,instrument,bus,master};
struct Filter
{
    Category category=Category::all;
    std::size_t prefixLength=0;
};
constexpr char lowerAscii(char c) noexcept
{
    return c>='A'&&c<='Z'?static_cast<char>(c-'A'+'a'):c;
}
inline bool samePrefix(std::string_view a,std::string_view b) noexcept
{
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)
        if(lowerAscii(a[i])!=b[i])return false;
    return true;
}
inline Filter parse(std::string_view query) noexcept
{
    const auto colon=query.find(':');
    if(colon==std::string_view::npos)return {};
    auto word=query.substr(0,colon);
    while(!word.empty()&&word.front()==' ')word.remove_prefix(1);
    while(!word.empty()&&word.back()==' ')word.remove_suffix(1);
    if(samePrefix(word,"a")||samePrefix(word,"audio"))
        return {Category::audio,colon+1};
    if(samePrefix(word,"m")||samePrefix(word,"midi"))
        return {Category::midi,colon+1};
    if(samePrefix(word,"i")||samePrefix(word,"inst")||
       samePrefix(word,"instrument"))
        return {Category::instrument,colon+1};
    if(samePrefix(word,"b")||samePrefix(word,"bus"))
        return {Category::bus,colon+1};
    if(samePrefix(word,"master"))
        return {Category::master,colon+1};
    return {}; // Unknown prefixes remain normal track-name searches.
}
// direction = 0: first match, +1: next, -1: previous.
// Always wraps around; no match returns -1. Predicate receives the
// unchanged strip index, so selection and plug-in routing stay stable.
template<typename Predicate>
inline int findMatchingIndex(int count,int current,int direction,
                             Predicate matches)
{
    if(count<=0)return -1;
    const bool backwards=direction<0;
    int first=0;
    if(direction!=0)
    {
        first=current>=0&&current<count?
            (current+(backwards?count-1:1))%count:
            (backwards?count-1:0);
    }
    for(int step=0;step<count;++step)
    {
        const int index=backwards?(first+count-step)%count:
                                  (first+step)%count;
        if(matches(index))return index;
    }
    return -1;
}
constexpr bool accepts(Category category,Kind kind) noexcept
{
    return category==Category::all||
        (category==Category::audio&&kind==Kind::audio)||
        (category==Category::midi&&kind==Kind::midi)||
        (category==Category::instrument&&kind==Kind::instrument)||
        (category==Category::bus&&kind==Kind::bus)||
        (category==Category::master&&kind==Kind::master);
}
}
