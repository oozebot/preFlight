///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2020 - 2023 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "Technologies.hpp"
#include "GUI_Init.hpp"

#include "luminary/presets/app_config/AppConfig.hpp"
#include "luminary/platform/paths/Paths.hpp"

#include "DSKY/GUI/GUI.hpp"
#include "DSKY/GUI/GUI_App.hpp"
#include "DSKY/GUI/3DScene.hpp"
#include "DSKY/GUI/InstanceCheck.hpp"
#include "DSKY/GUI/format.hpp"
#include "DSKY/GUI/MainFrame.hpp"
#include "DSKY/GUI/Plater.hpp"
#include "DSKY/GUI/I18N.hpp"

// To show a message box if GUI initialization ends up with an exception thrown.
#include <wx/msgdlg.h>

#include <boost/nowide/iostream.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/console.hpp>

#if __APPLE__
#include <signal.h>
#endif // __APPLE__

namespace DSKY
{
using namespace Luminary;

const std::vector<std::pair<int, int>> OpenGLVersions::core = {{3, 2}, {3, 3}, {4, 0}, {4, 1}, {4, 2},
                                                               {4, 3}, {4, 4}, {4, 5}, {4, 6}};

int GUI_Run(GUI_InitParams &params)
{
#if __APPLE__
    // On OSX, we use boost::process::spawn() to launch new instances of preFlight from another preFlight.
    // boost::process::spawn() sets SIGCHLD to SIGIGN for the child process, thus if a child preFlight spawns another
    // subprocess and the subrocess dies, the child preFlight will not receive information on end of subprocess
    // (posix waitpid() call will always fail).
    // https://jmmv.dev/2008/10/boostprocess-and-sigchld.html
    // The child instance of preFlight has to reset SIGCHLD to its default, so that posix waitpid() and similar continue to work.
    signal(SIGCHLD, SIG_DFL);
#endif // __APPLE__

#ifdef PREFLIGHT_LOG_TO_FILE
    auto sink = boost::log::add_file_log(get_default_datadir() + "/slicer.log");
    sink->locked_backend()->auto_flush();
    boost::log::add_console_log();
#endif // PREFLIGHT_LOG_TO_FILE
    try
    {
        DSKY::GUI_App *gui = new DSKY::GUI_App(params.start_as_gcodeviewer ? DSKY::GUI_App::EAppMode::GCodeViewer
                                                                           : DSKY::GUI_App::EAppMode::Editor);
        if (gui->get_app_mode() != DSKY::GUI_App::EAppMode::GCodeViewer)
        {
            // G-code viewer is currently not performing instance check, a new G-code viewer is started every time.
            bool gui_single_instance_setting = gui->app_config->get_bool("single_instance");
            if (Luminary::instance_check(params.argc, params.argv, gui_single_instance_setting))
            {
                return -1;
            }
        }

        DSKY::GUI_App::SetInstance(gui);
        gui->init_params = &params;
        return wxEntry(params.argc, params.argv);
    }
    catch (const Luminary::Exception &ex)
    {
        boost::nowide::cerr << ex.what() << std::endl;
        wxMessageBox(boost::nowide::widen(ex.what()), _L("preFlight GUI initialization failed"), wxICON_STOP);
    }
    catch (const std::exception &ex)
    {
        boost::nowide::cerr << "preFlight GUI initialization failed: " << ex.what() << std::endl;
        wxMessageBox(format_wxstr(_L("Fatal error, exception catched: %1%"), ex.what()),
                     _L("preFlight GUI initialization failed"), wxICON_STOP);
    }

    // error
    return 1;
}

} // namespace DSKY
