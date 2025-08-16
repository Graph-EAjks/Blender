/* SPDX-FileCopyrightText: 2025 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "GHOST_C-api.h"
#include "GHOST_ContextNone.hh"
#include "GHOST_Debug.hh"
#include "GHOST_EventButton.hh"
#include "GHOST_EventCursor.hh"
#include "GHOST_EventDragnDrop.hh"
#include "GHOST_EventKey.hh"
#include "GHOST_EventTouch.hh"
#include "GHOST_EventTrackpad.hh"
#include "GHOST_EventWheel.hh"
#include "GHOST_SystemIOS.h"
#include "GHOST_TimerManager.hh"
#include "GHOST_TimerTask.hh"
#include "GHOST_Types.h"
#include "GHOST_WindowIOS.h"
#include "GHOST_WindowManager.hh"

#include "GHOST_ContextIOS.hh"

#include <CoreGraphics/CoreGraphics.h>
#include <Metal/Metal.h>
#include <MetalKit/MTKDefines.h>
#include <QuartzCore/QuartzCore.h>
#include <UIKit/UIKit.h>

#include <sys/sysctl.h>

#import <GameController/GameController.h>
#import <MetalKit/MTKDefines.h>
#import <MetalKit/MTKView.h>
#import <UIKit/UIKit.h>
#import <UIKit/UIPencilInteraction.h>

#include <unordered_map>

// #define IOS_INPUT_LOGGING
#if defined(IOS_INPUT_LOGGING)
#  define IOS_INPUT_LOG(...) NSLog(__VA_ARGS__)
#else
#  define IOS_INPUT_LOG(...)
#endif

// #define IOS_WINDOW_LOGGING
#if defined(IOS_WINDOW_LOGGING)
#  define IOS_WINDOW_LOG(...) NSLog(format, __VA_ARGS__)
#else
#  define IOS_WINDOW_LOG(...)
#endif

extern "C" {

struct bContext;
static bContext *C = NULL;
}

int argc = 0;
const char **argv = nullptr;

/* Global vars that track the window currently valid from the iOS POV. */
GHOST_WindowIOS *current_active_window = nullptr;
GHOST_WindowIOS *next_active_window = nullptr;

/* Implemented in wm.cc. */
void WM_main_loop_body(bContext *C);
int main_ios_callback(int argc, const char **argv);

struct TouchData {
  CGPoint pos;
  bool part_of_multitouch = false;
};

typedef struct UserInputEvent {
  enum EventTypes {
    CURSOR_MOVE,
    PAN_GESTURE,
    PAN_GESTURE_TWO_FINGERS,
    PINCH_GESTURE,
    ORBIT_GESTURE,
    LEFT_BUTTON_DOWN,
    LEFT_BUTTON_UP,
    MIDDLE_BUTTON_DOWN,
    MIDDLE_BUTTON_UP,
    SHIFT_KEY_DOWN,
    SHIFT_KEY_UP,
    ALT_KEY_DOWN,
    ALT_KEY_UP,
    PENCIL_TAP
  };
  EventTypes event_list[10];
  int num_events;
  CGPoint location;
  CGPoint translation;
  CGFloat distance;
  CGFloat rotation;
  bool pencil_used;

  UserInputEvent(CGPoint *loc, CGPoint *tran, CGFloat *dist, bool pencil, CGFloat *rot = nullptr)
  {
    num_events = 0;
    location = loc ? *loc : CGPointMake(-1.0f, -1.0f);
    translation = tran ? *tran : CGPointMake(0.0f, 0.0f);
    distance = dist ? *dist : 0.0f;
    rotation = rot ? *rot : 0.0f;
    pencil_used = pencil;
  }

  void add_event(EventTypes event_type)
  {
    GHOST_ASSERT(num_events <= sizeof(event_list) / sizeof(*event_list),
                 "add_event: Failed to add event");
    event_list[num_events] = event_type;
    num_events++;
  }

  NSString *getEventTypeDesc(EventTypes event_type) const
  {
    switch (event_type) {
      case CURSOR_MOVE:
        return @"CM";
      case PAN_GESTURE:
        return @"PAN";
      case PAN_GESTURE_TWO_FINGERS:
        return @"PAN2F";
      case PINCH_GESTURE:
        return @"PINCH";
      case LEFT_BUTTON_DOWN:
        return @"LB-DOWN";
      case LEFT_BUTTON_UP:
        return @"LB-UP";
      case MIDDLE_BUTTON_DOWN:
        return @"MB-DOWN";
      case MIDDLE_BUTTON_UP:
        return @"MB-UP";
      case SHIFT_KEY_DOWN:
        return @"SHIFT-DOWN";
      case SHIFT_KEY_UP:
        return @"SHIFT-UP";
      case ALT_KEY_DOWN:
        return @"ALT-DOWN";
      case ALT_KEY_UP:
        return @"ALT-UP";
      case ORBIT_GESTURE:
        return @"ORBIT";
      case PENCIL_TAP:
        return @"PENCIL-TAP";
    }
    BLI_assert_unreachable();
    return @"Event undefined";
  }

} UserInputEvent;

/* GHOSTUITapGesture interface for capturing taps. */
@interface GHOSTUITapGestureRecognizer : UITapGestureRecognizer

- (CGPoint)getScaledTouchPoint:(GHOST_WindowIOS *)window;

@end

@implementation GHOSTUITapGestureRecognizer

- (CGPoint)getScaledTouchPoint:(GHOST_WindowIOS *)window
{
  CGPoint touch_point = [self locationInView:window->getView()];
  return window->scalePointToWindow(touch_point);
}

@end

/* GHOSTUITapGesture interface for capturing taps. */
@interface GHOSTUIPanGestureRecognizer : UIPanGestureRecognizer
{
  CGPoint cached_translation;
}
- (CGPoint)getScaledTouchPoint:(GHOST_WindowIOS *)window;
- (CGPoint)getScaledTranslation:(GHOST_WindowIOS *)window;

- (void)setCachedTranslation:(CGPoint)translation;
- (CGPoint)getCachedTranslation;
@end

@implementation GHOSTUIPanGestureRecognizer

- (CGPoint)getScaledTouchPoint:(GHOST_WindowIOS *)window
{
  CGPoint touch_point = [self locationInView:window->getView()];
  return window->scalePointToWindow(touch_point);
}

- (CGPoint)getScaledTranslation:(GHOST_WindowIOS *)window
{
  CGPoint translation = [self translationInView:window->getView()];
  return window->scalePointToWindow(translation);
}

- (CGPoint)getRelativeTranslation:(CGPoint)translation
{
  CGPoint relative_translation;
  relative_translation.x = translation.x - cached_translation.x;
  relative_translation.y = translation.y - cached_translation.y;
  return relative_translation;
}

- (void)setCachedTranslation:(CGPoint)translation
{
  cached_translation = translation;
}

- (CGPoint)getCachedTranslation
{
  return cached_translation;
}
@end

@interface GHOSTUIHoverGestureRecognizer : UIHoverGestureRecognizer
- (CGPoint)getScaledTouchPoint:(GHOST_WindowIOS *)window;
@end

@implementation GHOSTUIHoverGestureRecognizer

- (CGPoint)getScaledTouchPoint:(GHOST_WindowIOS *)window
{
  CGPoint touch_point = [self locationInView:window->getView()];
  return window->scalePointToWindow(touch_point);
}
@end

@interface GHOSTUIPinchGestureRecognizer : UIPinchGestureRecognizer
{
  CGFloat cached_distance;
}
- (CGPoint)getScaledTouchPoint:(GHOST_WindowIOS *)window touch_id:(int)touch_id;
- (CGFloat)getScaledDistance:(GHOST_WindowIOS *)window;
- (CGPoint)getPinchMidpoint:(GHOST_WindowIOS *)window;
- (void)setCachedDistance:(CGFloat)distance;
- (CGFloat)getCachedDistance;
@end

@interface GHOSTUIRotationGestureRecognizer : UIRotationGestureRecognizer
{
  CGFloat cached_rotation;
}
- (CGPoint)getScaledTouchPoint:(GHOST_WindowIOS *)window touch_id:(int)touch_id;
- (CGPoint)getRotationCenter:(GHOST_WindowIOS *)window;
- (CGFloat)getScaledRotation;
- (CGFloat)getRelativeRotation:(CGFloat)rotation;
- (void)setCachedRotation:(CGFloat)rotation;
- (CGFloat)getCachedRotation;
@end

@implementation GHOSTUIPinchGestureRecognizer
- (CGPoint)getScaledTouchPoint:(GHOST_WindowIOS *)window touch_id:(int)touch_id
{
  CGPoint touch_point = [self locationOfTouch:touch_id inView:window->getView()];
  return window->scalePointToWindow(touch_point);
}

- (CGFloat)getScaledDistance:(GHOST_WindowIOS *)window
{
  CGPoint touch_point0 = [self locationOfTouch:0 inView:window->getView()];
  CGPoint touch_point1 = [self locationOfTouch:1 inView:window->getView()];
  touch_point0 = window->scalePointToWindow(touch_point0);
  touch_point1 = window->scalePointToWindow(touch_point1);
  float dx = touch_point1.x - touch_point0.x;
  float dy = touch_point1.y - touch_point0.y;
  CGFloat point_distance = sqrt(dx * dx + dy * dy);
  return point_distance;
}

- (CGPoint)getPinchMidpoint:(GHOST_WindowIOS *)window
{
  CGPoint touch_point0 = [self locationOfTouch:0 inView:window->getView()];
  CGPoint touch_point1 = [self locationOfTouch:1 inView:window->getView()];
  touch_point0 = window->scalePointToWindow(touch_point0);
  touch_point1 = window->scalePointToWindow(touch_point1);
  CGPoint midPoint = CGPointMake((touch_point0.x + touch_point1.x) / 2.0f,
                                 (touch_point0.y + touch_point1.y) / 2.0f);
  return midPoint;
}

- (void)setCachedDistance:(CGFloat)distance
{
  cached_distance = distance;
}

- (CGFloat)getCachedDistance
{
  return cached_distance;
}
@end

@implementation GHOSTUIRotationGestureRecognizer
- (CGPoint)getScaledTouchPoint:(GHOST_WindowIOS *)window touch_id:(int)touch_id
{
  CGPoint touch_point = [self locationOfTouch:touch_id inView:window->getView()];
  return window->scalePointToWindow(touch_point);
}

- (CGPoint)getRotationCenter:(GHOST_WindowIOS *)window
{
  CGPoint touch_point0 = [self locationOfTouch:0 inView:window->getView()];
  CGPoint touch_point1 = [self locationOfTouch:1 inView:window->getView()];
  touch_point0 = window->scalePointToWindow(touch_point0);
  touch_point1 = window->scalePointToWindow(touch_point1);
  CGPoint centerPoint = CGPointMake((touch_point0.x + touch_point1.x) / 2.0f,
                                    (touch_point0.y + touch_point1.y) / 2.0f);
  return centerPoint;
}

- (CGFloat)getScaledRotation
{
  return self.rotation;
}

- (CGFloat)getRelativeRotation:(CGFloat)rotation
{
  CGFloat relative_rotation = rotation - cached_rotation;
  return relative_rotation;
}

- (void)setCachedRotation:(CGFloat)rotation
{
  cached_rotation = rotation;
}

- (CGFloat)getCachedRotation
{
  return cached_rotation;
}
@end

/* GHOSTUIWindow interface. */
@interface GHOSTUIWindow : UIWindow <UIGestureRecognizerDelegate, UIPencilInteractionDelegate>
{
  GHOST_SystemIOS *system;
  GHOST_WindowIOS *window;
  int touch_stack;
  std::unordered_map<uint64_t, TouchData> touchmap;

  GHOSTUITapGestureRecognizer *tap_gesture_recognizer;
  GHOSTUITapGestureRecognizer *tap2f_gesture_recognizer;
  GHOSTUITapGestureRecognizer *tap3f_gesture_recognizer;
  GHOSTUITapGestureRecognizer *tap4f_gesture_recognizer;
  GHOSTUIPanGestureRecognizer *pan_gesture_recognizer;
  GHOSTUIPanGestureRecognizer *pan_pencil_gesture_recognizer;
  GHOSTUIPanGestureRecognizer *pan2f_gesture_recognizer;
  GHOSTUIPinchGestureRecognizer *zoom_gesture_recognizer;
  GHOSTUIRotationGestureRecognizer *rotation_gesture_recognizer;
  GHOSTUIHoverGestureRecognizer *hover_gesture_recognizer;
  UIPencilInteraction *pencil_interaction;
  UIScreenEdgePanGestureRecognizer *edge_swipe_left;
  UIScreenEdgePanGestureRecognizer *edge_swipe_right;
  // GHOSTUILongPressGestureRecognizer *long_press_gesture_recognizer;

  /* Data from the Apple pencil */
  UITouch *current_pencil_touch;
  GHOST_TabletData tablet_data;
  bool last_tap_with_pencil;

  /* Keyboard handling. */
  UITextField *text_field;
  NSString *original_text;
  bool onscreen_keyboard_active;
  const char *text_field_string;
  GHOST_KeyboardProperties current_keyboard_properties;
  bool external_keyboard_connected;

  /* Toolbar */
  bool toolbar_enabled;
  UIToolbar *toolbar;
  UIBarButtonItem *toolbar_tip_item;
  UIBarButtonItem *toolbar_live_text_item;
  UIBarButtonItem *toolbar_done_editing_item;
  UIBarButtonItem *toolbar_cancel_editing_item;

