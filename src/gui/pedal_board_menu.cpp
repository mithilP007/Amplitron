#include "gui/pedal_board.h"
#include "gui/gui_midi.h"
#include "midi/midi_manager.h"
#include "gui/theme.h"


#include "audio/effects/noise_gate.h"
#include "audio/effects/compressor.h"
#include "audio/effects/overdrive.h"
#include "audio/effects/distortion.h"
#include "audio/effects/equalizer.h"
#include "audio/effects/chorus.h"
#include "audio/effects/phaser.h"
#include "audio/effects/flanger.h"
#include "audio/effects/delay.h"
#include "audio/effects/reverb.h"
#include "audio/effects/cabinet_sim.h"
#include "audio/effects/ir_cabinet.h"
#include "audio/effects/amp_simulator.h"
#include "audio/effects/wah.h"
#include "audio/effects/octaver.h"
#include "audio/effects/pitch_shifter.h"


#include <imgui.h>
#include <cstdio>


namespace Amplitron {


void PedalBoard::render_add_pedal_menu() {
    if (ImGui::Button("+ Add Pedal")) {
        ImGui::OpenPopup("AddPedalPopup");
    }


    if (ImGui::BeginPopup("AddPedalPopup")) {
        ImGui::TextColored(Theme::Gold(), "DRIVE");
        if (ImGui::MenuItem("Overdrive")) {
            add_effect_and_show(std::make_shared<Overdrive>());
        }
