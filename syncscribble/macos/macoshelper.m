#import <AppKit/AppKit.h>
#include "macoshelper.h"
#include "ugui/svggui_platform.h"  // for SDL and pen ids

void macosWaitEvent(void)
{
  // indefinite wait + dequeue:NO freezes w/ 100% CPU on scaling change, w/o ever returning to our code
  //  a note in GLFW source mentions the same problem
  //[NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate dateWithTimeIntervalSinceNow:2] inMode:NSDefaultRunLoopMode dequeue:NO];
  NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantFuture]
      inMode:NSDefaultRunLoopMode dequeue:YES];
  if(event)
    [NSApp sendEvent:event];
}

void macosWakeEventLoop(void)
{
  // send an empty event; autoreleasepool needed because this can be called from a different thread
  @autoreleasepool {
    NSEvent* event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSMakePoint(0,0)
        modifierFlags:0 timestamp:0.0 windowNumber:0 context:nil subtype:0 data1:0 data2:0];
    [NSApp postEvent:event atStart:YES];
  }
}

void macosDisableMouseCoalescing(void)
{
  NSEvent.mouseCoalescingEnabled = NO;
}

int macosOpenUrl(const char* url)
{
  return [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@(url)]] == YES;
}

int macosClipboardChangeCount(void)
{
  return [NSPasteboard generalPasteboard].changeCount;
}

//#import "SDL/src/video/cocoa/SDL_cocoawindow.h"
@interface Cocoa_WindowListener : NSResponder <NSWindowDelegate>
- (void)mouseDown:(NSEvent *)event;
- (void)mouseMoved:(NSEvent *)event;
- (void)mouseUp:(NSEvent *)event;
- (void)tabletPoint:(NSEvent *)event;
- (void)tabletProximity:(NSEvent *)event;
@end

@implementation Cocoa_WindowListener(TabletListener)

// [event subtype] throws exception if event is not mouse event, so only check for mouse events
static void handleMouseEvent(NSEvent* event, int subtype, int eventtype)
{
  static unsigned int penPointerType = PenPointerPen;
  static unsigned int prevTabletButtons = 0;
  static unsigned int prevMouseButtons = 0;

  SDL_Event sdlevent = {0};
  if ([event type] == NSEventTypeTabletProximity || subtype == NSEventSubtypeTabletProximity) {
    NSEvent.mouseCoalescingEnabled = NO;  // this is somehow being reenabled after we disable on startup
    //NSLog(@"Got proximity event: %d", (int)[event pointingDeviceType]);
    penPointerType = [event pointingDeviceType] == NSPointingDeviceTypeEraser ? PenPointerEraser : PenPointerPen;
    return;  // no event to send
  }

  sdlevent.type = eventtype;
  NSPoint pos = [event locationInWindow];
  pos.y = NSHeight(event.window.contentView.frame) - pos.y;
  sdlevent.tfinger.x = pos.x;
  sdlevent.tfinger.y = pos.y;
  sdlevent.tfinger.timestamp = SDL_GetTicks();  //fmod([event timestamp]/1000, INT_MAX)
  if ([event type] == NSEventTypeTabletPoint || subtype == NSEventSubtypeTabletPoint) {
    sdlevent.tfinger.touchId = penPointerType;
    sdlevent.tfinger.pressure = [event pressure];
    NSEventButtonMask buttons = [event buttonMask];
    unsigned int sdlButtons = (buttons & NSEventButtonMaskPenTip) ? SDL_BUTTON_LMASK : 0;
    if(buttons & NSEventButtonMaskPenLowerSide) sdlButtons |= SDL_BUTTON_RMASK;  //SDL_BUTTON_MMASK;
    if(buttons & NSEventButtonMaskPenUpperSide) sdlButtons |= SDL_BUTTON_RMASK;
    sdlevent.tfinger.fingerId = sdlevent.type == SDL_FINGERMOTION ? sdlButtons : (sdlButtons ^ prevTabletButtons);
    prevTabletButtons = sdlButtons;
  }
  else {
    sdlevent.tfinger.touchId = SDL_TOUCH_MOUSEID;
    sdlevent.tfinger.pressure = 1;
    NSEventButtonMask buttons = [NSEvent pressedMouseButtons];
    unsigned int sdlButtons = (buttons & 0x01) ? SDL_BUTTON_LMASK : 0;
    if(buttons & 0x02) sdlButtons |= SDL_BUTTON_RMASK;
    if(buttons & ~0x03) sdlButtons |= SDL_BUTTON_MMASK;
    sdlevent.tfinger.fingerId = sdlevent.type == SDL_FINGERMOTION ? sdlButtons : (sdlButtons ^ prevMouseButtons);
    prevMouseButtons = sdlButtons;
  }
  // PeepEvents bypasses gesture recognizer and event filters
  SDL_PeepEvents(&sdlevent, 1, SDL_ADDEVENT, 0, 0);  //SDL_PushEvent(&event);
}

- (void)mouseDown:(NSEvent *)event
{
  handleMouseEvent(event, [event subtype], SDL_FINGERDOWN);
}

- (void)mouseMoved:(NSEvent *)event
{
  handleMouseEvent(event, [event subtype], SDL_FINGERMOTION);
}

- (void)mouseUp:(NSEvent *)event
{
  handleMouseEvent(event, [event subtype], SDL_FINGERUP);
}

// this should work - tried rebuilding SDL with keyDown removed from SDL_cocoawindow.m and keyDown here was
//  still called
// according to https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/EventOverview/HandlingTabletEvents/HandlingTabletEvents.html
//  tablet points are usually sent as mouse events with NSEventSubtypeTabletPoint, but some drivers in some
//  cases may send NSEventTypeTabletPoint events, so both should be handled
// another potential ref: github.com/ixchow/kit/blob/master/kit-SDL2-osx.mm
- (void)tabletPoint:(NSEvent *)event
{
  handleMouseEvent(event, -1, SDL_FINGERMOTION);
}

- (void)tabletProximity:(NSEvent *)event
{
  handleMouseEvent(event, -1, SDL_FINGERMOTION);
}

// we might need to intercept all mouse events
/*
 -(void) mouseDown:(NSEvent *) theEvent;
 -(void) rightMouseDown:(NSEvent *) theEvent;
 -(void) otherMouseDown:(NSEvent *) theEvent;
 -(void) mouseUp:(NSEvent *) theEvent;
 -(void) rightMouseUp:(NSEvent *) theEvent;
 -(void) otherMouseUp:(NSEvent *) theEvent;
 -(void) mouseMoved:(NSEvent *) theEvent;
 -(void) mouseDragged:(NSEvent *) theEvent;
 -(void) rightMouseDragged:(NSEvent *) theEvent;
 -(void) otherMouseDragged:(NSEvent *) theEvent;
 */

@end