  /* Direct event handling state tracking */
  std::unordered_map<uint64_t, GHOST_TButton> touch_button_map;
}

- (void)setSystemAndWindowIOS:(GHOST_SystemIOS *)sysCocoa windowIOS:(GHOST_WindowIOS *)winCocoa;

/* Blender event generation. */
- (void)generateUserInputEvents:(const UserInputEvent &)event_info;

/* Gesture recognizers. */
- (void)registerGestureRecognizers;
- (BOOL)gestureRecognizer:(UIGestureRecognizer *)gestureRecognizer
    shouldRecognizeSimultaneouslyWithGestureRecognizer:
        (UIGestureRecognizer *)otherGestureRecognizer;
- (void)handleTap:(GHOSTUITapGestureRecognizer *)sender;
- (void)handlePan:(GHOSTUIPanGestureRecognizer *)sender;
- (void)handleFingerPan:(GHOSTUIPanGestureRecognizer *)sender;
- (void)handlePencilPan:(GHOSTUIPanGestureRecognizer *)sender;
- (void)handlePan2f:(GHOSTUIPanGestureRecognizer *)sender;
- (void)handleZoom:(GHOSTUIPinchGestureRecognizer *)sender;
- (void)handleRotation:(GHOSTUIRotationGestureRecognizer *)sender;
- (void)handleHover:(GHOSTUIHoverGestureRecognizer *)sender;
- (void)handleTap2F:(GHOSTUITapGestureRecognizer *)sender;
- (void)handleTap3F:(GHOSTUITapGestureRecognizer *)sender;
- (void)handleTap4F:(GHOSTUITapGestureRecognizer *)sender;

/* Direct event handling bypass methods */
- (void)sendEvent:(UIEvent *)event;
- (void)handleDirectTouchEvent:(UIEvent *)event;
- (GHOST_TButton)getMouseButtonFromTouch:(UITouch *)touch withEvent:(UIEvent *)event;

/* Direct keyboard handling methods */
- (void)handleDirectKeyboardEvent:(UIEvent *)event;
- (GHOST_TKey)convertIOSKeyToGHOST:(NSString *)key;
- (void)handleKeyPress:(UIPress *)press withEvent:(UIPressesEvent *)event;

/* On screen keyboard handling */
- (UITextField *)getUITextField;
- (const GHOST_TabletData)getTabletData;
- (GHOST_TSuccess)popupOnscreenKeyboard:(const GHOST_KeyboardProperties &)keyboard_properties;
- (GHOST_TSuccess)hideOnscreenKeyboard;
- (const char *)getLastKeyboardString;

/* UIKit keyboard press handling (fallback when not bypassing) */
- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event;
- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event;
- (void)pressesCancelled:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event;
@end

@implementation GHOSTUIWindow
- (void)setSystemAndWindowIOS:(GHOST_SystemIOS *)sys windowIOS:(GHOST_WindowIOS *)win
{
  system = sys;
  window = win;
  touch_stack = 0;
  text_field = nil;
  original_text = nil;
  onscreen_keyboard_active = false;
  text_field_string = nullptr;
  current_pencil_touch = nil;
  tablet_data = GHOST_TABLET_DATA_NONE;
  toolbar_enabled = true;
  toolbar = nil;
  last_tap_with_pencil = false;
  external_keyboard_connected = [GCKeyboard coalescedKeyboard] != nil;

  /* Initialize direct event handling state. */
  touch_button_map.clear();

  /* Register for notifications of changes to the onscreen keyboard. */
  [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(keyboardWillChange:)
                                               name:UIKeyboardWillChangeFrameNotification
                                             object:nil];
  [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(keyboardWillChange:)
                                               name:UIKeyboardWillShowNotification
                                             object:nil];
  [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(keyboardWillChange:)
                                               name:UIKeyboardWillHideNotification
                                             object:nil];

  /* Check whether we've linked the GameController framework.  */
  if (&GCKeyboardDidConnectNotification != NULL) {
    /* Register for notifcations an external keyboard has been added/removed. */
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(externalKeyboardChange:)
                                                 name:GCKeyboardDidConnectNotification
                                               object:nil];

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(externalKeyboardChange:)
                                                 name:GCKeyboardDidDisconnectNotification
                                               object:nil];
  }
}

- (void)registerGestureRecognizers
{
  /** Create Gesture recognisers. */
  /* Tap gesture recognizer. */
  tap_gesture_recognizer = [[GHOSTUITapGestureRecognizer alloc]
      initWithTarget:self
              action:@selector(handleTap:)];
  tap_gesture_recognizer.delegate = self;
  tap_gesture_recognizer.cancelsTouchesInView = false;
  tap_gesture_recognizer.allowedTouchTypes = @[ @(UITouchTypePencil), @(UITouchTypeDirect) ];
  [window->getView() addGestureRecognizer:tap_gesture_recognizer];

  /* Two-finger tap gesture recognizer. */
  tap2f_gesture_recognizer = [[GHOSTUITapGestureRecognizer alloc]
      initWithTarget:self
              action:@selector(handleTap2F:)];
  tap2f_gesture_recognizer.delegate = self;
  tap2f_gesture_recognizer.cancelsTouchesInView = false;
  tap2f_gesture_recognizer.delaysTouchesBegan = YES;
  tap2f_gesture_recognizer.numberOfTouchesRequired = 2;
  tap2f_gesture_recognizer.allowedTouchTypes = @[ @(UITouchTypeDirect) ];
  [window->getView() addGestureRecognizer:tap2f_gesture_recognizer];

  /* Three-finger tap gesture recognizer. */
  tap3f_gesture_recognizer = [[GHOSTUITapGestureRecognizer alloc]
      initWithTarget:self
              action:@selector(handleTap3F:)];
  tap3f_gesture_recognizer.delegate = self;
  tap3f_gesture_recognizer.cancelsTouchesInView = false;
  tap3f_gesture_recognizer.delaysTouchesBegan = YES;
  tap3f_gesture_recognizer.numberOfTouchesRequired = 3;
  tap3f_gesture_recognizer.allowedTouchTypes = @[ @(UITouchTypeDirect) ];
  [window->getView() addGestureRecognizer:tap3f_gesture_recognizer];

  /* Four-finger tap gesture recognizer. */
  tap4f_gesture_recognizer = [[GHOSTUITapGestureRecognizer alloc]
      initWithTarget:self
              action:@selector(handleTap4F:)];
  tap4f_gesture_recognizer.delegate = self;
  tap4f_gesture_recognizer.cancelsTouchesInView = false;
  tap4f_gesture_recognizer.delaysTouchesBegan = YES;
  tap4f_gesture_recognizer.numberOfTouchesRequired = 4;
  tap4f_gesture_recognizer.allowedTouchTypes = @[ @(UITouchTypeDirect) ];
  [window->getView() addGestureRecognizer:tap4f_gesture_recognizer];

  /* Pan gesture recognizer for finger input. */
  pan_gesture_recognizer = [[GHOSTUIPanGestureRecognizer alloc]
      initWithTarget:self
              action:@selector(handlePan:)];
  pan_gesture_recognizer.delegate = self;
  pan_gesture_recognizer.cancelsTouchesInView = false;
  pan_gesture_recognizer.minimumNumberOfTouches = 1;
  pan_gesture_recognizer.maximumNumberOfTouches = 1;
  pan_gesture_recognizer.allowedTouchTypes =
      @[ @(UITouchTypeDirect), @(UITouchTypeIndirectPointer), @(UITouchTypeIndirect) ];
  [window->getView() addGestureRecognizer:pan_gesture_recognizer];

  /* Pan gesture recognizer for pencil input. */
  pan_pencil_gesture_recognizer = [[GHOSTUIPanGestureRecognizer alloc]
      initWithTarget:self
              action:@selector(handlePencilPan:)];
  pan_pencil_gesture_recognizer.delegate = self;
  pan_pencil_gesture_recognizer.cancelsTouchesInView = false;
  pan_pencil_gesture_recognizer.minimumNumberOfTouches = 1;
  pan_pencil_gesture_recognizer.maximumNumberOfTouches = 1;
  pan_pencil_gesture_recognizer.allowedTouchTypes = @[ @(UITouchTypePencil) ];
  [window->getView() addGestureRecognizer:pan_pencil_gesture_recognizer];

  /* Pan gesture recognizer - two fingers 3D UI. */
  pan2f_gesture_recognizer = [[GHOSTUIPanGestureRecognizer alloc]
      initWithTarget:self
              action:@selector(handlePan2f:)];
  pan2f_gesture_recognizer.delegate = self;
  pan2f_gesture_recognizer.cancelsTouchesInView = false;
  pan2f_gesture_recognizer.minimumNumberOfTouches = 2;
  pan2f_gesture_recognizer.maximumNumberOfTouches = 2;
  pan2f_gesture_recognizer.allowedTouchTypes = @[ @(UITouchTypeDirect), @(UITouchTypeIndirect) ];
  [window->getView() addGestureRecognizer:pan2f_gesture_recognizer];

  /* Pinch/Zoom gesture recognizer. */
  zoom_gesture_recognizer = [[GHOSTUIPinchGestureRecognizer alloc]
      initWithTarget:self
              action:@selector(handleZoom:)];
  zoom_gesture_recognizer.delegate = self;
  zoom_gesture_recognizer.cancelsTouchesInView = false;
  zoom_gesture_recognizer.allowedTouchTypes = @[ @(UITouchTypeDirect), @(UITouchTypeIndirect) ];
  [window->getView() addGestureRecognizer:zoom_gesture_recognizer];

  /* Rotation gesture recognizer for 2-finger rotation. */
  rotation_gesture_recognizer = [[GHOSTUIRotationGestureRecognizer alloc]
      initWithTarget:self
              action:@selector(handleRotation:)];
  rotation_gesture_recognizer.delegate = self;
  rotation_gesture_recognizer.cancelsTouchesInView = false;
  rotation_gesture_recognizer.allowedTouchTypes =
      @[ @(UITouchTypeDirect), @(UITouchTypeIndirect) ];
  [window->getView() addGestureRecognizer:rotation_gesture_recognizer];

  /* Edge swipe. */
  edge_swipe_left = [[UIScreenEdgePanGestureRecognizer alloc]
      initWithTarget:self
              action:@selector(handleEdgeSwipe:)];
  edge_swipe_left.edges = UIRectEdgeLeft;
  edge_swipe_left.delegate = self;
  [window->getView() addGestureRecognizer:edge_swipe_left];

  edge_swipe_right = [[UIScreenEdgePanGestureRecognizer alloc]
      initWithTarget:self
              action:@selector(handleEdgeSwipe:)];
  edge_swipe_right.edges = UIRectEdgeRight;
  edge_swipe_right.delegate = self;
  [window->getView() addGestureRecognizer:edge_swipe_right];

  /* Apple Pencil hover recognizer. */
  hover_gesture_recognizer = [[GHOSTUIHoverGestureRecognizer alloc]
      initWithTarget:self
              action:@selector(handleHover:)];
  hover_gesture_recognizer.allowedTouchTypes =
      @[ @(UITouchTypePencil), @(UITouchTypeIndirectPointer) ];
  hover_gesture_recognizer.delegate = self;
  [window->getView() addGestureRecognizer:hover_gesture_recognizer];
  current_pencil_touch = nil;

  /**  Apple Pencil double-tap. */
  pencil_interaction = [[UIPencilInteraction alloc] init];
  pencil_interaction.delegate = self;
  [window->getView() addInteraction:pencil_interaction];
}

