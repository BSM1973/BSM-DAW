// Liberty Step Sequencer v2 (Phase 2) — shared eight-lane polyphonic editor.
// All coordinates come from PolyphonicStepGridLayout.h, so paint and hit tests
// cannot drift apart when the dock or floating window changes size.
#include "PolyphonicStepGridLayout.h"

namespace
{
juce::Rectangle<int> polyRect(const LibertyPolyGridUI::Rect& r)
{ return {r.x, r.y, r.w, r.h}; }

juce::String polyNoteName(int pitch)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F",
                                    "F#", "G", "G#", "A", "A#", "B" };
    const int safe = juce::jlimit(0, 127, pitch);
    // Same octave names as the reference: MIDI 60 = C3, 72 = C4.
    return juce::String(names[safe % 12]) + juce::String(safe / 12 - 2);
}
bool polyHasLegacyNotes(const LibertyStepSequencer::Pattern& p)
{
    return std::any_of(p.steps.begin(), p.steps.end(),
                       [](const auto& event) { return event.enabled; });
}
bool polyShowNewGrid(const LibertyStepSequencer::Pattern& p)
{
    // Legacy patterns keep their complete existing editor and sound. New
    // empty patterns immediately show the eight-line editor; no migration.
    return p.polyGrid.active || !polyHasLegacyNotes(p);
}
constexpr juce::uint32 orange = 0xffed963c;
constexpr juce::uint32 cyan = 0xff65e3ee;
constexpr juce::uint32 white = 0xffecf6fc;
constexpr juce::uint32 dim = 0xff9fb7c6;

void polyText(juce::Graphics& g, const juce::String& text,
              juce::Rectangle<int> r, float size, juce::Colour colour,
              juce::Justification justification = juce::Justification::centred,
              bool bold = false)
{
    if (r.getWidth() < 1 || r.getHeight() < 1) return;
    g.setColour(colour);
    g.setFont(juce::Font(size, bold ? juce::Font::bold : juce::Font::plain));
    g.drawFittedText(text, r.reduced(1, 0), justification, 1, 0.70f);
}
void polyChip(juce::Graphics& g, const LibertyPolyGridUI::Rect& rect,
              juce::String text, bool selected, float fontSize)
{
    const auto r = polyRect(rect);
    g.setColour(juce::Colour(selected ? 0xff24505a : 0xff1b2a3a));
    g.fillRoundedRectangle(r.toFloat(), 3.0f);
    g.setColour(juce::Colour(selected ? cyan : 0xff405768));
    g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 3.0f, 0.8f);
    polyText(g, text, r, fontSize,
             juce::Colour(selected ? white : dim), juce::Justification::centred, selected);
}
} // namespace

