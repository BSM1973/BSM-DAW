#pragma once
#include <array>

namespace LibertyChannelDialogLayout {
struct Rect {
    int x=0, y=0, w=0, h=0;
    constexpr int right() const { return x+w; }
    constexpr int bottom() const { return y+h; }
    constexpr bool contains(Rect other)const {
        return other.x>=x && other.y>=y &&
               other.right()<=right() && other.bottom()<=bottom();
    }
    constexpr bool intersects(Rect other)const {
        return x<other.right() && right()>other.x &&
               y<other.bottom() && bottom()>other.y;
    }
};
struct Layout {
    std::array<Rect,4> sections;
    std::array<Rect,8> controls;
    std::array<Rect,3> powers;
    std::array<Rect,3> actions;
    std::array<Rect,2> clipboard;
    Rect variant;
};
// The fixed-size, dark-blue Channel Inspector has a deterministic geometry.
// This geometry is reused by the JUCE UI and a dedicated CI no-overlap test.
constexpr Layout make(int width=760,int height=520){
    Layout l{};
    const int margin=20, gap=14;
    const int pw=(width-2*margin-gap)/2;
    const int right=margin+pw+gap;
    l.sections={{{margin,82,pw,168},{right,82,pw,168},
                 {margin,262,pw,170},{right,262,pw,170}}};
    l.controls={{{margin+33,146,108,93},    // input gain
                 {margin+184,146,108,93},   // input high-pass
                 {right+122,146,108,93},    // gate threshold
                 {margin+33,326,108,93},    // compressor threshold
                 {margin+184,326,108,93},   // compressor ratio
                 {right+17,326,101,93},    // EQ low
                 {right+127,326,101,93},   // EQ mid
                 {right+237,326,101,93}}}; // EQ high
    l.powers={{{right+pw-113,95,97,25},    // gate enabled
               {margin+pw-113,275,97,25}, // compressor enabled
               {right+pw-113,275,97,25}}};// EQ enabled
    l.actions={{{20,height-58,150,34},       // reset
                {width-247,height-58,104,34}, // cancel
                {width-132,height-58,112,34}}}; // apply
    l.clipboard={{{190,height-58,110,34},   // copy
                  {314,height-58,110,34}}}; // paste
    l.variant={435,height-58,66,34};        // A/B variant, between paste and cancel
    return l;
}
}