/* Turn the user inputs into Blender events. */
- (void)generateUserInputEvents:(const UserInputEvent &)event_info
{
  /* Lock access to ensure all input-events are received sequentially. */
  @synchronized(self) {
    for (int i = 0; i < event_info.num_events; i++) {
      UserInputEvent::EventTypes event_type = event_info.event_list[i];
      IOS_INPUT_LOG(@"%d-%@ %f,%f",
                    i,
                    event_info.getEventTypeDesc(event_type),
                    event_info.location.x,
                    event_info.location.y);

      switch (event_type) {
        case UserInputEvent::EventTypes::CURSOR_MOVE:
          system->pushEvent(
              new GHOST_EventCursor(GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
                                    GHOST_kEventCursorMove,
                                    window,
                                    event_info.location.x,
                                    event_info.location.y,
                                    tablet_data));
          break;
        case UserInputEvent::EventTypes::PAN_GESTURE:
          system->pushEvent(
              new GHOST_EventTrackpad(GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
                                      window,
                                      GHOST_kTrackpadEventScroll,
                                      event_info.location.x,
                                      event_info.location.y,
                                      event_info.translation.x,
                                      event_info.translation.y,
                                      false,
                                      1));
          break;
        case UserInputEvent::EventTypes::PAN_GESTURE_TWO_FINGERS:
          system->pushEvent(
              new GHOST_EventTrackpad(GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
                                      window,
                                      GHOST_kTrackpadEventScroll,
                                      event_info.location.x,
                                      event_info.location.y,
                                      event_info.translation.x,
                                      event_info.translation.y,
                                      true,
                                      2));
          break;
        case UserInputEvent::EventTypes::LEFT_BUTTON_DOWN:
          system->pushEvent(
              new GHOST_EventButton(GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
                                    GHOST_kEventButtonDown,
                                    window,
                                    GHOST_kButtonMaskLeft,
                                    tablet_data));
          break;
        case UserInputEvent::EventTypes::LEFT_BUTTON_UP:
          system->pushEvent(
              new GHOST_EventButton(GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
                                    GHOST_kEventButtonUp,
                                    window,
                                    GHOST_kButtonMaskLeft,
                                    tablet_data));
          break;
        case UserInputEvent::EventTypes::MIDDLE_BUTTON_DOWN:
          system->pushEvent(
              new GHOST_EventButton(GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
                                    GHOST_kEventButtonDown,
                                    window,
                                    GHOST_kButtonMaskMiddle,
                                    tablet_data));
          break;
        case UserInputEvent::EventTypes::MIDDLE_BUTTON_UP:
          system->pushEvent(
              new GHOST_EventButton(GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
                                    GHOST_kEventButtonUp,
                                    window,
                                    GHOST_kButtonMaskMiddle,
                                    tablet_data));
          break;
        case UserInputEvent::EventTypes::PINCH_GESTURE:
          system->pushEvent(
              new GHOST_EventTrackpad(GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
                                      window,
                                      GHOST_kTrackpadEventMagnify,
                                      event_info.location.x,
                                      event_info.location.y,
                                      event_info.distance,
                                      0,
                                      false,
                                      2));
          break;
        case UserInputEvent::EventTypes::ORBIT_GESTURE:
          system->pushEvent(
              new GHOST_EventTrackpad(GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
                                      window,
                                      GHOST_kTrackpadEventScroll,
                                      event_info.location.x,
                                      event_info.location.y,
                                      event_info.translation.x,
                                      event_info.translation.y,
                                      true,
                                      2));
          break;
        case UserInputEvent::EventTypes::SHIFT_KEY_DOWN:
          system->pushEvent(new GHOST_EventKey(GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
                                               GHOST_kEventKeyDown,
                                               window,
                                               GHOST_kKeyLeftShift,
                                               false,
                                               nullptr));
          break;
        case UserInputEvent::EventTypes::SHIFT_KEY_UP:
          system->pushEvent(new GHOST_EventKey(GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
                                               GHOST_kEventKeyUp,
                                               window,
                                               GHOST_kKeyLeftShift,
                                               false,
                                               nullptr));
          break;
        case UserInputEvent::EventTypes::ALT_KEY_DOWN:
          system->pushEvent(new GHOST_EventKey(GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
                                               GHOST_kEventKeyDown,
                                               window,
                                               GHOST_kKeyLeftAlt,
                                               false,
                                               nullptr));
          break;
        case UserInputEvent::EventTypes::ALT_KEY_UP:
          system->pushEvent(new GHOST_EventKey(GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
                                               GHOST_kEventKeyUp,
                                               window,
                                               GHOST_kKeyLeftAlt,
                                               false,
                                               nullptr));
          break;
        case UserInputEvent::EventTypes::PENCIL_TAP:
          system->pushEvent(
              new GHOST_EventButton(GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
                                    GHOST_kEventButtonDown,
                                    window,
                                    GHOST_kButtonMaskRight,
                                    tablet_data));
          break;
        default:
          GHOST_ASSERT(FALSE, "GHOST_SystemIOS::generateUserInputEvents unsupported event type");
      }
    }
  }
}

/* Allow simultaneous gestures for two finger pans, zooms, and rotations but nothing else. */
- (BOOL)gestureRecognizer:(UIGestureRecognizer *)gestureRecognizer
    shouldRecognizeSimultaneouslyWithGestureRecognizer:
        (UIGestureRecognizer *)otherGestureRecognizer
{
  /* Allow tap gestures to work alongside any other gesture. */
  if (gestureRecognizer == tap_gesture_recognizer ||
      otherGestureRecognizer == tap_gesture_recognizer ||
      gestureRecognizer == tap2f_gesture_recognizer ||
      otherGestureRecognizer == tap2f_gesture_recognizer ||
      gestureRecognizer == tap3f_gesture_recognizer ||
      otherGestureRecognizer == tap3f_gesture_recognizer ||
      gestureRecognizer == tap4f_gesture_recognizer ||
      otherGestureRecognizer == tap4f_gesture_recognizer)
  {
    return YES;
  }

  if (gestureRecognizer == pan2f_gesture_recognizer &&
      otherGestureRecognizer == zoom_gesture_recognizer)
  {
    return YES;
  }
  if (gestureRecognizer == pan_gesture_recognizer &&
      otherGestureRecognizer == zoom_gesture_recognizer)
  {
    return YES;
  }
  if (gestureRecognizer == pan_pencil_gesture_recognizer &&
      otherGestureRecognizer == zoom_gesture_recognizer)
  {
    return YES;
  }
  if (gestureRecognizer == pan2f_gesture_recognizer &&
      otherGestureRecognizer == rotation_gesture_recognizer)
  {
    return YES;
  }
  if (gestureRecognizer == zoom_gesture_recognizer &&
      otherGestureRecognizer == rotation_gesture_recognizer)
  {
    return YES;
  }
  return NO;
}

/* Control gesture priority - rotation, zoom, and pan need careful coordination. */
- (BOOL)gestureRecognizerShouldBegin:(UIGestureRecognizer *)gestureRecognizer
{
  if (gestureRecognizer == rotation_gesture_recognizer) {
    if (zoom_gesture_recognizer.state == UIGestureRecognizerStateBegan ||
        zoom_gesture_recognizer.state == UIGestureRecognizerStateChanged)
    {
      CGFloat scale = zoom_gesture_recognizer.scale;
      if (fabs(scale - 1.0) > 0.03) {
        return NO;
      }
    }

    if (pan2f_gesture_recognizer.state == UIGestureRecognizerStateBegan ||
        pan2f_gesture_recognizer.state == UIGestureRecognizerStateChanged)
    {
      CGPoint translation = [pan2f_gesture_recognizer getScaledTranslation:window];
      CGFloat translationMagnitude = sqrt(translation.x * translation.x +
                                          translation.y * translation.y);
      if (translationMagnitude > 5.0) {
        return NO;
      }
    }
  }

  if (gestureRecognizer == pan2f_gesture_recognizer) {
    if (zoom_gesture_recognizer.state == UIGestureRecognizerStatePossible ||
        zoom_gesture_recognizer.state == UIGestureRecognizerStateBegan ||
        zoom_gesture_recognizer.state == UIGestureRecognizerStateChanged)
    {

      CGFloat scale = zoom_gesture_recognizer.scale;
      if (fabs(scale - 1.0) > 0.05) {
        return NO;
      }
    }

    if (rotation_gesture_recognizer.state == UIGestureRecognizerStateBegan ||
        rotation_gesture_recognizer.state == UIGestureRecognizerStateChanged)
    {
      CGFloat rotation = fabs([rotation_gesture_recognizer getScaledRotation]);
      if (rotation > 0.05) {
        return NO;
      }
    }
  }

  if (gestureRecognizer == zoom_gesture_recognizer) {
    if (pan2f_gesture_recognizer.state == UIGestureRecognizerStateBegan ||
        pan2f_gesture_recognizer.state == UIGestureRecognizerStateChanged)
    {

      CGPoint translation = [pan2f_gesture_recognizer getScaledTranslation:window];
      CGFloat translationMagnitude = sqrt(translation.x * translation.x +
                                          translation.y * translation.y);

      if (translationMagnitude > 10.0) {
        return NO;
      }
    }

    if (rotation_gesture_recognizer.state == UIGestureRecognizerStateBegan ||
        rotation_gesture_recognizer.state == UIGestureRecognizerStateChanged)
    {
      CGFloat rotation = fabs([rotation_gesture_recognizer getScaledRotation]);
      if (rotation > 0.05) {
        return NO;
      }
    }
  }

  return YES;
}

/* Override touch methods to capture the UITouch object. */
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
  [super touchesBegan:touches withEvent:event];

  for (UITouch *touch in touches) {
    if (touch.type == UITouchTypePencil) {
      current_pencil_touch = touch;
      break;
    }
  }
}

/* Get updated tablet data. */
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
  [super touchesMoved:touches withEvent:event];

  /* Check if one of the touches was from a pencil. */
  if (current_pencil_touch) {
    /* Iterate through all pencil touches. */
    for (UITouch *touch in touches) {
      if (touch.type == UITouchTypePencil) {
        current_pencil_touch = touch;

        tablet_data.Active = GHOST_kTabletModeStylus;

        /* Map apple pessure Range to Blender range: 0.0 (not touching) to 1.0 (full pressure). */
        tablet_data.Pressure = current_pencil_touch.force /
                               current_pencil_touch.maximumPossibleForce;

        CGFloat azimuthAngle = [current_pencil_touch azimuthAngleInView:window->getView()];
        CGFloat altitudeAngle = [current_pencil_touch altitudeAngle];

        /* Calculate the maximum possible tilt (1.0) when altitude is 0. */
        CGFloat maxTilt = cos(0);

        /* Convert to x and y tilt - range -1.0 (left) to +1.0 (right). */
        tablet_data.Xtilt = sin(azimuthAngle) * cos(altitudeAngle) / maxTilt;
        tablet_data.Ytilt = -cos(azimuthAngle) * cos(altitudeAngle) / maxTilt;
        IOS_INPUT_LOG(
            @"TABLET: X:%f,Y:%f,P:%f", tablet_data.Xtilt, tablet_data.Ytilt, tablet_data.Pressure);
        break;
      }
    }
  }
}

/* Reset tablet data. */
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
  [super touchesEnded:touches withEvent:event];
  current_pencil_touch = nil;
  tablet_data = GHOST_TABLET_DATA_NONE;
}

/* Reset tablet data. */
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
  [super touchesCancelled:touches withEvent:event];
  current_pencil_touch = nil;
  tablet_data = GHOST_TABLET_DATA_NONE;
}

- (void)sendEvent:(UIEvent *)event
{
  if (event.type == UIEventTypeTouches) {
    [self handleDirectTouchEvent:event];
  }

  if (event.type == UIEventTypePresses) {
    [self handleDirectKeyboardEvent:event];
  }

  /* Always forward the event to UIKit so gestures don't break. */
  [super sendEvent:event];
}

- (GHOST_TButton)getMouseButtonFromTouch:(UITouch *)touch withEvent:(UIEvent *)event
{
  if (@available(iOS 13.4, *)) {
    UIEventButtonMask buttonMask = event.buttonMask;

    static const UIEventButtonMask UIEventButtonMaskMiddle = (1 << 2);
    static const UIEventButtonMask UIEventButtonMaskButton4 = (1 << 3);
    static const UIEventButtonMask UIEventButtonMaskButton5 = (1 << 4);
    static const UIEventButtonMask UIEventButtonMaskButton6 = (1 << 5);
    static const UIEventButtonMask UIEventButtonMaskButton7 = (1 << 6);

    if (buttonMask & UIEventButtonMaskMiddle) {
      return GHOST_kButtonMaskMiddle;
    }
    if (buttonMask & UIEventButtonMaskSecondary) {
      return GHOST_kButtonMaskRight;
    }
    if (buttonMask & UIEventButtonMaskPrimary) {
      return GHOST_kButtonMaskLeft;
    }
    if (buttonMask & UIEventButtonMaskButton4) {
      return GHOST_kButtonMaskButton4;
    }
    if (buttonMask & UIEventButtonMaskButton5) {
      return GHOST_kButtonMaskButton5;
    }
    if (buttonMask & UIEventButtonMaskButton6) {
      return GHOST_kButtonMaskButton6;
    }
    if (buttonMask & UIEventButtonMaskButton7) {
      return GHOST_kButtonMaskButton7;
    }
  }

  return GHOST_kButtonMaskLeft;
}

- (void)handleDirectTouchEvent:(UIEvent *)event
{
  for (UITouch *touch in event.allTouches) {
    CGPoint location = [touch locationInView:window->getView()];
    CGPoint scaledLocation = window->scalePointToWindow(location);

    switch (touch.phase) {
      case UITouchPhaseBegan: {
        /* Always send cursor move first for mouse. */
        system->pushEvent(new GHOST_EventCursor(GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
                                                GHOST_kEventCursorMove,
                                                window,
                                                scaledLocation.x,
                                                scaledLocation.y,
                                                GHOST_TABLET_DATA_NONE));

        if (touch.type == UITouchTypeIndirectPointer) {
          GHOST_TButton buttonMask = [self getMouseButtonFromTouch:touch withEvent:event];
          touch_button_map[(uint64_t)touch] = buttonMask;
          system->pushEvent(
              new GHOST_EventButton(GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
                                    GHOST_kEventButtonDown,
                                    window,
                                    buttonMask,
                                    GHOST_TABLET_DATA_NONE));
        }
        break;
      }

      case UITouchPhaseMoved:
        /* Send cursor move for all touch types. */
        system->pushEvent(new GHOST_EventCursor(GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
                                                GHOST_kEventCursorMove,
                                                window,
                                                scaledLocation.x,
                                                scaledLocation.y,
                                                GHOST_TABLET_DATA_NONE));
        break;

      case UITouchPhaseEnded:
      case UITouchPhaseCancelled: {
        if (touch.type == UITouchTypeIndirectPointer) {
          uint64_t touchKey = (uint64_t)touch;
          GHOST_TButton buttonMask = GHOST_kButtonMaskLeft;
          auto it = touch_button_map.find(touchKey);
          if (it != touch_button_map.end()) {
            buttonMask = it->second;
            touch_button_map.erase(it);
          }
          system->pushEvent(
              new GHOST_EventButton(GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
                                    GHOST_kEventButtonUp,
                                    window,
                                    buttonMask,
                                    GHOST_TABLET_DATA_NONE));
        }
        break;
      }

      default:
        break;
    }
  }
}

