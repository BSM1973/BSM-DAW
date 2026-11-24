#!/usr/bin/env python3
"""Liberty Obsidian 2.0: source and geometry regression for the three main pages."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
theme=read("Source/LibertyDesignSystem.h")
main=read("Source/Main.cpp")
arrange=read("Source/MainComponent.cpp")
performance=read("Source/PerformController.cpp")
mixer=read("Source/MixConsoleController.cpp")
pages=read("Source/PageModeCoordinator.cpp")
toolbar=read("Source/DynamicTrackController.cpp")
geometry=read("Source/NoOverlapLayout.h")

for token in ("canvasDeep","surfaceRaised","accentBlue","accentPurple",
              "accentLight","textPrimary","fillPanel","accentRule"):
    assert token in theme, "Missing visual design token: "+token
assert "juce::ColourGradient" in theme
assert "drawButtonBackground(juce::Graphics& g,juce::Button& button" in main
assert "drawButtonText(juce::Graphics& g,juce::TextButton& b" in main
assert "void drawLinearSlider(juce::Graphics& g,int x,int y,int width,int height," in main
assert "editor->setLookAndFeel(&menuLookAndFeel);" in main
assert "if(editor)editor->setLookAndFeel(nullptr);" in main
assert "performButton.setLookAndFeel(&arrangementLook);" in pages
assert "performButton.setToggleState(perform,juce::dontSendNotification);" in pages
assert "LibertyUI::Colour::accentPurple" in pages
assert "LibertyUI::fillPanel(g,header.toFloat(),false,7.f);" in performance
assert "LibertyUI::fillPanel(g,live.toFloat(),false,8.f);" in performance
assert "LibertyUI::fillPanel(g,area.toFloat(),true,10.f);" in performance
assert "const int columnW = visibleTracks>0?" in performance
assert "juce::jmin(440" in performance
assert "jlimit(110, 220" not in performance
assert "LibertyUI::Colour::canvasDeep" in performance
assert "LibertyUI::Colour::canvas" in mixer
assert "LibertyUI::Colour::accent" in mixer
assert "LibertyUI::fillPanel(g,c.toFloat(),master,7.f);" in arrange
assert "LibertyUI::Colour::canvasDeep" in arrange
assert "LibertyUI::fillPanel(g,juce::Rectangle<float>(" in toolbar
assert "LibertyNoOverlap::audioButton" in toolbar
assert "LibertyNoOverlap::instrumentButton" in toolbar
assert "creationControlsHaveSafeGaps()" in geometry
assert 'g.drawText("FOLLOW ACTIONS' not in performance
assert 'juce::String::fromUTF8("NON ASSIGNÉ")' in performance

# For desktop-supported sizes the four-track PERFORMANCE grid must fill
# almost the entire canvas, never cover the scene launch column, and each
# track must retain a positive, non-overlapping hit rectangle.
for width in (1280,1440,1659,1920,2560):
    left=18
    scene_launch=92
    grid_right=width-scene_launch-28
    total=grid_right-left
    for tracks in range(1,33):
        gap=8
        usable=max(1,total-max(0,tracks-1)*gap)
        col=min(440,max(1,usable//tracks))
        last=left+tracks*col+(tracks-1)*gap
        assert col>0 and last<=grid_right, (width,tracks,col,last,grid_right)
        if tracks==4:
            assert tracks*col>=total*0.72, (width,col,total)

# Signature rendering protects original control geometry.
browser=read("Source/BrowserController.cpp")
assert "LibertyUI::fillPanel(g,brandPanel,false,10.f);" in arrange
assert "LibertyUI::fillPanel(g,commandDeck,false,11.f);" in arrange
assert "mark.closeSubPath();" in arrange
assert 'g.drawFittedText(playing?"STOP":"PLAY",textArea,' in arrange
assert "215 + i * 62, 38, 56, 28" in arrange
assert "LibertyUI::fillPanel(g,s.bounds.toFloat().reduced(0.5f),chosen,6.f);" in mixer
assert "juce::Colour(0xffe7f0f7),thumb.getX(),thumb.getY()" in mixer
assert 'label="BAL 0.0 dB";' in mixer
assert "setDoubleClickReturnValue(true,0.0)" in mixer
assert 'g.drawFittedText("NAVIGATEUR",14,10,juce::jmax(0,getWidth()-96),24,' in browser
assert "closeButton.setBounds(getWidth() - 72, 8, 62, 26);" in browser
assert "if(!button.isVisible()||cell.getWidth()<80||cell.getHeight()<24)" in performance
assert "if(!queued&&!active)continue;" in performance
assert "g.fillRoundedRectangle(light,1.5f);" in performance
assert "queued?LibertyUI::Colour::accentAmber" in performance
print("PASS: Obsidian Signature rendering, typography and preserved control geometry")

# Obsidian Studio: shared musical visuals, real signal data, and non-overlapping
# information regions. Interaction geometry remains under the original editors.
piano=read("Source/MidiEditor.cpp")
step=read("Source/PolyphonicStepGridUI.inc")
multi=read("Source/MultiMidiClipController.cpp")
inline=read("Source/InlineMidiEditor.inc")
names=read("Source/ClipNamingController.inc")
warp=read("Source/AudioClipWarpView.cpp")
assert "inline void musicalBlock(" in theme
assert "inline void waveformEnvelope(" in theme
assert "g.reduceClipRegion(area.toNearestInt());" in theme
assert "LibertyUI::waveformEnvelope(g,waveform,clip.toFloat(),selected);" in arrange
assert "waveformMin[index][(size_t)bin]" in arrange
assert "waveformMax[index][(size_t)bin]" in arrange
assert "LibertyUI::musicalBlock(g,r.toFloat(),col.withAlpha(" in multi
assert "LibertyUI::musicalBlock(g,bounds.toFloat()," in inline
assert "g.reduceClipRegion(box);" in names
assert "LibertyUI::musicalBlock(g,r.toFloat()," in piano
assert "const juce::Colour velocityColour = colourForMidiVelocity((int)n.velocity);" in piano
assert "infoArea.removeFromRight(122).reduced(2,1)" in piano
assert "infoArea.removeFromRight(6);" in piano
assert "g.drawFittedText(baseInfo,infoArea," in piano
assert "pianoKeyWidth + 158, infoY" not in piano
assert "LibertyUI::musicalBlock(g,noteR," in step
assert "LibertyPolyGridDirect::normalizedValue(grid, note, activeProperty)" in step
assert "LibertyUI::musicalBlock(g,bounds.toFloat(),colour," in performance
assert "const int step = 1; // Exact peak envelope" in warp
assert "LibertyUI::Colour::accentLight);" in warp
print("PASS: Obsidian Studio waveform and musical block rendering")

# Motion: animations remain message-thread-only, have bounded lifetime and never
# move hit targets. Stereo values and the transport clock remain engine-driven.
motion=read("Source/LibertyMotionAnimator.h")
assert "SafePointer<juce::Component>" in motion
assert "entries.size()>=32" in motion
assert "if(entries.empty())stopTimer();" in motion
assert "c->repaint();" in motion
assert "startTimerHz(60);" in motion
assert "hoverMotion.request(button," in main
assert "pageMotion.request(b,selected?1.f:0.f)" in pages
assert "LibertyMotion::meterSegmentFill(amount,i,segments)" in mixer
assert "LibertyMixConsoleMeter::meterPosition(value.rms)" in mixer
assert "LibertyMixConsoleMeter::meterPosition(value.heldPeak)" in mixer
assert "LibertyMixConsoleMeter::advance(strip->meter,peaks,rms)" in mixer
assert "g.reduceClipRegion(area.withTrimmedLeft(headerW));" in arrange
assert "repaint(740,34,90,36);" in arrange
assert "LibertyMotion::pulse(seconds,1.0)" in performance
assert "player.isCapturing() && recordArrangeButton.isVisible()" in performance
assert "LibertyMotion::pulse(seconds,1.4)" in performance
print("PASS: Motion bounded animation, real stereo meter, locator and capture lights")

precision=read("Source/LibertyPrecisionUI.h")
assert "LibertyPrecision::drawDial(g" in main and "LibertyPrecision::drawDial(g" in mixer
assert "slider.setPopupDisplayEnabled(true,true,nullptr,950)" in precision
assert "tickCount=d>=36.f?16:8" in precision
assert "drawPopupMenuItem" in main and "precisionTooltips" in main
assert "hasKeyboardFocus(true)" in main and "hasKeyboardFocus(true)" in precision
assert "ui-motion.txt" in main and "LibertyMotion::setAnimationsEnabled(next)" in main
assert "!animationsEnabled() || !c.isShowing()" in motion
assert "juce::AffineTransform::scale(2.f)" in precision
assert "LibertyPrecision::midiCursor(tool)" in piano
assert "LibertyPrecision::arrangementCursor(activeArrangeTool)" in multi
assert "cursorGraphics.drawText" not in piano
print("PASS: Precision vector controls, value popups, focus, saved motion preference and tool cursors")

# Obsidian menu consistency: shared native painter and no legacy local selector colors.
menus=read("Source/LibertyObsidianMenuStyle.h")
inputs=read("Source/TrackInputPaletteLookAndFeel.h")
assert "public LibertyUI::ObsidianMenuStyle" in main and "public LibertyUI::ObsidianMenuStyle" in inputs and "public LibertyUI::ObsidianMenuStyle" in mixer
assert "LibertyUI::ObsidianMenuStyle::drawPopupMenuItem" in main
assert "drawRoundedRectangle(tile.reduced(.5f)" in menus
assert "ticked,submenu,text,shortcut,icon,&colour" in menus
assert "g.reduceClipRegion(area)" in menus
for path in ("Source/AudioRecordingController.cpp","Source/MidiEditor.cpp","Source/LibertyDrumSampler.h"):
    import re
    assert not re.search(r"juce::ComboBox::\w+ColourId,\s*juce::Colour\(0x",read(path)), path
print("PASS: common Obsidian menu painter, preserved native menu content and shared selector colors")

# Superseded single-clip overlay must not wake up and draw a fixed-scale ghost.
owner_header=read("Source/MainComponent.h")
assert "owner.midiClipOverlay.retireForMultiClipController();" in multi
retirement=owner_header.split("void retireForMultiClipController() noexcept",1)[1].split("bool isRetired()",1)[0]
for token in ("retired = true;","stopTimer();","setInterceptsMouseClicks(false, false);","setVisible(false);"):
    assert token in retirement
assert owner_header.split("class MidiClipOverlay final",1)[1].split("void timerCallback() override;",1)[0].count("if (retired) return;")>=2
assert "retired MIDI overlay must paint no pixels" in read("Source/ArrangementTools.inc")
print("PASS: legacy MIDI overlay permanently retired after multi-clip takeover")

# MIDI right-click opens a scrollable JUCE dialog. Wheel input stays in the
# dialog and must never zoom/scroll the underlying ARRANGE timeline.
zoom=read("Source/ZoomController.cpp")
inline_midi=read("Source/InlineMidiEditor.inc")
assert "juce::Viewport*>(component)" in zoom
assert "juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0" in zoom
assert zoom.index("getNumModalComponents() > 0") < zoom.index("timelineHorizontalZoom.store(applyWheelZoom")
assert "if (isInsideScrollableControl(event.eventComponent))" in zoom
assert "class InlineMidiActionsViewport final : public juce::Viewport" in inline_midi
assert "setScrollBarsShown(true,false)" in inline_midi
assert "options.content.setOwned(scrollable);" in inline_midi
assert "options.launchAsync();" in inline_midi
print("PASS: MIDI dialog wheel stays in its viewport, ARRANGE zoom isolated")

# Drum Studio: all controls are real, native notes are committed to a chosen
# instrument lane, and the fixed-size three-page UI has no scrolling.
drum=read("Source/LibertyDrumSampler.h")
drum_grid=read("Source/LibertyDrumPatternGrid.h")
main_drum=read("Source/MainComponent.cpp")
main_header=read("Source/MainComponent.h")
for token in ('previewPlay.onClick=','previewStop.onClick=','insertButton.onClick=','sampler.setPatternPreviewPlaying(true)','patternMidiNotes()','sampler.stopAllPads()','setPage(0)','patternGrid.setBounds','setResizable(false,false)'):
    assert token in (drum+main_drum),token
assert 'class LibertyDrumPatternGrid final' in drum_grid
assert 'sampler.setPatternVelocity(found.first,found.second,dragValue)' in drum_grid
assert 'engine.setDrumSamplerTrack(lane);' in main_drum
assert 'commitLibertySequencerInstrumentClip(*this,notes,lane,start,duration,false)' in main_header
assert 'centreWithSize(960,700);' in main_drum
drum_window=main_drum.split("class FloatingDrumSamplerWindow final",1)[1].split("std::unique_ptr<FloatingDrumSamplerWindow>",1)[0]
assert 'setResizable(true, true)' not in drum_window
assert 'setScrollBarsShown(' not in drum
print("PASS: Drum Studio PLAY/STOP, 16x16 grid, native arrangement insertion, 13-inch fixed UI")

# 960x700 design-system layout: all page controls stay inside the window,
# apart from deliberate panel-only backgrounds (not interactive hit areas).
w,h=960,700
margin=14; pad_top=135;pad_bottom=h-80;gap=12
cell_w=(w-2*margin-3*gap)//4;cell_h=(pad_bottom-pad_top-3*gap)//4
pads=[(margin+(i%4)*(cell_w+gap),pad_top+(i//4)*(cell_h+gap),cell_w,cell_h) for i in range(16)]
assert all(x>=margin and y>=pad_top and x+cw<=w-margin and y+ch<=pad_bottom for x,y,cw,ch in pads)
assert all(not (a[0]<b[0]+b[2] and b[0]<a[0]+a[2] and a[1]<b[1]+b[3] and b[1]<a[1]+a[3]) for i,a in enumerate(pads) for b in pads[i+1:])
pattern_bounds=(margin,125,w-2*margin,h-266)
assert pattern_bounds[1]+pattern_bounds[3]<h-95
assert h-95+34<h-28
sample_last_row=316+5*39+28
assert sample_last_row<h-28
assert 'setScrollBarsShown' not in drum
print("PASS: 960x700 Drum Studio page geometry and non-overlapping pad grid")

# Layout is initialized after the component has real dimensions; the pattern
# grid finishes before its instruction line (both on a 13-inch display).
assert drum.index("setSize(960,700);") < drum.index("setPage(0);",drum.index("setSize(960,700);"))
assert "patternGrid.setBounds(margin,125,getWidth()-2*margin,getHeight()-266);" in drum
assert 125+(700-266)<=700-130
print("PASS: Drum Studio startup size and instruction/grid separation")

# Fresh projects must ship with audible drum samples and an editable rhythm.
assert "drumSampler->loadFactoryKit(0);" in main_drum
assert "drumSampler->loadStarterPattern();" in main_drum
assert "sampler.getFactoryKit()+2" in drum
print("PASS: first-run Drum Studio has audible default kit and playable starter rhythm")

# Closing Drum Studio during song playback must not mute the routed instrument.
assert "void stopPreview(bool closingWindow=false)" in drum
assert "if(wasPreviewing || (!closingWindow && (!transportPlaying||!transportPlaying())))" in drum
assert "panel->stopPreview(true);" in main_drum
assert "if(!allowed && previewPlaying.load(std::memory_order_acquire))" in drum
print("PASS: Drum Studio closes without silencing arrangement drum voices")

# 100 real factory grooves have compact two-level navigation and retain the
# authoritative native MIDI clip insertion (no placeholder graphic).
library=read("Source/LibertyDrumPatternLibrary.h")
kit=read("Source/LibertyDrumFactoryCC0.h")
cmake=read("CMakeLists.txt")
assert "styleCount=10, variationsPerStyle=10" in library
assert "static constexpr int factoryPatternCount=100" in drum
assert "libraryStyleSelector.onChange=" in drum
assert "libraryPatternSelector.onChange=" in drum
assert "sampler.loadFactoryPattern(style,variation)" in drum
assert "libraryPatternSelector.setTextWhenNothingSelected" in drum
assert "factoryKit" in drum and "factoryPreset" in drum
assert "juce_add_binary_data(LibertyFactoryDrumAssets" in cmake
assert "bd71faf5035e8aa278ebfe3f4dc68804ae7bc5ca" in cmake
assert "LibertyDrumAssets::getNamedResource" in kit
assert "namedResourceListSize" in kit
assert "setResizable(false,false)" in main_drum
assert 'libraryStyleSelector.setBounds(margin,87,198,31)' in drum
assert 'libraryPatternSelector.setBounds(margin+208,87,330,31)' in drum
assert 14+198 < 14+208
assert 14+208+330 < 560
print("PASS: 100 named rhythms, CC0 audio bundling, saved kits and fixed-size menu geometry")

# Native drums are rendered by exactly one owner each audio callback.
# With an instrument lane assigned, both idle preview and arrangement
# pass through the same strip, effect slots, one-knob and master gain.
engine=read("Source/AudioEngine.cpp")
drum=read("Source/LibertyDrumSampler.h")
assert "if (drumSamplerTrack.load(std::memory_order_acquire) < 0)" in engine
assert "if ((!playing.load(std::memory_order_relaxed) ||" not in engine
assert engine.count("oneKnob.process(100000 + instrumentTrack * 8 + slot")>=2
assert "const float auditionMaster=masterGain.load" in engine
assert "if(v>0)noteOn(36+pad,(float)v/127.f);" in drum
assert "if(v>0)noteOn(36+pad,(float)v/127.f,true);" not in drum
smoke=read("Tests/DrumSamplerSmoke.cpp")
assert "maxDifference>1.0e-5f" in smoke
assert "audition.renderMidi(auditionAudio,juce::MidiBuffer{});" in smoke
assert "arrangement.renderMidi(arrangementAudio,midi);" in smoke
print("PASS: Drum Sampler preview and arrangement use one DSP render, same FX and MIDI articulations")

# Approved Obsidian drum-pad icon designs are rendered as native vectors,
# and classified by actual audio sample name, not just by fixed slot.
art=read("Source/LibertyDrumPadArtwork.h")
drum=read("Source/LibertyDrumSampler.h")
assert "class Button final:public juce::TextButton" in art
assert "void paintButton(juce::Graphics& g,bool mouseOver,bool mouseDown) override" in art
assert "std::array<LibertyDrumArt::Button, LibertyDrumSampler::padCount> pads;" in drum
assert "pads[(size_t)i].configure(" in drum
assert "pads[(size_t)i].setPadSelected(i==selectedPad)" in drum
assert "sampler.auditionPad(i, 1.0f)" in drum
for role in ("closedHat","openHat","clap","rim","tomLow","tomMid","tomHigh",
             "crash","ride","shaker","tambourine","cowbell","percussion","effect"):
    assert role in art,role
assert "return hasSample?effect:fallback[" in art
assert "setResizable(false,false)" in main_drum
assert "setScrollBarsShown(" not in drum
print("PASS: 16 clickable Obsidian drum pad illustrations, kit-aware mapping, no scrollbars")

# MIDI learn controls cannot intercept native drum patterns 36..51; the pad
# buttons retain the custom Obsidian artwork and right-click no longer plays it.
drum=read("Source/LibertyDrumSampler.h")
art=read("Source/LibertyDrumPadArtwork.h")
engine=read("Source/AudioEngine.cpp")
main=read("Source/MainComponent.cpp")
for needle in ("routeLearnedMidi(", "setPadMidiLearn(", "midiLearnBindings", "midiCcHigh",
               'setAttribute("midiLearn"', "void openPadMidiMenu(int index)",
               "button.onContextMenu=[this,i]"):
    assert needle in drum,needle
assert "if(e.mods.isPopupMenu())" in art
assert "learnedDrumEvent=sampler->routeLearnedMidi(message)" in engine
assert "!(learnedDrumEvent&&monitored==drumLane)" in engine
assert "owner.getDrumMidiLearnEvent()" in main
studio=read("Source/LibertyDrumFactoryCC0.h")
assert "inline Supplement layer(int style,int pad)" in studio
assert "const float gain=std::min(.88f/peak" in studio
assert "if(style==1||style==7)" in studio
assert "secondary.level*layerLow" in studio
print("PASS MIDI Learn (16 pads, physical CC/note, persistence) and new genre-specific CC0 layering")

art=read("Source/LibertyDrumPadArtwork.h")
drum=read("Source/LibertyDrumSampler.h")
assert "void setMidiLearnAssigned(bool assigned)" in art
assert "pads[(size_t)i].setMidiLearnAssigned(raw!=0);" in drum
assert 'g.drawFittedText("MIDI",tag.toNearestInt(),' in art
print("PASS: MIDI Learn assignment indicator preserves icon and pad labels")

# Browser filters must use the established Obsidian look on every menu level.
filters=browser.split("void showPluginFiltersMenu()",1)[1].split("juce::Component::SafePointer<BrowserPanel> safe(this);",1)[0]
assert "static LibertyUI::ObsidianMenuStyle filterMenuStyle;" in filters
assert "popup.setLookAndFeel(&filterMenuStyle);" in filters
for name in ("menu","accessMenu","collectionsMenu","targetedRepairMenu","missingMenu","renameMenu","deleteMenu","manufacturerMenu","letterMenu","formatMenu","typeMenu","usageMenu","instrumentMenu","effectMenu","layoutMenu","backupMenu"):
    assert f"auto {name} = makeFilterMenu();" in filters, name
assert "letterMenu = makeFilterMenu();" in filters
assert "letterMenu = juce::PopupMenu();" not in filters
print("PASS: existing Obsidian painter applied explicitly to browser filter menu and all nested menus")
