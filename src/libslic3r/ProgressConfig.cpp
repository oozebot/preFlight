///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/

#include "ProgressConfig.hpp"
#include "Print.hpp"

namespace Slic3r
{

// Global instance of progress configuration
// Can be modified at runtime if needed for different scenarios
ProgressConfig g_progress_config;

void ProgressTracker::report(float weight, const std::string &message)
{
    m_accumulated_weight += weight;
    int percent = current_percent();
    if (m_print)
        m_print->set_status(percent, message);
}

} // namespace Slic3r