/* Direct keyboard handling methods */
- (void)handleDirectKeyboardEvent:(UIEvent *)event
{
  if (event.type == UIEventTypePresses) {
    UIPressesEvent *pressEvent = (UIPressesEvent *)event;

    for (UIPress *press in pressEvent.allPresses) {
      [self handleKeyPress:press withEvent:pressEvent];
    }
  }
}

- (void)handleKeyPress:(UIPress *)press withEvent:(UIPressesEvent *)event
{
  GHOST_TEventType eventType;
  switch (press.phase) {
    case UIPressPhaseStationary:
    case UIPressPhaseBegan:
      eventType = GHOST_kEventKeyDown;
      break;
    case UIPressPhaseEnded:
    case UIPressPhaseCancelled:
      eventType = GHOST_kEventKeyUp;
      break;
    default:
      return;
  }

  /* Get current modifier flags from the press event */
  UIKeyModifierFlags modifiers = press.key.modifierFlags;

  NSString *keyString = press.key.charactersIgnoringModifiers;

  /* Handle modifier keys when no character is produced */
  if (!keyString || keyString.length == 0) {
    /* Map iOS key codes to GHOST keys for modifier detection */
    GHOST_TKey ghostModifierKey = GHOST_kKeyUnknown;

    /* Common iOS modifier key codes */
    switch (press.key.keyCode) {
      case 225: /* Left Shift */
        ghostModifierKey = GHOST_kKeyLeftShift;
        break;
      case 229: /* Right Shift */
        ghostModifierKey = GHOST_kKeyRightShift;
        break;
      case 224: /* Left Control */
        ghostModifierKey = GHOST_kKeyLeftControl;
        break;
      case 228: /* Right Control */
        ghostModifierKey = GHOST_kKeyLeftControl;
        break;
      case 226: /* Left Alt/Option */
        ghostModifierKey = GHOST_kKeyRightAlt;
        break;
      case 230: /* Right Alt/Option */
        ghostModifierKey = GHOST_kKeyLeftAlt;
        break;
      case 227: /* Left Command */
        ghostModifierKey = GHOST_kKeyRightOS;
        break;
      case 231: /* Right Command */
        ghostModifierKey = GHOST_kKeyLeftOS;
        break;
      default:
        IOS_INPUT_LOG(@"KEYBOARD: Unknown modifier key code: %ld", (long)press.key.keyCode);
        return;
    }

    /* Send the specific modifier key event */
    GHOST_EventKey *keyEvent = new GHOST_EventKey(
        GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
        eventType,
        window,
        ghostModifierKey,
        false,
        nullptr);
    system->pushEvent(keyEvent);
    return;
  }

  /* Handle regular keys (with or without modifiers) */
  GHOST_TKey ghostKey = [self convertIOSKeyToGHOST:keyString];
  /* Send modifier events first for combinations like Cmd+C */
  if (eventType == GHOST_kEventKeyDown) {
    if (modifiers & UIKeyModifierCommand) {
      GHOST_EventKey *modEvent = new GHOST_EventKey(
          GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
          GHOST_kEventKeyDown,
          window,
          GHOST_kKeyLeftOS,
          false,
          nullptr);
      system->pushEvent(modEvent);
    }

    if (modifiers & UIKeyModifierControl) {
      GHOST_EventKey *modEvent = new GHOST_EventKey(
          GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
          GHOST_kEventKeyDown,
          window,
          GHOST_kKeyLeftControl,
          false,
          nullptr);
      system->pushEvent(modEvent);
    }

    if (modifiers & UIKeyModifierAlternate) {
      GHOST_EventKey *modEvent = new GHOST_EventKey(
          GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
          GHOST_kEventKeyDown,
          window,
          GHOST_kKeyLeftAlt,
          false,
          nullptr);
      system->pushEvent(modEvent);
    }

    if (modifiers & UIKeyModifierShift) {
      GHOST_EventKey *modEvent = new GHOST_EventKey(
          GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
          GHOST_kEventKeyDown,
          window,
          GHOST_kKeyLeftShift,
          false,
          nullptr);
      system->pushEvent(modEvent);
    }
  }

  /* Create GHOST event with UTF8 string for character input */
  NSString *utf8String = nil;
  if (eventType == GHOST_kEventKeyDown && press.key.characters.length > 0) {
    utf8String = press.key.characters;
  }
  const char *utf8CString = utf8String ? [utf8String UTF8String] : nullptr;

  GHOST_EventKey *keyEvent = new GHOST_EventKey(GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
                                                eventType,
                                                window,
                                                ghostKey,
                                                false, /* No repeat detection for now */
                                                utf8CString);

  system->pushEvent(keyEvent);

  /* Send modifier key up events after the main key for combinations */
  if (eventType == GHOST_kEventKeyUp) {
    if (modifiers & UIKeyModifierShift) {
      GHOST_EventKey *modEvent = new GHOST_EventKey(
          GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
          GHOST_kEventKeyUp,
          window,
          GHOST_kKeyLeftShift,
          false,

          nullptr);
      system->pushEvent(modEvent);
    }

    if (modifiers & UIKeyModifierAlternate) {
      GHOST_EventKey *modEvent = new GHOST_EventKey(
          GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
          GHOST_kEventKeyUp,
          window,
          GHOST_kKeyLeftAlt,
          false,
          nullptr);
      system->pushEvent(modEvent);
    }

    if (modifiers & UIKeyModifierControl) {
      GHOST_EventKey *modEvent = new GHOST_EventKey(
          GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
          GHOST_kEventKeyUp,
          window,
          GHOST_kKeyLeftControl,
          false,
          nullptr);
      system->pushEvent(modEvent);
    }

    if (modifiers & UIKeyModifierCommand) {
      GHOST_EventKey *modEvent = new GHOST_EventKey(
          GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
          GHOST_kEventKeyUp,
          window,
          GHOST_kKeyLeftOS,
          false,
          nullptr);
      system->pushEvent(modEvent);
    }
  }
}

- (GHOST_TKey)convertIOSKeyToGHOST:(NSString *)key
{
  /* Handle special keys using string comparison (iOS 7.0+) */
  if (@available(iOS 7.0, *)) {
    if ([key isEqualToString:UIKeyInputEscape]) {
      return GHOST_kKeyEsc;
    }
    if ([key isEqualToString:UIKeyInputUpArrow]) {
      return GHOST_kKeyUpArrow;
    }
    if ([key isEqualToString:UIKeyInputDownArrow]) {
      return GHOST_kKeyDownArrow;
    }
    if ([key isEqualToString:UIKeyInputLeftArrow]) {
      return GHOST_kKeyLeftArrow;
    }
    if ([key isEqualToString:UIKeyInputRightArrow]) {
      return GHOST_kKeyRightArrow;
    }
    if ([key isEqualToString:UIKeyInputDelete]) {
      return GHOST_kKeyBackSpace;
    }
  }

  /* Handle additional special keys by string name */
  if ([key isEqualToString:UIKeyInputF1])
    return GHOST_kKeyF1;
  if ([key isEqualToString:UIKeyInputF2])
    return GHOST_kKeyF2;
  if ([key isEqualToString:UIKeyInputF3])
    return GHOST_kKeyF3;
  if ([key isEqualToString:UIKeyInputF4])
    return GHOST_kKeyF4;
  if ([key isEqualToString:UIKeyInputF5])
    return GHOST_kKeyF5;
  if ([key isEqualToString:UIKeyInputF6])
    return GHOST_kKeyF6;
  if ([key isEqualToString:UIKeyInputF7])
    return GHOST_kKeyF7;
  if ([key isEqualToString:UIKeyInputF8])
    return GHOST_kKeyF8;
  if ([key isEqualToString:UIKeyInputF9])
    return GHOST_kKeyF9;
  if ([key isEqualToString:UIKeyInputF10])
    return GHOST_kKeyF10;
  if ([key isEqualToString:UIKeyInputF11])
    return GHOST_kKeyF11;
  if ([key isEqualToString:UIKeyInputF12])
    return GHOST_kKeyF12;

  /* Additional navigation and editing keys */
  if ([key isEqualToString:@"Home"])
    return GHOST_kKeyHome;
  if ([key isEqualToString:@"End"])
    return GHOST_kKeyEnd;
  if ([key isEqualToString:@"Page Up"])
    return GHOST_kKeyUpPage;
  if ([key isEqualToString:@"Page Down"])
    return GHOST_kKeyDownPage;
  if ([key isEqualToString:@"Insert"])
    return GHOST_kKeyInsert;
  if ([key isEqualToString:@"Delete"])
    return GHOST_kKeyDelete;

  /* Keypad/Numeric keys */
  if ([key isEqualToString:@"Keypad 0"])
    return GHOST_kKeyNumpad0;
  if ([key isEqualToString:@"Keypad 1"])
    return GHOST_kKeyNumpad1;
  if ([key isEqualToString:@"Keypad 2"])
    return GHOST_kKeyNumpad2;
  if ([key isEqualToString:@"Keypad 3"])
    return GHOST_kKeyNumpad3;
  if ([key isEqualToString:@"Keypad 4"])
    return GHOST_kKeyNumpad4;
  if ([key isEqualToString:@"Keypad 5"])
    return GHOST_kKeyNumpad5;
  if ([key isEqualToString:@"Keypad 6"])
    return GHOST_kKeyNumpad6;
  if ([key isEqualToString:@"Keypad 7"])
    return GHOST_kKeyNumpad7;
  if ([key isEqualToString:@"Keypad 8"])
    return GHOST_kKeyNumpad8;
  if ([key isEqualToString:@"Keypad 9"])
    return GHOST_kKeyNumpad9;
  if ([key isEqualToString:@"Keypad ."])
    return GHOST_kKeyNumpadPeriod;
  if ([key isEqualToString:@"Keypad +"])
    return GHOST_kKeyNumpadPlus;
  if ([key isEqualToString:@"Keypad -"])
    return GHOST_kKeyNumpadMinus;
  if ([key isEqualToString:@"Keypad *"])
    return GHOST_kKeyNumpadAsterisk;
  if ([key isEqualToString:@"Keypad /"])
    return GHOST_kKeyNumpadSlash;
  if ([key isEqualToString:@"Keypad Enter"])
    return GHOST_kKeyNumpadEnter;

  /* For regular character keys, get the first character */
  unichar character = [key characterAtIndex:0];

  /* Handle common control characters */
  switch (character) {
    case '\r':
    case '\n':
      return GHOST_kKeyEnter;
    case '\t':
      return GHOST_kKeyTab;
    case ' ':
      return GHOST_kKeySpace;
    case 0x1B:
      return GHOST_kKeyEsc; /* ESC character */
    default:
      break;
  }

  /* Handle alphanumeric keys - convert to uppercase for consistency */
  if (character >= 'a' && character <= 'z') {
    return (GHOST_TKey)(GHOST_kKeyA + (character - 'a'));
  }
  if (character >= 'A' && character <= 'Z') {
    return (GHOST_TKey)(GHOST_kKeyA + (character - 'A'));
  }
  if (character >= '0' && character <= '9') {
    return (GHOST_TKey)(GHOST_kKey0 + (character - '0'));
  }

  /* Handle other special characters */
  switch (character) {
    case '-':
      return GHOST_kKeyMinus;
    case '=':
      return GHOST_kKeyEqual;
    case '[':
      return GHOST_kKeyLeftBracket;
    case ']':
      return GHOST_kKeyRightBracket;
    case '\\':
      return GHOST_kKeyBackslash;
    case ';':
      return GHOST_kKeySemicolon;
    case '\'':
      return GHOST_kKeyQuote;
    case '`':
      return GHOST_kKeyAccentGrave;
    case ',':
      return GHOST_kKeyComma;
    case '.':
      return GHOST_kKeyPeriod;
    case '/':
      return GHOST_kKeySlash;

    /* Shifted special characters */
    case '_':
      return GHOST_kKeyMinus; /* Shifted minus */
    case '+':
      return GHOST_kKeyEqual; /* Shifted equal */
    case '{':
      return GHOST_kKeyLeftBracket; /* Shifted [ */
    case '}':
      return GHOST_kKeyRightBracket; /* Shifted ] */
    case '|':
      return GHOST_kKeyBackslash; /* Shifted \ */
    case ':':
      return GHOST_kKeySemicolon; /* Shifted ; */
    case '"':
      return GHOST_kKeyQuote; /* Shifted ' */
    case '~':
      return GHOST_kKeyAccentGrave; /* Shifted ` */
    case '<':
      return GHOST_kKeyComma; /* Shifted , */
    case '>':
      return GHOST_kKeyPeriod; /* Shifted . */
    case '?':
      return GHOST_kKeySlash; /* Shifted / */
    /* Shifted number row */
    case '!':
      return GHOST_kKey1;
    case '@':
      return GHOST_kKey2;
    case '#':
      return GHOST_kKey3;
    case '$':
      return GHOST_kKey4;
    case '%':
      return GHOST_kKey5;
    case '^':
      return GHOST_kKey6;
    case '&':
      return GHOST_kKey7;
    case '*':
      return GHOST_kKey8;
    case '(':
      return GHOST_kKey9;
    case ')':
      return GHOST_kKey0;

    /* Additional control characters */
    case 0x08:
      return GHOST_kKeyBackSpace; /* Backspace */
    case 0x7F:
      return GHOST_kKeyDelete; /* Delete */

    default:
      return GHOST_kKeyUnknown;
  }
}

