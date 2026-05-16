#include "gui/pedal_board.h"
#include "gui/pedal_widget.h"
#include "gui/theme.h"
#include "gui/command.h"
#include "audio/effects/amp_simulator.h"
#include "gui/gui_midi.h"
#include "midi/midi_manager.h"


#include <cstring>
#include <imgui.h>
#include <set>


namespace Amplitron {


/** @brief Construct PedalBoard and build initial widget list from engine state. */
PedalBoard::PedalBoard(AudioEngine& engine, CommandHistory& history, GuiMidi* gui_midi)
    : engine_(engine), history_(history), gui_midi_(gui_midi) {
    rebuild_widgets();
}


/** @brief Default destructor. */
PedalBoard::~PedalBoard() = default;


/** @brief Recreate PedalWidget list to match the engine's current effect chain.
 *  Visibility is preserved by effect pointer identity so that a footswitch-off pedal
 *  stays on the board.  Brand-new effects (unrecognised pointers, e.g. after a preset
 *  load or an add) are shown only if they are currently enabled or are the Amp Sim. */
void PedalBoard::rebuild_widgets() {
    // Snapshot which effect pointers are currently on the board before clearing.
    std::set<Effect*> prev_visible;
    for (int idx : visible_indices_) {
        if (idx >= 0 && idx < static_cast<int>(widgets_.size())) {
            prev_visible.insert(widgets_[idx]->get_effect().get());
        }
    }


    widgets_.clear();
    visible_indices_.clear();
