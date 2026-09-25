///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2019 - 2021 Vojtěch Bubník @bubnikv, Vojtěch Král @vojtechkral
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

class wxWindow;

namespace DSKY
{

class RetinaHelper
{
public:
    RetinaHelper(wxWindow *window);
    ~RetinaHelper();

    void set_use_retina(bool value);
    bool get_use_retina();
    float get_scale_factor();

private:
#ifdef __WXGTK3__
    wxWindow *m_window;
#endif // __WXGTK3__
    void *m_self;
};

} // namespace DSKY