- (void)handleTap:(GHOSTUITapGestureRecognizer *)sender
{
  if (sender.state == UIGestureRecognizerStateEnded) {
    CGPoint touch_point = [sender getScaledTouchPoint:window];

    bool is_pencil = current_pencil_touch ? true : false;

    UserInputEvent event_info(&touch_point, nullptr, nullptr, is_pencil);
    event_info.add_event(UserInputEvent::EventTypes::CURSOR_MOVE);
    event_info.add_event(UserInputEvent::EventTypes::LEFT_BUTTON_DOWN);
    event_info.add_event(UserInputEvent::EventTypes::LEFT_BUTTON_UP);

    [self generateUserInputEvents:event_info];
  }
}

- (void)handleTap2F:(GHOSTUITapGestureRecognizer *)sender
{
  if (sender.state != UIGestureRecognizerStateEnded) {
    return;
  }

  system->pushEvent(new GHOST_Event(
      GHOST_GetMilliSeconds((GHOST_SystemHandle)system), GHOST_kEventTwoFingerTap, window));
}

- (void)handleTap3F:(GHOSTUITapGestureRecognizer *)sender
{
  if (sender.state != UIGestureRecognizerStateEnded) {
    return;
  }

  system->pushEvent(new GHOST_Event(
      GHOST_GetMilliSeconds((GHOST_SystemHandle)system), GHOST_kEventThreeFingerTap, window));
}

- (void)handleTap4F:(GHOSTUITapGestureRecognizer *)sender
{
  if (sender.state != UIGestureRecognizerStateEnded) {
    return;
  }

  system->pushEvent(new GHOST_Event(
      GHOST_GetMilliSeconds((GHOST_SystemHandle)system), GHOST_kEventFourFingerTap, window));
}

- (void)handlePan:(GHOSTUIPanGestureRecognizer *)sender
{
  bool is_pencil = current_pencil_touch ? true : false;

  if (is_pencil) {
    /* Pencil drag = Box selection - redirect to dedicated pencil handler */
    [self handlePencilPan:sender];
  }
  else {
    /* Finger drag = Orbit (maps to legacy 2-finger drag) */
    [self handleFingerPan:sender];
  }
}

- (void)handleFingerPan:(GHOSTUIPanGestureRecognizer *)sender
{
  CGPoint touch_point = [sender getScaledTouchPoint:window];
  CGPoint translation = [sender getScaledTranslation:window];
  bool is_pencil = false;  // This is definitely finger input

  if (sender.state == UIGestureRecognizerStateBegan ||
      sender.state == UIGestureRecognizerStateChanged)
  {
    /* Calculate translation relative to previous cached value. */
    CGPoint relative_translation = [sender getRelativeTranslation:translation];

    /* Cache new translation. */
    [sender setCachedTranslation:translation];

    /* Generate orbit event if translation is non zero. */
    if (!CGPointEqualToPoint(relative_translation, CGPointMake(0.0f, 0.0f))) {
      UserInputEvent event_info(&touch_point, &relative_translation, nullptr, is_pencil);
      event_info.add_event(UserInputEvent::EventTypes::ORBIT_GESTURE);
      [self generateUserInputEvents:event_info];
    }
  }
  else if (sender.state == UIGestureRecognizerStateEnded ||
           sender.state == UIGestureRecognizerStateCancelled ||
           sender.state == UIGestureRecognizerStateFailed)
  {
    /* Set translation back to zero. */
    [sender setCachedTranslation:CGPointMake(0.0f, 0.0f)];
  }
}

- (void)handlePencilPan:(GHOSTUIPanGestureRecognizer *)sender
{
  CGPoint touch_point = [sender getScaledTouchPoint:window];
  CGPoint translation = [sender getScaledTranslation:window];
  bool is_pencil = true;  // This is definitely pencil input

  UserInputEvent event_info(&touch_point, nullptr, nullptr, is_pencil);

  if (sender.state == UIGestureRecognizerStateBegan ||
      sender.state == UIGestureRecognizerStateChanged)
  {
    /* Register initial click for click and drag support. */
    if (sender.state == UIGestureRecognizerStateBegan) {
      /* Set initial translation */
      [sender setCachedTranslation:translation];
      event_info.add_event(UserInputEvent::EventTypes::CURSOR_MOVE);
      event_info.add_event(UserInputEvent::EventTypes::LEFT_BUTTON_DOWN);
    }

    /* Calculate translation change since last begin/change event */
    CGPoint relative_translation = [sender getRelativeTranslation:translation];
    /* Update cached translation */
    [sender setCachedTranslation:translation];
    /* Send pan event if non zero */
    if (!CGPointEqualToPoint(relative_translation, CGPointMake(0.0f, 0.0f))) {
      event_info.translation = relative_translation;
      event_info.add_event(UserInputEvent::EventTypes::PAN_GESTURE);
    }

    /* Update cursor position on change */
    if (sender.state == UIGestureRecognizerStateChanged) {
      event_info.add_event(UserInputEvent::EventTypes::CURSOR_MOVE);
    }
  }

  /* Mouse release for pan. */
  if (sender.state == UIGestureRecognizerStateEnded ||
      sender.state == UIGestureRecognizerStateCancelled ||
      sender.state == UIGestureRecognizerStateFailed)
  {
    event_info.add_event(UserInputEvent::EventTypes::LEFT_BUTTON_UP);
  }
  [self generateUserInputEvents:event_info];
}

- (void)handlePan2f:(GHOSTUIPanGestureRecognizer *)sender
{
  /* Translation can be non-zero on begin event */
  if (sender.state == UIGestureRecognizerStateBegan ||
      sender.state == UIGestureRecognizerStateChanged)
  {
    CGPoint translation = [sender getScaledTranslation:window];

    /* Calculate translation relative to previous cached value. */
    CGPoint relative_translation = [sender getRelativeTranslation:translation];

    /* Cache new translation. */
    [sender setCachedTranslation:translation];

    /* Generate shift+orbit events if translation is non zero. */
    if (!CGPointEqualToPoint(relative_translation, CGPointMake(0.0f, 0.0f))) {
      CGPoint touch_point = [sender getScaledTouchPoint:window];
      bool pencil_pan = current_pencil_touch ? true : false;
      UserInputEvent event_info(&touch_point, &relative_translation, nullptr, pencil_pan);

      if (sender.state == UIGestureRecognizerStateBegan) {
        event_info.add_event(UserInputEvent::EventTypes::SHIFT_KEY_DOWN);
      }

      event_info.add_event(UserInputEvent::EventTypes::ORBIT_GESTURE);
      [self generateUserInputEvents:event_info];
    }
  }
  else if (sender.state == UIGestureRecognizerStateEnded ||
           sender.state == UIGestureRecognizerStateCancelled ||
           sender.state == UIGestureRecognizerStateFailed)
  {
    /* Set translation back to zero. */
    [sender setCachedTranslation:CGPointMake(0.0f, 0.0f)];

    /* Send shift key up when gesture ends */
    CGPoint touch_point = [sender getScaledTouchPoint:window];
    bool pencil_pan = current_pencil_touch ? true : false;
    UserInputEvent event_info(&touch_point, nullptr, nullptr, pencil_pan);
    event_info.add_event(UserInputEvent::EventTypes::SHIFT_KEY_UP);
    [self generateUserInputEvents:event_info];
  }
}

- (void)handleRotation:(GHOSTUIRotationGestureRecognizer *)sender
{
  /* Handle gesture ending states first - don't access touches if they may not exist */
  if (sender.state == UIGestureRecognizerStateEnded ||
      sender.state == UIGestureRecognizerStateCancelled ||
      sender.state == UIGestureRecognizerStateFailed || [sender numberOfTouches] < 2)
  {

    /* Use safe fallback center point - DO NOT access touch data when ending */
    CGPoint centerPoint = CGPointMake(0.0f, 0.0f);

    UserInputEvent event_info(&centerPoint, nullptr, nullptr, false);
    event_info.add_event(UserInputEvent::EventTypes::MIDDLE_BUTTON_UP);
    event_info.add_event(UserInputEvent::EventTypes::ALT_KEY_UP);
    [self generateUserInputEvents:event_info];
    return;
  }

  /* Now safe to access touch data since we know we have at least 2 touches */
  CGPoint centerPoint = [sender getRotationCenter:window];

  if (sender.state == UIGestureRecognizerStateBegan) {
    /* Set initial rotation value */
    CGFloat rotation_angle = [sender getScaledRotation];
    [sender setCachedRotation:rotation_angle];

    /* Send Alt down + Middle button down to start rotation */
    UserInputEvent event_info(&centerPoint, nullptr, nullptr, false);
    event_info.add_event(UserInputEvent::EventTypes::ALT_KEY_DOWN);
    event_info.add_event(UserInputEvent::EventTypes::MIDDLE_BUTTON_DOWN);
    [self generateUserInputEvents:event_info];
  }
  else if (sender.state == UIGestureRecognizerStateChanged) {
    /* Calculate change in rotation since last event */
    CGFloat rotation_angle = [sender getScaledRotation];
    CGFloat relative_rotation = [sender getRelativeRotation:rotation_angle];

    /* Update cached rotation */
    [sender setCachedRotation:rotation_angle];

    /* Send cursor movement if change is significant */
    if (fabs(relative_rotation) > 0.01) { /* 0.01 radian threshold (~0.6 degrees) */
      /* Convert rotation to cursor movement (horizontal drag for rotation) */
      float movement_pixels = -relative_rotation * 100.0f;
      CGPoint movement = CGPointMake(movement_pixels, 0);

      UserInputEvent event_info(&centerPoint, &movement, nullptr, false);
      event_info.add_event(UserInputEvent::EventTypes::CURSOR_MOVE);
      [self generateUserInputEvents:event_info];
    }
  }
}

- (void)handleEdgeSwipe:(UIScreenEdgePanGestureRecognizer *)gesture
{
  if (gesture.state != UIGestureRecognizerStateEnded) {
    return;
  }

  UIView *view = window->getView();
  CGPoint location = [gesture locationInView:view];
  CGSize viewSize = view.bounds.size;

  GHOST_TTouchEventSubTypes ghostEventType;

  if (gesture.edges == UIRectEdgeLeft) {
    ghostEventType = GHOST_kTouchEventEdgeSwipeInLeft;
  }
  else if (gesture.edges == UIRectEdgeRight) {
    ghostEventType = GHOST_kTouchEventEdgeSwipeInRight;
  }
  else {
    /* For now only handle left/right. */
    return;
  }

  system->pushEvent(new GHOST_EventTouch(
      system->getMilliSeconds(), window, ghostEventType, location.x, location.y));
}

- (void)handleHover:(GHOSTUIHoverGestureRecognizer *)sender
{
  CGPoint hover_point = [sender getScaledTouchPoint:window];

  /* Determine if this is Pencil hover or a normal pointer device. */
  BOOL isPencilHover = NO;
  if (sender.allowedTouchTypes.count > 0) {
    NSNumber *touchType = sender.allowedTouchTypes.firstObject;
    isPencilHover = (touchType.integerValue == UITouchTypePencil);
  }

  /* Set tablet data for Pencil hover, mouse mode otherwise. */
  if (isPencilHover) {
    tablet_data.Active = GHOST_kTabletModeStylus;
  }
  else {
    tablet_data = GHOST_TABLET_DATA_NONE;
  }

  /* Always send cursor move (desktop-style hover). */
  system->pushEvent(new GHOST_EventCursor(GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
                                          GHOST_kEventCursorMove,
                                          window,
                                          hover_point.x,
                                          hover_point.y,
                                          tablet_data));

  /* Reset tablet data when hover ends. */
  if (sender.state == UIGestureRecognizerStateEnded ||
      sender.state == UIGestureRecognizerStateCancelled ||
      sender.state == UIGestureRecognizerStateFailed)
  {
    tablet_data = GHOST_TABLET_DATA_NONE;
  }
}

- (void)handleZoom:(GHOSTUIPinchGestureRecognizer *)sender
{
  /* Ignore any calls where don't have two touches to work with. */
  if ([sender numberOfTouches] < 2) {
    return;
  }

  /* Pinch/Zoom gestures */
  if (sender.state == UIGestureRecognizerStateBegan) {
    /* Set an initial distance value. */
    CGFloat point_distance = [sender getScaledDistance:window];
    [sender setCachedDistance:point_distance];
  }
  else if (sender.state == UIGestureRecognizerStateChanged) {

    /* Calculate change in distance since last event */
    CGFloat point_distance = [sender getScaledDistance:window];
    CGFloat relative_dist = point_distance - [sender getCachedDistance];

    /* Updated cached distance. */
    [sender setCachedDistance:point_distance];

    /* Send pinch/zoom event. */
    if (fabs(relative_dist) > 0.0) {
      /* Calculate midpoint between the two touch points. */
      CGPoint midPoint = [sender getPinchMidpoint:window];

      UserInputEvent event_info(&midPoint, nullptr, &relative_dist, false);
      event_info.add_event(UserInputEvent::EventTypes::PINCH_GESTURE);
      [self generateUserInputEvents:event_info];
    }
  }
  else if (sender.state == UIGestureRecognizerStateEnded ||
           sender.state == UIGestureRecognizerStateCancelled ||
           sender.state == UIGestureRecognizerStateFailed)
  {
  }
}