void MainComponent::drawPolyphonicStepSequencer(juce::Graphics& g, juce::Rectangle<int> area,
    const LibertyStepSequencer::Pattern& pattern, int activeBank, bool isMidi)
{
    const juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(area);
    g.addTransform(juce::AffineTransform::translation((float)area.getX(), (float)area.getY()));
    const auto l = LibertyPolyGridUI::Layout::make(area.getWidth(), area.getHeight(), stepSequencerPage);
    g.fillAll(juce::Colour(0xff0d1926));
    if (!l.valid)
    {
        polyText(g, juce::String::fromUTF8("Ouvrez le Step Sequencer en fenêtre détachée"),
                 {10, 10, area.getWidth() - 20, area.getHeight() - 20}, 12.0f,
                 juce::Colour(dim));
        return;
    }

    const auto& grid = pattern.polyGrid;
    const int trackIndex = isMidi ? selectedTrack - getAudioTrackCount()
                                  : selectedTrack - getAudioTrackCount() - getMidiTrackCount();
    const int logical = isMidi ? getAudioTrackCount() + trackIndex
                               : getAudioTrackCount() + getMidiTrackCount() + trackIndex;
    const auto foundEditor = polyGridEditors.find(std::make_pair(logical, activeBank));
    const LibertyPolyGridEditing* editor =
        foundEditor != polyGridEditors.end() ? &foundEditor->second : nullptr;
    const int panelX = l.clipPanel.x;
    const auto panelR = polyRect(l.clipPanel);
    g.setColour(juce::Colour(0xff172636));
    g.fillRoundedRectangle(panelR.toFloat(), 5.0f);
    g.setColour(juce::Colour(0xff34546b));
    g.drawRoundedRectangle(panelR.toFloat().reduced(0.5f), 5.0f, 1.0f);

    polyText(g, "CLIP MIDI", {panelX+6, 39, l.clipPanel.w-12, 22},
             l.compact ? 10.0f : 13.0f, juce::Colour(cyan), juce::Justification::centredLeft, true);
    polyText(g, juce::String(grid.stepCount) + " PAS",
             {panelX+6, 61, l.clipPanel.w-12, 20}, l.compact ? 10.0f : 12.0f,
             juce::Colour(white), juce::Justification::centredLeft, true);
    const int statsY = l.compact ? 83 : 93;
    polyText(g, "BOUCLE : OUI", {panelX+6, statsY, l.clipPanel.w-12, 18},
             l.compact ? 9.0f : 11.0f, juce::Colour(dim), juce::Justification::centredLeft);
    if (!l.compact)
        polyText(g, "PAGE " + juce::String(stepSequencerPage+1) + " / 4",
                 {panelX+6, statsY+21, l.clipPanel.w-12, 18},
                 11.0f, juce::Colour(white), juce::Justification::centredLeft);
    int count = 0;
    for (const auto& row : grid.lanes) count += (int)row.events.size();
    if (!l.compact)
    {
        polyText(g, juce::String(count) + (count == 1 ? " NOTE" : " NOTES"),
                 {panelX+6, statsY+45, l.clipPanel.w-12, 20}, 11.0f, juce::Colour(dim),
                 juce::Justification::centredLeft);
        polyText(g, isMidi ? "PISTE MIDI" : "PISTE INSTRUMENT",
                 {panelX+6, statsY+68, l.clipPanel.w-12, 18}, 10.0f, juce::Colour(dim),
                 juce::Justification::centredLeft);
    }
    const bool hasNotes = grid.hasNotes();
    polyChip(g, l.classicMode,
             l.compact ? "PAGE " + juce::String(stepSequencerPage+1) + " / 4"
                       : juce::String("PAGE SUIV."),
             false, l.compact ? 8.3f : 10.0f);
    polyChip(g, l.createClip, hasNotes ? "CRÉER CLIP" : "CLIP VIDE",
             hasNotes, l.compact ? 8.3f : 10.0f);

    // Undo/Redo controls occupy the top of the parameter panel, not the grid.
    polyChip(g, l.undoButton, "ANNULER", editor && editor->canUndo(),
             l.compact ? 8.2f : 10.0f);
    polyChip(g, l.redoButton, "RÉTABLIR", editor && editor->canRedo(),
             l.compact ? 8.2f : 10.0f);

    // Eight retained pattern banks, shared with the legacy Step Sequencer.
    for (int b = 0; b < 8; ++b)
        polyChip(g, l.banks[(size_t)b], "P" + juce::String(b + 1),
                 b == activeBank, l.compact ? 9.0f : 11.0f);

    const int paramX = l.parameterHeader.x;
    polyText(g, "NOTE  M S", {paramX, 36, l.compact ? 70 : 102, 26},
             l.compact ? 8 : 10, juce::Colour(dim), juce::Justification::centredLeft, true);
    const char* headings[4] = { "PAS", "RATE", "DIR", "SWING" };
    for (int c = 0; c < 4; ++c)
    {
        const auto r = l.rowControls[0][(size_t)c];
        polyText(g, headings[c], {r.x, 37, r.w, 23},
                 l.compact ? 7.8f : 10.0f, juce::Colour(dim),
                 juce::Justification::centred, true);
    }
    for (int i = 0; i < 16; ++i)
    {
        const auto r = l.cells[0][(size_t)i];
        if (i % 4 == 0)
            polyText(g, juce::String(l.firstStep+i+1), {r.x, 37, r.w+1, 23},
                     l.compact ? 9.0f : 11.0f, juce::Colour(white), juce::Justification::centred);
    }

    const juce::Colour rowColours[8] = {
        juce::Colour(0xff715bb4),juce::Colour(0xff498b88),
        juce::Colour(0xff5fa26b),juce::Colour(0xff4f8aa7),
        juce::Colour(0xff6ca865),juce::Colour(0xff448ba2),
        juce::Colour(0xff438db0),juce::Colour(0xff8764bf)
    };
    for (int row = 0; row < LibertyPolyphonicGrid::laneCount; ++row)
    {
        const auto& lane = grid.lanes[(size_t)row];
        const auto labelR = polyRect(l.pitchLabels[(size_t)row]);
        const bool silent = !lane.enabled || lane.muted;
        g.setColour(rowColours[row].withMultipliedAlpha(silent ? 0.35f : 0.90f));
        g.fillRoundedRectangle(labelR.toFloat(), 2.5f);
        polyText(g, polyNoteName(lane.pitch), labelR, l.compact ? 9.0f : 11.5f,
                 juce::Colour(white), juce::Justification::centred, true);
        polyChip(g, l.muteButtons[(size_t)row], "M", lane.muted, l.compact ? 8 : 10);
        polyChip(g, l.soloButtons[(size_t)row], "S", lane.solo, l.compact ? 8 : 10);
        const juce::String values[4] = {
            juce::String(lane.steps),
            lane.rate == 1 ? "1/8" : lane.rate == 2 ? "1/32" : "1/16",
            lane.reverse ? "AR" : "AV",
            juce::String(lane.swing) + "%"
        };
        for (int c = 0; c < 4; ++c)
            polyChip(g, l.rowControls[(size_t)row][(size_t)c],
                     values[c], false, l.compact ? 8.2f : 10.0f);

        for (int step = 0; step < 16; ++step)
        {
            const int absoluteStep = l.firstStep + step;
            const auto r = polyRect(l.cells[(size_t)row][(size_t)step]);
            const bool inCycle = absoluteStep < grid.stepCount;
            g.setColour(juce::Colour(!inCycle ? 0xff101b28
                        : (step % 4 == 0 ? 0xff293e51 : 0xff213345)));
            g.fillRoundedRectangle(r.toFloat(), 2.0f);
            if (step % 4 == 0)
            {
                g.setColour(juce::Colour(0xff527080).withAlpha(0.5f));
                g.drawVerticalLine(r.getX(), (float)r.getY(), (float)r.getBottom());
            }
        }
        for (const auto& note : lane.events)
        {
            const auto start = note.startStep;
            const auto endStep = (std::int64_t)start +
                (note.lengthTicks + grid.stepTicks - 1) / grid.stepTicks;
            if (start >= l.firstStep + 16 || endStep <= l.firstStep || start >= grid.stepCount)
                continue;
            const int visibleStart = juce::jmax(l.firstStep, start);
            const int visibleEnd = (int)juce::jmin<std::int64_t>(
                l.firstStep + 16, juce::jmin<std::int64_t>(grid.stepCount, endStep));
            const int x0 = l.gridLeft + (visibleStart - l.firstStep) * l.gridWidth / 16;
            const int x1 = l.gridLeft + (visibleEnd - l.firstStep) * l.gridWidth / 16;
            const int y = l.cells[(size_t)row][0].y + 1;
            const int h = juce::jmax(2, l.rowHeight - 4);
            const auto noteR = juce::Rectangle<float>((float)x0+1.0f, (float)y,
                                                      (float)juce::jmax(2,x1-x0-3), (float)h);
            g.setColour(juce::Colour(note.muted || silent ? 0xff6f6c63 : orange));
            g.fillRoundedRectangle(noteR, 3.0f);
            const bool isSelected = editor
                ? editor->contains({row, note.id})
                : (row == polyGridSelectedLane && note.id == polyGridSelectedEvent);
            if (isSelected)
            {
                g.setColour(juce::Colour(cyan));
                g.drawRoundedRectangle(noteR.reduced(0.5f), 3.0f, 2.0f);
            }
        }
    }
    const char* editors[6] = { "VÉLOCITÉ", "DÉCALAGE", "DURÉE",
                                "RÉPÉTITION", "HAUTEUR", "CHANCE" };
    for (int i = 0; i < 6; ++i)
        polyChip(g, l.editorTabs[(size_t)i], juce::String::fromUTF8(editors[i]),
                 polyGridEditorTab == i, l.compact ? 8.0f : 10.0f);
    // All six editors are live. +/- applies relative changes to every selected
    // note, while horizontal dragging continues the same Undo transaction.
    const LibertyPolyphonicGrid::Event* selected = nullptr;
    int selectedCount = 0;
    if (editor)
    {
        selectedCount = (int)editor->selection().size();
        if (!editor->selection().empty())
            selected = LibertyPolyGridEditing::find(grid, editor->selection().front());
    }
    juce::String detail = "SÉLECTIONNER UNE NOTE";
    if (selected)
    {
        switch (polyGridEditorTab)
        {
            case 0: detail = juce::String(selected->velocity) + " / 127"; break;
            case 1: detail = juce::String(selected->offsetTicks) + " TICKS"; break;
            case 2: detail = juce::String(selected->lengthTicks) + " TICKS"; break;
            case 3: detail = juce::String(selected->repeats) + " FOIS"; break;
            case 4: detail = (selected->pitchOffset >= 0 ? "+" : "")
                            + juce::String(selected->pitchOffset) + " DEMI-TONS"; break;
            case 5: detail = juce::String(selected->probability) + " %"; break;
        }
        if (selectedCount > 1)
            detail = juce::String(selectedCount) + " NOTES  |  " + detail;
    }
    polyChip(g, l.editMinus, "-", false, l.compact ? 11.0f : 16.0f);
    polyChip(g, l.editPlus, "+", false, l.compact ? 11.0f : 16.0f);
    polyText(g, detail, polyRect(l.editValue), l.compact ? 9.0f : 11.0f,
             juce::Colour(selected ? white : dim), juce::Justification::centred, true);
    if (polyGesture.active && polyGesture.mode == PolyGridGesture::Mode::Marquee &&
        polyGesture.area.getWidth() == area.getWidth() &&
        polyGesture.area.getHeight() == area.getHeight())
    {
        const int startX = polyGesture.down.x - area.getX();
        const int startY = polyGesture.down.y - area.getY();
        const auto relative = polyGesture.current - polyGesture.area.getPosition();
        const auto r = juce::Rectangle<int>(
            juce::jmin(startX, relative.x), juce::jmin(startY, relative.y),
            juce::jmax(1, std::abs(startX - relative.x)),
            juce::jmax(1, std::abs(startY - relative.y)));
        g.setColour(juce::Colour(cyan).withAlpha(0.12f));
        g.fillRect(r);
        g.setColour(juce::Colour(cyan).withAlpha(0.75f));
        g.drawRect(r, 1);
    }
}

