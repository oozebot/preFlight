///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2020 - 2021 David Kocík @kocikdav
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#import <Cocoa/Cocoa.h>

@interface OtherInstanceMessageHandlerMac : NSObject

-(instancetype) init;
-(void) add_observer:(NSString *)version;
-(void) message_update:(NSNotification *)note;
-(void) message_multicast_update:(NSNotification *)note;
-(void) closing_update:(NSNotification *)note;
-(void) bring_forward;
@end