- (void)pencilInteractionDidTap:(UIPencilInteraction *)interaction
{
  UserInputEvent event_info(nullptr, nullptr, nullptr, true);
  event_info.add_event(UserInputEvent::EventTypes::PENCIL_TAP);
  [self generateUserInputEvents:event_info];
}

- (void)beginFrame
{
}

- (void)endFrame
{
}

- (void)initToolbar
{
  /* This gets the current view size */
  UIView *ui_view = window->getView();
  CGSize frame_size = [ui_view sizeThatFits:CGSizeMake(0.0f, 0.0f)];
  /* Create a toolbar the width of the screen. */
  toolbar = [[UIToolbar alloc] initWithFrame:CGRectMake(0, 0, frame_size.width, 44)];
  toolbar.barStyle = UIBarStyleDefault;
  toolbar.translucent = true;
  /* IOS_FIXME - Despite following Apple guidelines this toolbar still
   * Appears to apparently violate the view constraints. It displays fine
   * but generates a lot of warning output to the console. */
  toolbar.autoresizingMask = UIViewAutoresizingFlexibleWidth;
  toolbar.translatesAutoresizingMaskIntoConstraints = NO;
  [toolbar sizeToFit];

  toolbar_tip_item = [[UIBarButtonItem alloc] initWithTitle:@""
                                                      style:UIBarButtonItemStylePlain
                                                     target:nil
                                                     action:nil];

  toolbar_live_text_item = [[UIBarButtonItem alloc] initWithTitle:@""
                                                            style:UIBarButtonItemStylePlain
                                                           target:nil
                                                           action:nil];

  toolbar_done_editing_item = [[UIBarButtonItem alloc]
      initWithBarButtonSystemItem:UIBarButtonSystemItemDone
                           target:nil
                           action:@selector(handleDoneButton)];

  toolbar_cancel_editing_item = [[UIBarButtonItem alloc]
      initWithBarButtonSystemItem:UIBarButtonSystemItemCancel
                           target:nil
                           action:@selector(handleCancelButton)];

  /* Prevents editing of tip and live text fields. */
  toolbar_tip_item.enabled = NO;
  toolbar_live_text_item.enabled = NO;
  toolbar_live_text_item.tintColor = UIColor.blackColor;

  /* Set the live text to a fixed width. */
  /* IOS_FIXME - should this be set dynamically? Need to move out of init if so. */
  toolbar_live_text_item.width = 150.0f;

  toolbar.items = @[
    toolbar_tip_item,
    toolbar_live_text_item,
    toolbar_done_editing_item,
    toolbar_cancel_editing_item
  ];
}

- (void)generateKeyboardReturnEvent
{
  /*
   Only push the event back if the keyboard is active otherwise we may generate new
   spurious events.
   */
  if (onscreen_keyboard_active) {
    /*
     This event should cause ui_textedit_end() to be called which will
     hide the keyboard.
     */
    system->pushEvent(new GHOST_EventKey(GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
                                         GHOST_kEventKeyDown,
                                         window,
                                         GHOST_kKeyEnter,
                                         false,
                                         nullptr));
  }
  else {
    IOS_INPUT_LOG(@"Ignoring handleKeyboardReturn %@", text_field.text);
  }
}

- (void)handleKeyboardReturn:(UITextField *)text_field
{
  @synchronized(self) {
    IOS_INPUT_LOG(@"handleKeyboardReturn %@", text_field.text);
    [self generateKeyboardReturnEvent];
  }
}

- (void)handleKeyboardEditChange:(UITextField *)text_field
{
  @synchronized(self) {

    /* Update the text in the tool bar as the edits arrive. */
    if (toolbar_live_text_item) {
      toolbar_live_text_item.title = text_field.text;
      /* Force toolbar to update */
      [toolbar setNeedsLayout];
      [toolbar layoutIfNeeded];
    }
    IOS_INPUT_LOG(@"Keyboard Edit change detected %@", text_field.text);

    /* IOS_FIXME - Enabling this will propogate text changes back into the Blender text field
     as they happen. Since pushing back individual key presses appears to be difficult this
     might be the best we can do. However this currently causes a segmentation fault if you delete
     text as the Blender-side string ends up being NULL in some cases. */
    bool push_edits_back_to_blender = false;

    if (push_edits_back_to_blender) {
      system->pushEvent(new GHOST_EventKey(GHOST_GetMilliSeconds((GHOST_SystemHandle)system),
                                           GHOST_kEventKeyDown,
                                           window,
                                           GHOST_kKeyTextEdit,
                                           false,
                                           nullptr));
    }
  }
}

- (void)handleKeyboardEditBegin:(UITextField *)text_field
{
  @synchronized(self) {
    IOS_INPUT_LOG(@"Keyboard Edit begin detected %@", text_field.text);
  }
}

- (void)handleKeyboardEditEnd:(UITextField *)text_field
{
  @synchronized(self) {
    /*
     This can get called when the keyboard is minimised
     so send a return keypress to emulate effective end
     of editing. Otherwise Blender's focus will remain
     on the text field.
     */
    IOS_INPUT_LOG(@"Keyboard Edit end detected %@", text_field.text);
    [self generateKeyboardReturnEvent];
  }
}

- (void)handleDoneButton
{
  IOS_INPUT_LOG(@"Keyboard Done button press detected %@", text_field.text);
  [self generateKeyboardReturnEvent];
}

- (void)handleCancelButton
{
  IOS_INPUT_LOG(@"Keyboard Cancel button press detected %@", text_field.text);
  /* Restore the original text and return */
  text_field.text = original_text;
  [self generateKeyboardReturnEvent];
}

/*
 * Add a text field so we can handle input from a popup keyboard and
 * attach it to our root window.
 */
- (void)initUITextField
{
  /* Initialise it if we have not already done so. */
  if (!text_field) {
    text_field = [[UITextField alloc] init];

    text_field.contentScaleFactor = window->getWindowScaleFactor();

    if (toolbar_enabled) {
      [self initToolbar];
      text_field.inputAccessoryView = toolbar;
    }

    [window->rootWindow addSubview:text_field];

    /* Add a handler for when 'return' is pressed on keyboard. */
    [text_field addTarget:self
                   action:@selector(handleKeyboardReturn:)
         forControlEvents:UIControlEventEditingDidEndOnExit];

    /* Add a handler for when the text field changes. */
    [text_field addTarget:self
                   action:@selector(handleKeyboardEditChange:)
         forControlEvents:UIControlEventEditingChanged];

    /* Add a handler for when user edits a text field. */
    [text_field addTarget:self
                   action:@selector(handleKeyboardEditBegin:)
         forControlEvents:UIControlEventEditingDidBegin];

    /* Add a handler for when user finishes editing a text field. */
    [text_field addTarget:self
                   action:@selector(handleKeyboardEditEnd:)
         forControlEvents:UIControlEventEditingDidEnd];
  }
}

- (void)convertWindowCoordToDisplayCoordWithWindow:(int)windowX
                                           windowY:(int)windowY
                                          displayX:(double *)displayX
                                          displayY:(double *)displayY
                                             flipY:(BOOL)flipY
{
  float pixelScale = window->getWindowScaleFactor();
  CGSize logicalWindowSize = window->getLogicalWindowSize();

  *displayX = (double)windowX / pixelScale;
  *displayY = (double)windowY / pixelScale;

  if (flipY) {
    *displayY = logicalWindowSize.height - *displayY;
  }
}

- (UITextField *)getUITextField
{
  return text_field;
}

- (void)setupKeyboard:(const GHOST_KeyboardProperties &)keyboard_properties
{
  /* Initialise it if we have not already done so */
  if (!text_field) {
    [self initUITextField];
  }

  /* Save this set of keyboard properties */
  current_keyboard_properties = keyboard_properties;

  /* Convert the text box coords to display coords */
  CGRect displayRect;
  [self convertWindowCoordToDisplayCoordWithWindow:keyboard_properties.text_box_origin[0]
                                           windowY:keyboard_properties.text_box_origin[1]
                                          displayX:&displayRect.origin.x
                                          displayY:&displayRect.origin.y
                                             flipY:true];

  [self convertWindowCoordToDisplayCoordWithWindow:keyboard_properties.text_box_size[0]
                                           windowY:keyboard_properties.text_box_size[1]
                                          displayX:&displayRect.size.width
                                          displayY:&displayRect.size.height
                                             flipY:false];

  /* Where to display the text on-screen. */
  text_field.frame = displayRect;

  /* Initialise text with existing string. */
  text_field.text = keyboard_properties.text_string ?
                        [NSString stringWithUTF8String:keyboard_properties.text_string] :
                        @"";
  /* Take a copy of the string so we can restore it if neccessary */
  original_text = keyboard_properties.text_string ?
                      [NSString stringWithUTF8String:keyboard_properties.text_string] :
                      @"";

  /* Set keyboard type and text alignment.
   * NOTE - the keyboard type is only honoured if using an Apple
   * pencil or if the keyboard is floating.
   * Otherwise it will just be the default full screen type. */
  switch (keyboard_properties.keyboard_type) {
    case GHOST_KeyboardProperties::ascii_keyboard_type: {
      text_field.keyboardType = UIKeyboardTypeASCIICapable;
      text_field.textAlignment = NSTextAlignmentLeft;
      break;
    }
    case GHOST_KeyboardProperties::decimal_numpad_keyboard_type: {
      text_field.keyboardType = UIKeyboardTypeDecimalPad;
      text_field.textAlignment = NSTextAlignmentCenter;
      break;
    }
    case GHOST_KeyboardProperties::numpad_keyboard_type: {
      text_field.keyboardType = UIKeyboardTypeNumberPad;
      text_field.textAlignment = NSTextAlignmentCenter;
      break;
    }
    default: {
      /* What's the sensible baviour here? Default? Assert? */
      text_field.keyboardType = UIKeyboardTypeDefault;
      text_field.textAlignment = NSTextAlignmentLeft;
    }
  }
  /* Reset keyboard type to default if not using Apple Pencil
   * or it's not floating. (Need to add floating detection.) */
  if (!last_tap_with_pencil) {
    // text_field.keyboardType = UIKeyboardTypeDefault;
  }

  /* Set light/dark mode or adopt system default. */
  text_field.keyboardAppearance = UIKeyboardAppearanceDefault;

  /* This seems sensible given Blender's typical behaviour. */
  text_field.autocorrectionType = UITextAutocorrectionTypeNo;
  text_field.spellCheckingType = UITextSpellCheckingTypeNo;

  /* Set font size. */
  float fontSize = keyboard_properties.font_size / window->getWindowScaleFactor();
  text_field.font = [UIFont systemFontOfSize:fontSize];

  /* Set font color. */
  text_field.textColor = [UIColor colorWithRed:keyboard_properties.font_color[0]
                                         green:keyboard_properties.font_color[1]
                                          blue:keyboard_properties.font_color[2]
                                         alpha:keyboard_properties.font_color[3]];

  /* Initial highlighting and text-cursor position. */
  switch (keyboard_properties.inital_text_state) {
    case GHOST_KeyboardProperties::select_all_text: {
      [text_field selectAll:nil];
      break;
    }
    case GHOST_KeyboardProperties::select_text_range: {
      UITextPosition *startPosition = [text_field
          positionFromPosition:text_field.beginningOfDocument
                        offset:keyboard_properties.text_select_range[0]];
      UITextPosition *endPosition = [text_field
          positionFromPosition:text_field.beginningOfDocument
                        offset:keyboard_properties.text_select_range[1]];
      text_field.selectedTextRange = [text_field textRangeFromPosition:startPosition
                                                            toPosition:endPosition];
      break;
    }
    case GHOST_KeyboardProperties::move_cursor_to_start: {
      UITextPosition *beginning = text_field.beginningOfDocument;
      text_field.selectedTextRange = [text_field textRangeFromPosition:beginning
                                                            toPosition:beginning];
      break;
    }
    case GHOST_KeyboardProperties::move_cursor_to_end: {
      UITextPosition *end = text_field.endOfDocument;
      text_field.selectedTextRange = [text_field textRangeFromPosition:end toPosition:end];
      break;
    }
    default: {
      GHOST_ASSERT(FALSE, "GHOST_SystemIOS::setupTextField unsupported text select option");
    }
  }

  /* Setup the tool bar if it's enabled. */
  if (toolbar_enabled) {
    toolbar_live_text_item.title = text_field.text;
    toolbar_tip_item.title = keyboard_properties.tip_text ?
                                 [NSString stringWithCString:keyboard_properties.tip_text
                                                    encoding:NSUTF8StringEncoding] :
                                 @"";
  }
}

- (void)externalKeyboardChange:(NSNotification *)notification
{
  external_keyboard_connected = [GCKeyboard coalescedKeyboard] != nil;
  IOS_INPUT_LOG(@"External Keyboard %s",
                external_keyboard_connected ? "Connected" : "Disconnected");
}

/* IOS_FIXME - Not currently used, could be removed. */
- (void)keyboardWillChange:(NSNotification *)notification
{
  IOS_INPUT_LOG(@"KEYBOARD: Keyboard frame will change");

  CGRect keyboardRect = [notification.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue];
  /* Sometimes we see a zero value for the end-frame value, possibly because... timing? */
  if (keyboardRect.size.width == 0 || keyboardRect.size.height == 0) {
    keyboardRect = [notification.userInfo[UIKeyboardFrameBeginUserInfoKey] CGRectValue];
  }
}

