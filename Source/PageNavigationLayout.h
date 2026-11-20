#pragma once

// One shared geometry for the main page tabs and the Browser toggle.
// Maintaining these positions in a single location prevents overlap.
namespace LibertyPageNavigation
{
constexpr int startX = 205;
constexpr int top = 4;
constexpr int height = 25;
constexpr int gap = 4;
constexpr int arrangeWidth = 120;
constexpr int mixerWidth = 152;
constexpr int performWidth = 118;
constexpr int browserWidth = 116;
constexpr int arrangeX = startX;
constexpr int mixerX = arrangeX + arrangeWidth + gap;
constexpr int performX = mixerX + mixerWidth + gap;
constexpr int browserX = performX + performWidth + gap;
static_assert(arrangeX + arrangeWidth + gap <= mixerX, "Arrangement tab overlap");
static_assert(mixerX + mixerWidth + gap <= performX, "Mixer tab overlap");
static_assert(performX + performWidth + gap <= browserX, "Browser tab overlap");
}
