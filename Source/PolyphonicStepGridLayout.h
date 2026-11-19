#pragma once
// JUCE-free layout contract shared by drawing, hit testing and CI tests.
#include <algorithm>
#include <array>

namespace LibertyPolyGridUI
{
struct Rect
{
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const noexcept
    { return w > 0 && h > 0 && px >= x && py >= y && px < x + w && py < y + h; }
    bool intersects(const Rect& other) const noexcept
    {
        return x < other.x + other.w && x + w > other.x
            && y < other.y + other.h && y + h > other.y;
    }
};
struct Layout
{
    bool valid = false;
    bool compact = false;
    Rect clipPanel, patternHeader, gridHeader, parameterHeader, editorArea;
    Rect createClip, classicMode, pianoExport, pianoImport, undoButton, redoButton;
    Rect editMinus, editPlus, editValue;
    std::array<Rect, 8> banks {}, pitchLabels {}, muteButtons {}, soloButtons {};
    std::array<std::array<Rect, 4>, 8> rowControls {};
    std::array<std::array<Rect, 16>, 8> cells {};
    std::array<Rect, 6> editorTabs {};
    int firstStep = 0, rowHeight = 0, gridLeft = 0, gridWidth = 0;

    static Layout make(int width, int height, int page = 0)
    {
        Layout l;
        if (width < 500 || height < 212) return l;
        const int margin = 8;
        l.compact = width < 920 || height < 370;
        const int clipWidth = l.compact ? std::clamp(width / 7, 92, 120)
                                        : std::clamp(width / 6, 155, 206);
        const int paramWidth = l.compact ? 167 : 262;
        const int footerHeight = l.compact ? 48 : 78;
        const int rowsTop = 64;
        const int rowsBottom = height - margin - footerHeight;
        l.rowHeight = (rowsBottom - rowsTop) / 8;
        l.gridLeft = margin + clipWidth + paramWidth + 6;
        l.gridWidth = width - margin - l.gridLeft;
        if (l.gridWidth < 220 || l.rowHeight < 12) return l;
        l.firstStep = std::clamp(page, 0, 3) * 16;
        l.clipPanel = { margin, 35, clipWidth - 5, rowsBottom - 35 };
        l.createClip = { margin + 6, std::max(76, rowsBottom - 29), clipWidth - 17, 23 };
        l.classicMode = { margin + 6, rowsBottom - 55, clipWidth - 17, 21 };
        const int exchangeY = l.compact ? 88 : 189;
        const int exchangeW = (clipWidth - 20) / 2;
        l.pianoExport = {margin + 6, exchangeY, exchangeW, 20};
        l.pianoImport = {margin + 8 + exchangeW, exchangeY,
                         clipWidth - 19 - exchangeW, 20};
        const int paramX = margin + clipWidth;
        const int undoW = (paramWidth - 12) / 2;
        l.undoButton = {paramX, margin, undoW, 24};
        l.redoButton = {paramX + undoW + 4, margin,
                        paramWidth - undoW - 12, 24};
        l.patternHeader = { l.gridLeft, margin, l.gridWidth, 26 };
        l.gridHeader = { l.gridLeft, 36, l.gridWidth, 26 };
        l.parameterHeader = { paramX, 36, paramWidth - 6, 26 };
        const int bankW = l.gridWidth / 8;
        for (int b = 0; b < 8; ++b)
            l.banks[(size_t)b] = { l.gridLeft + b * bankW, margin,
                                    b == 7 ? l.gridWidth - b * bankW - 2 : bankW - 2, 26 };
        const int pitchWidth = l.compact ? 34 : 65;
        const int auditionWidth = 18;
        const int controlsX = paramX + pitchWidth + auditionWidth * 2 + 2;
        const int controlsWidth = l.gridLeft - 3 - controlsX;
        const int columns[4] = { 26, 34, 26, 40 };
        const int sum = 126;
        for (int row = 0; row < 8; ++row)
        {
            const int y = rowsTop + row * l.rowHeight;
            l.pitchLabels[(size_t)row] = { paramX, y + 1, pitchWidth, l.rowHeight - 2 };
            l.muteButtons[(size_t)row] = { paramX + pitchWidth, y + 1, auditionWidth - 1, l.rowHeight - 2 };
            l.soloButtons[(size_t)row] = { paramX + pitchWidth + auditionWidth, y + 1, auditionWidth - 1, l.rowHeight - 2 };
            int running = controlsX;
            for (int col = 0; col < 4; ++col)
            {
                const int w = col == 3 ? std::max(16, controlsX + controlsWidth - running)
                                       : std::max(14, controlsWidth * columns[col] / sum);
                l.rowControls[(size_t)row][(size_t)col] =
                    { running, y + 1, w - 2, l.rowHeight - 2 };
                running += w;
            }
            for (int step = 0; step < 16; ++step)
            {
                const int x0 = l.gridLeft + step * l.gridWidth / 16;
                const int x1 = l.gridLeft + (step + 1) * l.gridWidth / 16;
                l.cells[(size_t)row][(size_t)step] =
                    { x0, y + 1, std::max(1, x1 - x0 - 1), l.rowHeight - 2 };
            }
        }
        const int footerY = rowsTop + 8 * l.rowHeight + 4;
        l.editorArea = { paramX, footerY, width - margin - paramX,
                         height - margin - footerY };
        const int tabGap = 3;
        const int each = (l.editorArea.w - 5 * tabGap) / 6;
        for (int i = 0; i < 6; ++i)
            l.editorTabs[(size_t)i] = { paramX + i * (each + tabGap), footerY,
                                           i == 5 ? l.editorArea.w - i * (each + tabGap)
                                                  : each, l.compact ? 19 : 24 };
        const int toolY = footerY + (l.compact ? 19 : 24) + 3;
        const int toolH = std::max(0, height - margin - toolY - 1);
        const int buttonWidth = l.compact ? 25 : 38;
        l.editMinus = {paramX + 2, toolY, buttonWidth, toolH};
        l.editPlus = {paramX + l.editorArea.w - buttonWidth - 2,
                      toolY, buttonWidth, toolH};
        l.editValue = {l.editMinus.x + buttonWidth + 5, toolY,
                       std::max(0, l.editPlus.x - l.editMinus.x - buttonWidth - 10), toolH};
        l.valid = true;
        return l;
    }
};
} // namespace LibertyPolyGridUI