/* UIKit keyboard press handling (fallback when not bypassing) */
- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event
{
  for (UIPress *press in presses) {
    [self handleKeyPress:press withEvent:event];
  }

  [super pressesBegan:presses withEvent:event];
}

- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event
{
  for (UIPress *press in presses) {
    [self handleKeyPress:press withEvent:event];
  }

  [super pressesEnded:presses withEvent:event];
}

- (void)pressesCancelled:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event
{
  [super pressesCancelled:presses withEvent:event];
}

- (const GHOST_TabletData)getTabletData
{
  return tablet_data;
}

- (GHOST_TSuccess)popupOnscreenKeyboard:(const GHOST_KeyboardProperties &)keyboard_properties
{
  @synchronized(self) {
    IOS_INPUT_LOG(@"Keyboard popup request received %@", text_field.text);

    [self setupKeyboard:keyboard_properties];

    if (!onscreen_keyboard_active) {
      text_field.userInteractionEnabled = YES;
      if (![text_field becomeFirstResponder]) {
        GHOST_ASSERT(FALSE, "GHOST_SystemIOS::popupOnScreenKeyboard Failed to display keyboard");
      }
      onscreen_keyboard_active = true;
    }
  }
  return GHOST_kSuccess;
}

- (GHOST_TSuccess)hideOnscreenKeyboard
{
  /* Lock access around keyboard handling events. */
  @synchronized(self) {
    IOS_INPUT_LOG(@"Keyboard hide request received %@", text_field.text);

    if (onscreen_keyboard_active) {
      /*
       This must come first so that any of the keyboard event handlers that get
       triggered in response to shutting down the keyboard don't do anything
       (like generating events back to Blender)
       */
      onscreen_keyboard_active = false;

      /* Shut down the keyboard. */
      [text_field resignFirstResponder];
      /*
       IOS_FIXME - Note: This may cause the console to display the warning message:
       "-[UIApplication _touchesEvent] will no longer work as expected. Please stop using it."
       But since this is being generated by Apple OS code there's nothing obvious to fix it right
       now.
       */

      IOS_INPUT_LOG(@"Resigned keyboard responder");
      /*
       This is required to disable any subsequent interactions with the text field that could
       potentially bypass Blender's input handling (since the UITextField is now live
       on the view)
       */
      text_field.userInteractionEnabled = NO;

      /* Save the input to a c-string */
      text_field_string = [[text_field text] UTF8String];

      /* Delete the text field copy of the string */
      text_field.text = nil;
    }
  }
  IOS_INPUT_LOG(@"Text field value was %s", text_field_string);
  return GHOST_kSuccess;
}

- (const char *)getLastKeyboardString
{
  /* Lock access around keyboard handling events */
  @synchronized(self) {

    /* Update text string if one exists */
    if (text_field.text && ![text_field.text isEqualToString:@""]) {
      /* Save the input to a c-string */
      text_field_string = [[text_field text] UTF8String];
    }
  }
  return text_field_string;
}

@end

@interface GHOST_IOSViewController : UIViewController

- (nonnull instancetype)initWithMetalKitView:(nonnull MTKView *)mtkView;

@end

@interface GHOST_IOSMetalRenderer : NSObject <MTKViewDelegate>

- (nonnull instancetype)initWithMetalKitView:(nonnull MTKView *)mtkView;

@end

@implementation GHOST_IOSViewController
{
  MTKView *_view;
  GHOST_IOSMetalRenderer *_renderer;
}

- (nonnull instancetype)initWithMetalKitView:(nonnull MTKView *)mtkView
{
  _view = mtkView;
  _view.multipleTouchEnabled = YES;
  self = [super init];
  self.view = (UIView *)mtkView;

  return self;
}

- (void)viewDidLoad
{
  [super viewDidLoad];
  _view = (MTKView *)self.view;
  _view.enableSetNeedsDisplay = NO;
  _view.device = MTLCreateSystemDefaultDevice();
  _view.clearColor = MTLClearColorMake(0, 0, 0, 1.0);
  _view.paused = NO;
  _view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
  _view.autoResizeDrawable = YES;
  _view.contentMode = UIViewContentModeScaleToFill;
  _view.contentScaleFactor = [[UIScreen mainScreen] scale];
  /* Set the refresh rate to the screen's maximum. There may be some value in capping
   * this value to preserve battery life (60fps seems to work well). */
  _view.preferredFramesPerSecond = [UIScreen mainScreen].maximumFramesPerSecond;
  _renderer = [[GHOST_IOSMetalRenderer alloc] initWithMetalKitView:_view];
  if (!_renderer) {
    NSLog(@"Renderer initialization failed");
    return;
  }

  [_renderer mtkView:_view drawableSizeWillChange:_view.drawableSize];

  _view.delegate = _renderer;
}

@end

@interface IOSAppDelegate : UIResponder <UIApplicationDelegate>

@property(strong, nonatomic) UIWindow *window;

@end

@implementation IOSAppDelegate

- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{

  main_ios_callback(argc, argv);

  return YES;
}

@end

@implementation GHOST_IOSMetalRenderer
{
  id<MTLDevice> _device;
  id<MTLCommandQueue> _commandQueue;
}

- (nonnull instancetype)initWithMetalKitView:(nonnull MTKView *)mtkView
{
  self = [super init];
  if (self) {
    _device = mtkView.device;

    /* Create the command queue. */
    _commandQueue = [_device newCommandQueue];
  }

  return self;
}

- (void)drawInMTKView:(nonnull MTKView *)drawInMTKView
{
  /* We should always have a window.. */
  if (current_active_window) {

    current_active_window->beginFrame();
  }

  /* Run the main loop to handle all events. */
  if (C) {
    WM_main_loop_body(C);
  }

  if (current_active_window) {
    current_active_window->flushDeferredSwapBuffers();
    current_active_window->endFrame();
  }

  /* Was there a request to switch windows? */
  if (next_active_window != nullptr) {
    if (current_active_window) {
      current_active_window->resignKeyWindow();
    }
    next_active_window->makeKeyWindow();
    next_active_window = nullptr;
  }
}

- (void)mtkView:(nonnull MTKView *)view drawableSizeWillChange:(CGSize)size
{
  if (!current_active_window) {
    return;
  }

  GHOST_SystemIOS *system = current_active_window->getSystem();
  system->pushEvent(
      new GHOST_Event(system->getMilliSeconds(), GHOST_kEventWindowSize, current_active_window));
}

@end

int GHOST_iosmain(int _argc, const char **_argv)
{
  argc = _argc;
  argv = _argv;
  @autoreleasepool {
    return UIApplicationMain(
        _argc, (char *_Nullable *)_argv, nil, NSStringFromClass([IOSAppDelegate class]));
  }
}

void GHOST_iosfinalize(bContext *CTX)
{
  C = CTX;
}

GHOST_WindowIOS::GHOST_WindowIOS(GHOST_SystemIOS *systemIos,
                                 const char *title,
                                 int32_t left,
                                 int32_t bottom,
                                 uint32_t width,
                                 uint32_t height,
                                 GHOST_TWindowState state,
                                 GHOST_TDrawingContextType type,
                                 const bool stereoVisual,
                                 bool /*is_debug*/,
                                 bool /*is_dialog*/,
                                 GHOST_WindowIOS *parentWindow)
    : GHOST_Window(width, height, state, stereoVisual, false), m_metalView(nil)
{
  m_fullScreen = false;
  m_systemIOS = systemIos;
  /* Parent window will be the window that focus is returned to upon close. */
  parent_window_ = parentWindow;
  m_window_title = nullptr;

  /* Create MTKView. */
  m_metalView = [[MTKView alloc] initWithFrame:CGRectMake(left, bottom, width, height)];
  [m_metalView retain];
  GHOST_ASSERT(m_metalView, "metalview not valid");

  /* Create view controller. */
  UIApplication *app = [UIApplication sharedApplication];
  GHOST_ASSERT(app, "App not valid");
  id<UIApplicationDelegate> app_delegate = [app delegate];
  GHOST_ASSERT(app_delegate, "App not valid");

  GHOSTUIWindow *ghost_rootWindow = nullptr;

  if (m_fullScreen) {
    /* Init window at native res. */
    ghost_rootWindow = [[GHOSTUIWindow alloc] init];
    [ghost_rootWindow retain];
    /* Ensure fullscreen. */
    CGRect rect = [UIScreen mainScreen].bounds;
    rootWindow.frame = rect;
  }
  else {
    /* Init window at specified size. */
    ghost_rootWindow = [[GHOSTUIWindow alloc]
        initWithFrame:CGRectMake(left, bottom, width, height)];
    [ghost_rootWindow retain];
    [ghost_rootWindow setClipsToBounds:YES];
  }

  rootWindow = (UIWindow *)ghost_rootWindow;

  [ghost_rootWindow setSystemAndWindowIOS:m_systemIOS windowIOS:this];
  rootWindow.windowLevel = UIWindowLevelAlert;

  GHOST_ASSERT(rootWindow, "UIWindow not valid");
  m_uiview_controller = [[[GHOST_IOSViewController alloc] initWithMetalKitView:m_metalView]
      retain];
  [m_uiview_controller viewDidLoad];
  GHOST_ASSERT(m_uiview_controller, "UIViewController not valid");

  /* Set presentation style depending on whether main window, dialog or temporary window. */
  if (m_fullScreen) {
    /* Initial window has no parent and is always fullscreen. */
    m_uiview_controller.modalPresentationStyle = UIModalPresentationFullScreen;
  }
  else {
    /* Initial window has no parent and is always fullscreen. */
    m_uiview_controller.modalPresentationStyle = UIModalPresentationPageSheet;
  }
  rootWindow.rootViewController = m_uiview_controller;

  /* Create UIView */
  GHOST_ASSERT(width > 0 && height > 0, "invalid wh");
  m_uiview = m_uiview_controller.view;
  GHOST_ASSERT(m_uiview, "uiview not valid");

  /* Initialize Metal device. */
  m_metalView.device = MTLCreateSystemDefaultDevice();

  /* Enable HDR/EDR Support. */
  CAMetalLayer *metalLayer = (CAMetalLayer *)m_metalView.layer;
  metalLayer.wantsExtendedDynamicRangeContent = YES;
  metalLayer.pixelFormat = MTLPixelFormatRGBA16Float;
  CGColorSpaceRef colorspace = CGColorSpaceCreateWithName(kCGColorSpaceExtendedSRGB);
  metalLayer.colorspace = colorspace;
  CGColorSpaceRelease(colorspace);

  setDrawingContextType(type);
  updateDrawingContext();
  activateDrawingContext();

  setTitle(title);

  /* Gesture recognizers. */
  [ghost_rootWindow registerGestureRecognizers];

  deferred_swap_buffers_count = 0;

  /* Deactive the parent (if it exists) and activate this one. */
  if (parent_window_) {
    parent_window_->requestToDeactivateWindow();
  }

  /* Make it the key window if there is no other window.
   * (Otherwise there will never be a call to drawInMTKView) */
  if (!current_active_window) {
    m_request_to_make_active = true;
    makeKeyWindow();
  }
  /* Activate this window at the end of the next draw loop. */
  else {
    requestToActivateWindow();
  }
}

GHOST_WindowIOS::~GHOST_WindowIOS()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

  releaseNativeHandles();

  /* Restore application control and display to parent window. */
  if (parent_window_) {
    parent_window_->requestToActivateWindow();
    parent_window_ = nil;
  }
  /* We have no choice but to resign, however this seems like it might cause issues. */
  if (current_active_window == this) {
    IOS_WINDOW_LOG(@"~GHOST_WindowIOS(): Warning, deactivating the active window %p?", this);
    requestToDeactivateWindow();
    resignKeyWindow();
  }

  if (m_metalView) {
    m_metalView.delegate = nil;
    [m_metalView release];
    m_metalView = nil;
  }
  if (m_uiview) {
    [m_uiview release];
    m_uiview = nil;
  }

  /* Release window. */
  if (rootWindow) {
    [rootWindow release];
    rootWindow = nil;
  }
  if (m_uiview_controller) {
    [m_uiview_controller release];
    m_uiview_controller = nil;
  }

  if (m_window_title) {
    free(m_window_title);
    m_window_title = nullptr;
  }

  [pool drain];
}

#pragma mark accessors

bool GHOST_WindowIOS::getValid() const
{
  MTKView *view = m_metalView;
  return GHOST_Window::getValid() && m_uiview != NULL && view != NULL;
}

void *GHOST_WindowIOS::getOSWindow() const
{
  return (void *)m_uiview;
}

GHOST_TSuccess GHOST_WindowIOS::swapBuffers()
{
  deferred_swap_buffers_count++;
  return GHOST_kSuccess;
}

void GHOST_WindowIOS::flushDeferredSwapBuffers()
{
  if (deferred_swap_buffers_count) {

    /* These two messages should be made asserts when we've fixed all the issues. */
    if (!getValid()) {
      IOS_WINDOW_LOG(@"Ignoring swap (invalid) con(%p) (win=%p)", getContext(), this);
      return;
    }

    if (!m_is_active_window) {
      IOS_WINDOW_LOG(@"Ignoring swap (not active window) con(%p) (win=%p)", getContext(), this);
      return;
    }

    IOS_WINDOW_LOG(@"Swapping (ui_View)%p (mtkView)%p con(%p) (win=%p)",
                   m_uiview,
                   m_metalView,
                   getContext(),
                   this);

    GHOST_ContextIOS *context = reinterpret_cast<GHOST_ContextIOS *>(getContext());
    context->swapBuffers();
    deferred_swap_buffers_count = 0;
  }
}