bool MainComponent::clickPolyphonicStepSequencer(juce::Point<int> point, juce::Rectangle<int> area,
    LibertyStepSequencer::Pattern& pattern, bool isMidi, int trackIndex)
{
    if (!area.contains(point)) return false;
    const auto l = LibertyPolyGridUI::Layout::make(area.getWidth(), area.getHeight(), stepSequencerPage);
    if (!l.valid) return true;
    const auto local = point - area.getPosition();
    auto hit = [&](const LibertyPolyGridUI::Rect& r) { return r.contains(local.x, local.y); };
    auto& grid = pattern.polyGrid;
    auto& activePattern = isMidi ? midiStepSequencers[(size_t)trackIndex].activePattern
                                 : instrumentStepSequencers[(size_t)trackIndex].activePattern;
    auto publish = [&]()
    {
        pattern.enabled = grid.hasNotes();
        if (!isMidi) publishInstrumentStepSequencer(trackIndex);
        repaint();
    };
    if (hit(l.createClip))
    {
        if (grid.hasNotes())
        {
            if (isMidi)
            {
                const auto notes = LibertyStepSequencer::render(pattern);
                const auto length = LibertyStepSequencer::getCycleLengthTicks(pattern);
                if (!notes.empty() && length > 0)
                    commitLibertySequencerMidiClip(*this, notes, trackIndex, playheadSeconds,
                       juce::jmax(0.01, MidiEngine::tickToSeconds(length, tempoBpm)));
            }
            else
                createInstrumentPatternClip(trackIndex);
        }
        repaint();
        return true;
    }
    if (hit(l.classicMode))
    {
        stepSequencerPage = (stepSequencerPage + 1) % 4;
        repaint();
        return true;
    }
    for (int i = 0; i < 8; ++i)
        if (hit(l.banks[(size_t)i]))
        {
            activePattern = i;
            stepSequencerPage = 0;
            polyGridSelectedLane = -1;
            polyGridSelectedEvent = 0;
            if (!isMidi) publishInstrumentStepSequencer(trackIndex);
            repaint();
            return true;
        }
    for (int i = 0; i < 6; ++i)
        if (hit(l.editorTabs[(size_t)i]))
        {
            polyGridEditorTab = i;
            repaint();
            return true;
        }
    for (int row = 0; row < LibertyPolyphonicGrid::laneCount; ++row)
    {
        auto* lane = grid.laneAt(row);
        if (!lane) continue;
        if (hit(l.muteButtons[(size_t)row])) { lane->muted = !lane->muted; publish(); return true; }
        if (hit(l.soloButtons[(size_t)row])) { lane->solo = !lane->solo; publish(); return true; }
        if (hit(l.rowControls[(size_t)row][0]))
        {
            lane->steps = (std::uint8_t)(lane->steps >= grid.stepCount ? 8 :
                std::min(grid.stepCount, (int)lane->steps + 4));
            publish(); return true;
        }
        if (hit(l.rowControls[(size_t)row][1]))
        { lane->rate = (std::uint8_t)((lane->rate + 1) % 3); publish(); return true; }
        if (hit(l.rowControls[(size_t)row][2]))
        { lane->reverse = !lane->reverse; publish(); return true; }
        if (hit(l.rowControls[(size_t)row][3]))
        { lane->swing = (std::uint8_t)(lane->swing >= 100 ? 0 : lane->swing + 25); publish(); return true; }
        for (int step = 0; step < 16; ++step)
        {
            if (!hit(l.cells[(size_t)row][(size_t)step])) continue;
            const int absolute = l.firstStep + step;
            if (absolute >= grid.stepCount) return true;
            const auto* existing = grid.noteAt(row, absolute);
            if (existing)
            {
                // First click selects, second click removes: selection is safe
                // for the upcoming six detailed edit modes.
                if (polyGridSelectedLane == row && polyGridSelectedEvent == existing->id)
                {
                    const auto id = existing->id;
                    grid.removeNote(row, id);
                    polyGridSelectedLane = -1;
                    polyGridSelectedEvent = 0;
                    publish();
                }
                else
                {
                    polyGridSelectedLane = row;
                    polyGridSelectedEvent = existing->id;
                    repaint();
                }
            }
            else
            {
                grid.active = true;
                const auto id = grid.addNote(row, absolute, grid.stepTicks);
                if (id)
                {
                    polyGridSelectedLane = row;
                    polyGridSelectedEvent = id;
                    publish();
                }
            }
            return true;
        }
    }
    return true;
}
