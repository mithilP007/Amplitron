#pragma once

#include "common.h"
#include "audio/audio_engine.h"
#include "gui/command_history.h"

namespace GuitarAmp {

class PedalWidget;

class PedalBoard {
public:
    PedalBoard(AudioEngine& engine, CommandHistory& history);
    ~PedalBoard();

    void render();

    void rebuild_widgets();

private:
    void render_add_pedal_menu();
    void render_signal_chain();

    AudioEngine& engine_;
    CommandHistory& history_;
    std::vector<std::unique_ptr<PedalWidget>> widgets_;
};

} // namespace GuitarAmp