void GHOST_WindowIOS::beginFrame()
{
  GHOSTUIWindow *ui_window = (GHOSTUIWindow *)rootWindow;
  [ui_window beginFrame];
}

void GHOST_WindowIOS::endFrame()
{
  GHOSTUIWindow *ui_window = (GHOSTUIWindow *)rootWindow;
  [ui_window endFrame];
}

void GHOST_WindowIOS::setTitle(const char *title)
{
  if (m_window_title) {
    free(m_window_title);
    m_window_title = nullptr;
  }
  m_window_title = (char *)malloc(strlen(title) + 1);
  if (!m_window_title) {
    GHOST_ASSERT(getValid(), "GHOST_WindowIOS::setTitle(): Failed to alloc mem for window title");
  }
  strcpy(m_window_title, title);
  NSString *window_title = [NSString stringWithCString:title encoding:NSUTF8StringEncoding];
  m_uiview_controller.title = window_title;
}

std::string GHOST_WindowIOS::getTitle() const
{
  return m_window_title;
}

void GHOST_WindowIOS::getWindowBounds(GHOST_Rect &bounds) const
{
  GHOST_ASSERT(getValid(), "GHOST_WindowIOS::getWindowBounds(): window invalid");

  CGRect screenRect = rootWindow.frame;
  CGFloat scale = [UIScreen mainScreen].scale;
  CGFloat screenWidth = screenRect.size.width * scale;
  CGFloat screenHeight = screenRect.size.height * scale;

  bounds.m_b = screenHeight;
  bounds.m_l = rootWindow.frame.origin.x;
  bounds.m_r = screenWidth;
  bounds.m_t = rootWindow.frame.origin.y;
}

void GHOST_WindowIOS::getClientBounds(GHOST_Rect &bounds) const
{
  GHOST_ASSERT(getValid(), "GHOST_WindowIOS::getWindowBounds(): window invalid");

  CGRect screenRect = rootWindow.frame;
  CGFloat scale = [UIScreen mainScreen].scale;
  CGFloat screenWidth = screenRect.size.width * scale;
  CGFloat screenHeight = screenRect.size.height * scale;

  bounds.m_b = screenHeight;
  bounds.m_l = 0;
  bounds.m_r = screenWidth;
  bounds.m_t = 0;
}

GHOST_TSuccess GHOST_WindowIOS::setClientWidth(uint32_t /*width*/)
{
  /* Ignore on iOS fow now. */
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowIOS::setClientHeight(uint32_t /*height*/)
{
  /* Ignore on iOS fow now. */
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowIOS::setClientSize(uint32_t /*width*/, uint32_t /*height*/)
{
  /* Ignore on iOS fow now. */
  return GHOST_kSuccess;
}

GHOST_TWindowState GHOST_WindowIOS::getState() const
{
  /* TODO: Implement. */
  return GHOST_kWindowStateNormal;
}

void GHOST_WindowIOS::screenToClient(int32_t inX, int32_t inY, int32_t &outX, int32_t &outY) const
{
  /* Pass through for fullscreen windows.
   * TODO: Support coordinate mapping for sized windows. */
  outX = inX;
  outY = inY;
}

void GHOST_WindowIOS::clientToScreen(int32_t inX, int32_t inY, int32_t &outX, int32_t &outY) const
{
  /* Pass through for fullscreen windows.
   * TODO: Support coordinate mapping for sized windows. */
  outX = inX;
  outY = inY;
}

void GHOST_WindowIOS::screenToClientIntern(int32_t inX,
                                           int32_t inY,
                                           int32_t &outX,
                                           int32_t &outY) const
{
  /* Pass through for fullscreen windows.
   * TODO: Support coordinate mapping for sized windows. */
  outX = inX;
  outY = inY;
}

void GHOST_WindowIOS::clientToScreenIntern(int32_t inX,
                                           int32_t inY,
                                           int32_t &outX,
                                           int32_t &outY) const
{
  /* Pass through for fullscreen windows.
   * TODO: Support coordinate mapping for sized windows. */
  outX = inX;
  outY = inY;
}

/* called for event, when window leaves monitor to another */
void GHOST_WindowIOS::setNativePixelSize(void) {}

/**
 * \note Fullscreen switch is not actual fullscreen with display capture.
 * As this capture removes all OS X window manager features.
 *
 * Instead, the menu bar and the dock are hidden, and the window is made border-less and
 * enlarged. Thus, process switch, exposé, spaces, ... still work in fullscreen mode
 */
GHOST_TSuccess GHOST_WindowIOS::setState(GHOST_TWindowState /*state*/)
{
  // Ignore on iOS?
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowIOS::setModifiedState(bool isUnsavedChanges)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

  [pool drain];
  return GHOST_Window::setModifiedState(isUnsavedChanges);
}

GHOST_TSuccess GHOST_WindowIOS::setOrder(GHOST_TWindowOrder /*order*/)
{
  /* TODO: Support or deprecate for iOS */
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

  GHOST_ASSERT(getValid(), "GHOST_WindowIOS::setOrder(): window invalid");

  [pool drain];
  return GHOST_kSuccess;
}

#pragma mark Drawing context

GHOST_Context *GHOST_WindowIOS::newDrawingContext(GHOST_TDrawingContextType type)
{

  if (type == GHOST_kDrawingContextTypeMetal) {

    GHOST_Context *context = new GHOST_ContextIOS(m_uiview, m_metalView);

    if (context->initializeDrawingContext())
      return context;
    else
      delete context;
  }

  return NULL;
}

#pragma mark invalidate

GHOST_TSuccess GHOST_WindowIOS::invalidate()
{
  GHOST_ASSERT(getValid(), "GHOST_WindowIOS::invalidate(): window invalid");
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [pool drain];
  return GHOST_kSuccess;
}

#pragma mark Progress bar

GHOST_TSuccess GHOST_WindowIOS::setProgressBar(float /*progress*/)
{
  return GHOST_kSuccess;
}

static void postNotification() {}

GHOST_TSuccess GHOST_WindowIOS::endProgressBar()
{
  return GHOST_kSuccess;
}

#pragma mark Cursor handling

void GHOST_WindowIOS::loadCursor(bool /*visible*/, GHOST_TStandardCursor /*shape*/) const {}

bool GHOST_WindowIOS::isDialog() const
{
  return m_is_dialog;
}

GHOST_TSuccess GHOST_WindowIOS::setWindowCursorVisibility(bool /*visible*/)
{
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowIOS::setWindowCursorGrab(GHOST_TGrabCursorMode /*mode*/)
{
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowIOS::setWindowCursorShape(GHOST_TStandardCursor /*shape*/)
{
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowIOS::hasCursorShape(GHOST_TStandardCursor /*shape*/)
{
  return GHOST_kSuccess;
}

/** Reverse the bits in a uint16_t */
static uint16_t uns16ReverseBits(uint16_t shrt)
{
  shrt = ((shrt >> 1) & 0x5555) | ((shrt << 1) & 0xAAAA);
  shrt = ((shrt >> 2) & 0x3333) | ((shrt << 2) & 0xCCCC);
  shrt = ((shrt >> 4) & 0x0F0F) | ((shrt << 4) & 0xF0F0);
  shrt = ((shrt >> 8) & 0x00FF) | ((shrt << 8) & 0xFF00);
  return shrt;
}

GHOST_TSuccess GHOST_WindowIOS::setWindowCustomCursorShape(const uint8_t * /*bitmap*/,
                                                           const uint8_t * /*mask*/,
                                                           const int /*size*/[2],
                                                           const int /*hot_spot*/[2],
                                                           bool /*canInvertColor*/)
{
  /* Passthrough for iOS. */
  return GHOST_kSuccess;
}

uint16_t GHOST_WindowIOS::getDPIHint()
{
  /* IOS_FIXME: 96 is the default but the UI is too small to see
   * easily - especially on smaller iPads.
   * 192 looks good but cannot fit all the menus in so this value
   * is the compromise. It's possible we should look at the size of
   * screen and adjust this value dynamically. 192 might be OK for
   * the larger iPads but for now 144 seems to work OK. */
  return 144;
}

GHOST_TSuccess GHOST_WindowIOS::popupOnscreenKeyboard(
    const GHOST_KeyboardProperties &keyboard_properties)
{
  GHOSTUIWindow *ghost_rootWindow = (GHOSTUIWindow *)rootWindow;
  return [ghost_rootWindow popupOnscreenKeyboard:keyboard_properties];
}

GHOST_TSuccess GHOST_WindowIOS::hideOnscreenKeyboard()
{
  GHOSTUIWindow *ghost_rootWindow = (GHOSTUIWindow *)rootWindow;
  return [ghost_rootWindow hideOnscreenKeyboard];
}

const char *GHOST_WindowIOS::getLastKeyboardString()
{
  GHOSTUIWindow *ghost_rootWindow = (GHOSTUIWindow *)rootWindow;
  return [ghost_rootWindow getLastKeyboardString];
}

UITextField *GHOST_WindowIOS::getUITextField()
{
  GHOSTUIWindow *ghost_rootWindow = (GHOSTUIWindow *)rootWindow;
  return [ghost_rootWindow getUITextField];
}

const GHOST_TabletData GHOST_WindowIOS::getTabletData()
{
  GHOSTUIWindow *ghost_rootWindow = (GHOSTUIWindow *)rootWindow;
  return [ghost_rootWindow getTabletData];
}

/* This is the size of the window pre-scaled. */
CGSize GHOST_WindowIOS::getLogicalWindowSize()
{
  return m_metalView.frame.size;
}

/* This is the size of the window post-scaled. */
CGSize GHOST_WindowIOS::getNativeWindowSize()
{
  return m_metalView.drawableSize;
}

float GHOST_WindowIOS::getWindowScaleFactor()
{
  return [[UIScreen mainScreen] scale];
}

/* Indicate that we want this window to be the next active one. */
void GHOST_WindowIOS::requestToActivateWindow()
{
  /* Check we're not already active. */
  if (current_active_window != this) {
    /* Replace any outstanding requests. */
    if (next_active_window) {
      next_active_window->requestToDeactivateWindow();
    }
    m_request_to_make_active = true;
    next_active_window = this;
  }
}

void GHOST_WindowIOS::requestToDeactivateWindow()
{
  if (next_active_window == this) {
    IOS_WINDOW_LOG(@"requestToDeactivateWindow(): Has something gone wrong? %p", this);
    next_active_window = nullptr;
  }
  m_request_to_make_active = false;
}

bool GHOST_WindowIOS::makeKeyWindow()
{
  if (!getValid()) {
    IOS_WINDOW_LOG(@"Failed to activate (invalid) con(%p) (win=%p)", getContext(), this);
    return false;
  }

  GHOST_ContextIOS *context = reinterpret_cast<GHOST_ContextIOS *>(getContext());
  GHOST_ASSERT(rootWindow != nil, "GHOST_WindowIOS::makeKeyWindow() root window required");
  GHOST_ASSERT(context != nullptr, "GHOST_WindowIOS::makeKeyWindow() context required");
  GHOST_ASSERT(m_request_to_make_active,
               "GHOST_WindowIOS::makeKeyWindow() must request activation first");

  /* Make window primary visible window. */
  [rootWindow makeKeyAndVisible];
  /* Enable the drawInMTKView() calls for this window. */
  m_metalView.paused = NO;

  IOS_WINDOW_LOG(@"Activating (ui_View)%p (mtkView)%p con(%p) (win=%p)",
                 m_uiview,
                 m_metalView,
                 getContext(),
                 this);

  current_active_window = this;
  m_is_active_window = true;
  m_request_to_make_active = false;
  return true;
}

void GHOST_WindowIOS::resignKeyWindow()
{
  GHOST_ASSERT(current_active_window == this,
               "GHOST_WindowIOS::resignKeyWindow(): Can only resign current active window");
  GHOST_ASSERT(m_is_active_window,
               "GHOST_WindowIOS::resignKeyWindow(): Can't resign non active window");
  GHOST_ASSERT(!m_request_to_make_active,
               "GHOST_WindowIOS::resignKeyWindow(): activation request outstanding");

  /* Disable the drawInMTKView() calls for this window. */
  m_metalView.paused = YES;
  /* Wait until any outstanding presents in flight are done. */
  while (m_uiview_controller.beingPresented) {
  }
  IOS_WINDOW_LOG(@"Deactivating (ui_View)%p (mtkView)%p con(%p) (win=%p)",
                 m_uiview,
                 m_metalView,
                 getContext(),
                 this);
  m_is_active_window = false;
  current_active_window = nullptr;
}

CGPoint GHOST_WindowIOS::scalePointToWindow(CGPoint &point)
{
  CGPoint scaled_point;
  scaled_point.x = point.x * getWindowScaleFactor();
  scaled_point.y = point.y * getWindowScaleFactor();
  return scaled_point;
}

#ifdef WITH_INPUT_IME
void GHOST_WindowIOS::beginIME(
    int32_t /*x*/, int32_t /*y*/, int32_t /*w*/, int32_t /*h*/, bool /*completed*/)
{
  /* Passthrough for iOS. */
}

void GHOST_WindowIOS::endIME()
{
  /* Passthrough for iOS. */
}
#endif /* WITH_INPUT_IME */
