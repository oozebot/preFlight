///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#import "MacDarkMode.hpp"

#include <wx/window.h>
#include "wx/osx/core/cfstring.h"

#import <algorithm>

#import <Cocoa/Cocoa.h>
#import <Foundation/Foundation.h>
#import <AppKit/NSScreen.h>
#import <WebKit/WebKit.h>
#import <objc/runtime.h>

@interface MacDarkMode : NSObject {}
@end


@implementation MacDarkMode

namespace DSKY {

bool mac_dark_mode()
{
    NSString *style = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleInterfaceStyle"];
    return style && [style isEqualToString:@"Dark"];

}

void mac_set_appearance(bool dark)
{
    // Pin the application appearance to the active theme so native controls don't follow the
    // (possibly opposite) macOS system setting. Applied at startup; theme changes restart the app.
    NSAppearanceName name = dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua;
    [NSApp setAppearance:[NSAppearance appearanceNamed:name]];
}

double mac_max_scaling_factor()
{
    double scaling = 1.;
    if ([NSScreen screens] == nil) {
        scaling = [[NSScreen mainScreen] backingScaleFactor];
    } else {
	    for (int i = 0; i < [[NSScreen screens] count]; ++ i)
	    	scaling = std::max<double>(scaling, [[[NSScreen screens] objectAtIndex:0] backingScaleFactor]);
	}
    return scaling;
}

void WKWebView_evaluateJavaScript(void * web, wxString const & script, void (*callback)(wxString const &))
{
    [(WKWebView*)web evaluateJavaScript:wxCFStringRef(script).AsNSString() completionHandler: ^(id result, NSError *error) {
        if (callback && error != nil) {
            wxString err = wxCFStringRef(error.localizedFailureReason).AsString();
            callback(err);
        }
    }];
}

// Recursively search for NSOutlineView in the view hierarchy
static NSOutlineView* findOutlineView(NSView *view)
{
    if ([view isKindOfClass:[NSOutlineView class]])
        return (NSOutlineView *)view;
    for (NSView *subview in [view subviews]) {
        NSOutlineView *found = findOutlineView(subview);
        if (found) return found;
    }
    return nil;
}

void mac_disable_horizontal_scroll(void *nsview_handle)
{
    if (!nsview_handle)
        return;
    NSView *view = (__bridge NSView *)nsview_handle;
    NSView *parent = view;
    while (parent && ![parent isKindOfClass:[NSScrollView class]])
        parent = [parent superview];
    if ([parent isKindOfClass:[NSScrollView class]])
        [(NSScrollView *)parent setHasHorizontalScroller:NO];
}

int mac_get_outlineview_name_width(void *nsview_handle)
{
    if (!nsview_handle)
        return -1;
    NSView *view = (__bridge NSView *)nsview_handle;

    // Find enclosing NSScrollView
    NSView *parent = view;
    while (parent && ![parent isKindOfClass:[NSScrollView class]])
        parent = [parent superview];
    NSScrollView *scrollView = nil;
    if ([parent isKindOfClass:[NSScrollView class]])
        scrollView = (NSScrollView *)parent;

    // Find the NSOutlineView
    NSOutlineView *outlineView = findOutlineView(view);
    if (!outlineView || outlineView.numberOfColumns < 2)
        return -1;

    // Visible width from the clip view (actual drawable area)
    CGFloat visibleWidth = scrollView
        ? [scrollView.contentView bounds].size.width
        : [outlineView bounds].size.width;

    // Sum widths of all columns except column 0
    CGFloat otherColumnsWidth = 0;
    for (NSInteger i = 1; i < outlineView.numberOfColumns; i++)
        otherColumnsWidth += [[outlineView.tableColumns objectAtIndex:i] width];

    // Empirically measure NSOutlineView's overhead (intercell spacing, outline
    // indentation, internal padding) by temporarily forcing column 0 wide,
    // reading the resulting frame, and computing everything-except-column-0.
    NSTableColumn *col0 = [outlineView.tableColumns objectAtIndex:0];
    CGFloat savedWidth = col0.width;

    // Force column 0 wide enough to guarantee overflow
    [col0 setWidth:visibleWidth];
    [outlineView tile]; // synchronous relayout

    // "X" = total width consumed by everything except column 0
    CGFloat X = outlineView.frame.size.width - visibleWidth;
    CGFloat nameWidth = visibleWidth - X;

    // Restore if calculation failed, otherwise leave at new width
    if (nameWidth <= 0) {
        [col0 setWidth:savedWidth];
        return -1;
    }

    // Set column 0 to exact fit and re-tile
    [col0 setWidth:nameWidth];
    // Let the last column absorb any remaining sub-pixel space so the native
    // trailing column separator is pushed to the very edge and not visible.
    [outlineView setColumnAutoresizingStyle:NSTableViewLastColumnOnlyAutoresizingStyle];
    [outlineView tile];

    return (int)nameWidth;
}

void mac_set_staticbox_transparent(void *nsview_handle)
{
    if (!nsview_handle)
        return;
    NSView *view = (__bridge NSView *)nsview_handle;
    // wxStaticBox wraps an NSBox, find it in the hierarchy
    NSView *current = view;
    while (current && ![current isKindOfClass:[NSBox class]])
        current = [current superview];
    if ([current isKindOfClass:[NSBox class]]) {
        NSBox *box = (NSBox *)current;
        // Make the NSBox fully transparent with no native title.
        // OnPaintMac() in FlatStaticBox does all the visual rendering (border,
        // background, label text).  NSNoTitle keeps the native title cell from
        // influencing wxStaticBoxSizer's content margins via GetBordersForSizer(),
        // which otherwise mismatches the native layout and the custom-drawn
        // border position.
        [box setBoxType:NSBoxCustom];
        [box setTransparent:YES];
        [box setBorderType:NSNoBorder];
        [box setBorderWidth:0];
        [box setTitlePosition:NSNoTitle];
    }
}

void mac_set_staticbox_colors(void *nsview_handle,
                              unsigned char fill_r, unsigned char fill_g, unsigned char fill_b,
                              unsigned char border_r, unsigned char border_g, unsigned char border_b,
                              unsigned char title_r, unsigned char title_g, unsigned char title_b)
{
    if (!nsview_handle)
        return;
    @try {
        NSView *view = (__bridge NSView *)nsview_handle;
        if (!view)
            return;
        // Find the NSBox; GetHandle() may return it directly or a child view
        NSView *current = view;
        while (current && ![current isKindOfClass:[NSBox class]])
            current = [current superview];
        if (!current || ![current isKindOfClass:[NSBox class]])
            return;
        NSBox *box = (NSBox *)current;
        [box setFillColor:[NSColor colorWithSRGBRed:fill_r/255.0 green:fill_g/255.0
                                               blue:fill_b/255.0 alpha:1.0]];
        [box setBorderColor:[NSColor colorWithSRGBRed:border_r/255.0 green:border_g/255.0
                                                 blue:border_b/255.0 alpha:1.0]];
        // Style the title: wxNSBox doesn't support setAttributedTitle:, so
        // set the title cell's text color directly via the NSBox's title cell.
        NSCell *titleCell = [box titleCell];
        if (titleCell && [titleCell respondsToSelector:@selector(setTextColor:)]) {
            [(NSTextFieldCell *)titleCell setTextColor:
                [NSColor colorWithSRGBRed:title_r/255.0 green:title_g/255.0
                                     blue:title_b/255.0 alpha:1.0]];
        }
    } @catch (NSException *e) {
        NSLog(@"mac_set_staticbox_colors exception: %@", e);
    }
}

void mac_set_textfield_background(void *nsview_handle,
                                  unsigned char r, unsigned char g, unsigned char b)
{
    if (!nsview_handle)
        return;
    NSView *view = (__bridge NSView *)nsview_handle;
    // wxTextCtrl wraps NSTextField (single-line) or NSScrollView+NSTextView (multi-line).
    // Disable native background drawing entirely; the parent StaticBox::doRender()
    // handles all background painting (including disabled state colors).
    // This prevents the native NSTextField disabled appearance from showing through.
    if ([view isKindOfClass:[NSTextField class]]) {
        NSTextField *field = (NSTextField *)view;
        [field setDrawsBackground:NO];
    } else if ([view isKindOfClass:[NSScrollView class]]) {
        NSScrollView *sv = (NSScrollView *)view;
        [sv setDrawsBackground:NO];
        NSTextView *tv = (NSTextView *)[sv documentView];
        if (tv && [tv isKindOfClass:[NSTextView class]])
            [tv setDrawsBackground:NO];
    }
}

// Selection styling is handled by preFlightRendererNative in GUI_App.cpp,
// which overrides DrawItemSelectionRect to draw a 1px outline instead of a fill.
void mac_set_treectrl_outline_selection(void * /*nsview_handle*/)
{
    // No-op, kept for API compatibility.
}

void mac_set_window_transparent(void *nsview_handle)
{
    if (!nsview_handle)
        return;
    NSView *view = (__bridge NSView *)nsview_handle;
    NSWindow *window = [view window];
    if (window) {
        [window setOpaque:NO];
        [window setBackgroundColor:[NSColor clearColor]];
    }
}

void mac_set_button_title_color(void *nsview_handle, unsigned char r, unsigned char g, unsigned char b)
{
    if (!nsview_handle)
        return;
    NSView *view = (__bridge NSView *)nsview_handle;
    if (![view isKindOfClass:[NSButton class]])
        return;
    NSButton *btn = (NSButton *)view;
    NSMutableAttributedString *title = [[NSMutableAttributedString alloc] initWithAttributedString:[btn attributedTitle]];
    NSColor *color = [NSColor colorWithSRGBRed:r/255.0 green:g/255.0 blue:b/255.0 alpha:1.0];
    [title addAttribute:NSForegroundColorAttributeName value:color range:NSMakeRange(0, [title length])];
    [btn setAttributedTitle:title];
}

// Native NSOutlineView controls (wxDataViewCtrl, e.g. the object list) draw their own selection and don't
// route through wxRendererNative, so the renderer-based tree theming can't reach them. wx's outline view is
// cell-based, so selection is painted by -[NSTableView highlightSelectionInClipRect:]; override it to fill
// selected rows with the themed muted-accent color instead of the system blue/gray. The theme only changes
// via a full process restart, so the color is fixed for the process lifetime and set once here.
static NSColor *s_table_selection_color = nil;
static IMP s_orig_highlightSelection = NULL;

static void preflight_highlightSelectionInClipRect(id self, SEL _cmd, NSRect clipRect)
{
    NSTableView *table = (NSTableView *) self;
    if (!s_table_selection_color)
    {
        if (s_orig_highlightSelection)
            ((void (*)(id, SEL, NSRect)) s_orig_highlightSelection)(self, _cmd, clipRect);
        return;
    }
    NSIndexSet *selected = [table selectedRowIndexes];
    if (selected.count == 0)
        return;
    [s_table_selection_color set];
    const NSRange rows = [table rowsInRect:clipRect];
    for (NSUInteger i = rows.location; i < NSMaxRange(rows); ++i)
        if ([selected containsIndex:i])
            NSRectFill([table rectOfRow:i]);
}

void mac_install_themed_table_selection(unsigned char r, unsigned char g, unsigned char b)
{
    s_table_selection_color = [NSColor colorWithSRGBRed:r / 255.0 green:g / 255.0 blue:b / 255.0 alpha:1.0];
    static dispatch_once_t once;
    dispatch_once(&once,
                  ^{
                      Method m =
                          class_getInstanceMethod([NSTableView class], @selector(highlightSelectionInClipRect:));
                      if (m)
                      {
                          s_orig_highlightSelection = method_getImplementation(m);
                          method_setImplementation(m, (IMP) preflight_highlightSelectionInClipRect);
                      }
                  });
}

void mac_set_insertion_point_color(void *nsview_handle, unsigned char r, unsigned char g, unsigned char b)
{
    if (!nsview_handle)
        return;
    NSView *view = (__bridge NSView *)nsview_handle;
    NSColor *color = [NSColor colorWithSRGBRed:r / 255.0 green:g / 255.0 blue:b / 255.0 alpha:1.0];
    // Multiline controls are NSTextView; single-line wxTextCtrl is an NSTextField whose caret is drawn by
    // the window's shared field editor (only valid while the field is first responder).
    if ([view isKindOfClass:[NSTextView class]])
    {
        [(NSTextView *) view setInsertionPointColor:color];
    }
    else if ([view isKindOfClass:[NSTextField class]])
    {
        NSTextField *tf = (NSTextField *) view;
        NSText *editor = [[tf window] fieldEditor:YES forObject:tf];
        if ([editor isKindOfClass:[NSTextView class]])
            [(NSTextView *) editor setInsertionPointColor:color];
    }
}

void mac_set_focus_ring_none(void *nsview_handle)
{
    if (!nsview_handle)
        return;
    NSView *view = (__bridge NSView *)nsview_handle;
    // Suppress the native blue focus ring; themed inputs draw their own focused border.
    [view setFocusRingType:NSFocusRingTypeNone];
}

void mac_set_button_bezel_color(void *nsview_handle, unsigned char r, unsigned char g, unsigned char b)
{
    if (!nsview_handle)
        return;
    NSView *view = (__bridge NSView *)nsview_handle;
    if (![view isKindOfClass:[NSButton class]])
        return;
    // Tints the rounded push-button bezel so dialog buttons follow the active theme instead of the
    // system default blue/gray. macOS draws a flat colored bezel when bezelColor is set.
    [(NSButton *)view setBezelColor:[NSColor colorWithSRGBRed:r / 255.0 green:g / 255.0 blue:b / 255.0 alpha:1.0]];
}

void mac_set_button_image_dims_when_disabled(void *nsview_handle, bool dims)
{
    if (!nsview_handle)
        return;
    NSView *view = (__bridge NSView *)nsview_handle;
    if (![view isKindOfClass:[NSButton class]])
        return;
    NSCell *cell = [(NSButton *)view cell];
    if ([cell isKindOfClass:[NSButtonCell class]])
        [(NSButtonCell *)cell setImageDimsWhenDisabled:dims];
}

void mac_set_view_corner_radius(void *nsview_handle, double radius)
{
    if (!nsview_handle)
        return;
    NSView *view = (__bridge NSView *)nsview_handle;
    [view setWantsLayer:YES];
    view.layer.cornerRadius = radius;
    view.layer.masksToBounds = YES;
}

}

@end
