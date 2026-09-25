///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2019 David Kocík @kocikdav
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#import <Cocoa/Cocoa.h>

@interface RemovableDriveManagerMM : NSObject

-(instancetype) init;
-(void) add_unmount_observer;
-(void) on_device_unmount: (NSNotification*) notification;
-(NSArray*) list_dev;
-(void)eject_drive:(NSString *)path;
@end
