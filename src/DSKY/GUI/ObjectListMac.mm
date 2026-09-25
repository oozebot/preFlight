///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
// The object list's hover tooltip on macOS: a mouse-moved feed that does not depend on keyboard
// focus, and a tooltip panel that looks like the system's.

#include "ObjectListMac.hpp"

#include <wx/window.h>
#include <wx/osx/private.h>

#import <Cocoa/Cocoa.h>

namespace DSKY
{

void mac_track_mouse_moves(wxWindow *data_view)
{
    // wx forwards a mouse-moved event only to the view Cocoa hit-tests under the pointer. The
    // window sends those events to its first responder, so the outline view receives them only
    // while it has keyboard focus; the copy from the scroll view's tracking area is dropped because
    // the hit view is the outline view. A tracking area owned by the outline view itself delivers
    // every move to it, whichever view holds the focus.
    auto *scroll = static_cast<NSScrollView *>(data_view->GetHandle());
    if (scroll == nil || ![scroll isKindOfClass:[NSScrollView class]])
        return;
    NSView *outline = [scroll documentView];
    if (outline == nil)
        return;
    const NSTrackingAreaOptions options = NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited |
                                          NSTrackingActiveAlways | NSTrackingInVisibleRect;
    NSTrackingArea *area = [[NSTrackingArea alloc] initWithRect:NSZeroRect options:options owner:outline userInfo:nil];
    [outline addTrackingArea:area];
    [area release];
}

// One panel for the whole app, created on first use: a borderless, non-activating panel that
// ignores the pointer, with the system tooltip material and font.
static NSPanel *s_tooltip_panel = nil;
static NSTextField *s_tooltip_label = nil;

void mac_tooltip_show(const wxString &text, const wxPoint &screen_pos)
{
    const CGFloat pad = 4.0;
    if (s_tooltip_panel == nil)
    {
        s_tooltip_panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 10, 10)
                                                     styleMask:NSWindowStyleMaskBorderless |
                                                               NSWindowStyleMaskNonactivatingPanel
                                                       backing:NSBackingStoreBuffered
                                                         defer:NO];
        [s_tooltip_panel setLevel:NSPopUpMenuWindowLevel];
        [s_tooltip_panel setOpaque:NO];
        [s_tooltip_panel setBackgroundColor:[NSColor clearColor]];
        [s_tooltip_panel setHasShadow:YES];
        [s_tooltip_panel setIgnoresMouseEvents:YES];
        [s_tooltip_panel setHidesOnDeactivate:YES];
        [s_tooltip_panel setReleasedWhenClosed:NO];

        NSVisualEffectView *effect = [[NSVisualEffectView alloc] initWithFrame:NSMakeRect(0, 0, 10, 10)];
        [effect setMaterial:NSVisualEffectMaterialToolTip];
        [effect setBlendingMode:NSVisualEffectBlendingModeBehindWindow];
        [effect setState:NSVisualEffectStateActive];
        [effect setWantsLayer:YES];
        [[effect layer] setCornerRadius:5.0];
        [[effect layer] setMasksToBounds:YES];

        s_tooltip_label = [[NSTextField alloc] initWithFrame:NSMakeRect(pad, pad, 1, 1)];
        [s_tooltip_label setBezeled:NO];
        [s_tooltip_label setDrawsBackground:NO];
        [s_tooltip_label setEditable:NO];
        [s_tooltip_label setSelectable:NO];
        [s_tooltip_label setFont:[NSFont toolTipsFontOfSize:0]];
        [s_tooltip_label setTextColor:[NSColor labelColor]];
        [effect addSubview:s_tooltip_label];
        [s_tooltip_panel setContentView:effect];
        [effect release];
    }

    [s_tooltip_label setStringValue:[NSString stringWithUTF8String:text.utf8_str()]];
    [s_tooltip_label sizeToFit];
    const NSSize label = [s_tooltip_label frame].size;
    [s_tooltip_label setFrameOrigin:NSMakePoint(pad, pad)];

    // wx screen coordinates have their origin at the top left of the primary display.
    const wxRect frame(screen_pos.x, screen_pos.y, int(label.width + 2 * pad), int(label.height + 2 * pad));
    [s_tooltip_panel setFrame:wxToNSRect(nil, frame) display:NO];
    [s_tooltip_panel orderFront:nil];
}

void mac_tooltip_hide()
{
    if (s_tooltip_panel != nil && [s_tooltip_panel isVisible])
        [s_tooltip_panel orderOut:nil];
}

} // namespace DSKY
