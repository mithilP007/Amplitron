// =============================================================================
// Web / Emscripten file dialog stub
//
// No native file dialogs in a browser environment — returns empty strings.
// =============================================================================

#include "gui/file_dialog.h"

namespace GuitarAmp {

std::string show_save_dialog(const std::string& /*default_name*/,
                             const std::string& /*filter_desc*/,
                             const std::string& /*filter_ext*/) {
    return "";
}

} // namespace GuitarAmp
