#include <ApplicationServices/ApplicationServices.h>
#include <math.h>

#define DEFAULT_BUTTON 3
#define DEFAULT_KEYS kCGEventFlagMaskShift
#define DEFAULT_SPEED 3
#define MAX_KEY_COUNT 5
#define EQ(x, y) (CFStringCompare(x, y, kCFCompareCaseInsensitive) == kCFCompareEqualTo)

// Autoscroll tuning (feel free to tweak)
#define TIMER_HZ 60.0
#define DEADZONE_PX 6.0
#define MAX_SCROLL_PER_TICK 80.0
#define GAIN 0.20   // converts pixels-from-anchor into scroll-per-tick; higher = faster

static const CFStringRef AX_NOTIFICATION = CFSTR("com.apple.accessibility.api");
static bool TRUSTED;

static int BUTTON;
static int KEYS;
static int SPEED;

static bool BUTTON_ENABLED; // toggled by middle click
static bool KEY_ENABLED;    // held modifier keys
static CGPoint ANCHOR_POINT;

static CFRunLoopTimerRef AUTOSCROLL_TIMER = NULL;

static CGPoint currentMouseLocation(void)
{
    CGEventRef e = CGEventCreate(NULL);
    CGPoint p = CGEventGetLocation(e);
    CFRelease(e);
    return p;
}

static double clampd(double v, double lo, double hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void stopAutoscrollTimer(void)
{
    if (AUTOSCROLL_TIMER) {
        CFRunLoopTimerInvalidate(AUTOSCROLL_TIMER);
        CFRelease(AUTOSCROLL_TIMER);
        AUTOSCROLL_TIMER = NULL;
    }
}

static void autoscrollTick(CFRunLoopTimerRef timer, void *info)
{
    (void)timer;
    (void)info;

    bool active = (BUTTON_ENABLED || KEY_ENABLED);
    if (!active) {
        stopAutoscrollTimer();
        return;
    }

    CGPoint cur = currentMouseLocation();
    double dx = cur.x - ANCHOR_POINT.x;
    double dy = cur.y - ANCHOR_POINT.y;

    // Deadzone so tiny hand jitter doesn't scroll
    if (fabs(dx) < DEADZONE_PX) dx = 0.0;
    if (fabs(dy) < DEADZONE_PX) dy = 0.0;

    // Convert distance-from-anchor into per-tick scroll deltas (velocity-style)
    // Negative because screen coordinates increase downward.
    double scrollY = -(double)SPEED * GAIN * dy;
    double scrollX = -(double)SPEED * GAIN * dx;

    scrollY = clampd(scrollY, -MAX_SCROLL_PER_TICK, MAX_SCROLL_PER_TICK);
    scrollX = clampd(scrollX, -MAX_SCROLL_PER_TICK, MAX_SCROLL_PER_TICK);

    // If we're in the deadzone, do nothing
    if (scrollX == 0.0 && scrollY == 0.0) return;

    CGEventRef scrollWheelEvent = CGEventCreateScrollWheelEvent(
        NULL, kCGScrollEventUnitPixel, 2, (int)scrollY, (int)scrollX
    );

    // If key-mode is active, strip the modifier keys from the synthetic scroll event
    // so apps don't interpret Shift/Option/etc. as part of the scroll gesture.
    if (KEY_ENABLED) {
        CGEventFlags f = CGEventGetFlags(scrollWheelEvent);
        CGEventSetFlags(scrollWheelEvent, f & ~((CGEventFlags)KEYS));
    }

    CGEventPost(kCGHIDEventTap, scrollWheelEvent);
    CFRelease(scrollWheelEvent);
}

static void startAutoscrollTimerIfNeeded(void)
{
    if (AUTOSCROLL_TIMER) return;

    CFRunLoopTimerContext ctx = {0};
    double interval = 1.0 / TIMER_HZ;

    AUTOSCROLL_TIMER = CFRunLoopTimerCreate(
        kCFAllocatorDefault,
        CFAbsoluteTimeGetCurrent() + interval,
        interval,
        0, 0,
        autoscrollTick,
        &ctx
    );

    CFRunLoopAddTimer(CFRunLoopGetCurrent(), AUTOSCROLL_TIMER, kCFRunLoopDefaultMode);
}

static void updateAutoscrollActiveState(CGEventRef eventForAnchor)
{
    bool active = (BUTTON_ENABLED || KEY_ENABLED);

    if (active) {
        // Capture anchor at the moment we become active.
        if (eventForAnchor) {
            ANCHOR_POINT = CGEventGetLocation(eventForAnchor);
        } else {
            ANCHOR_POINT = currentMouseLocation();
        }
        startAutoscrollTimerIfNeeded();
    } else {
        stopAutoscrollTimer();
    }
}

static CGEventRef tapCallback(CGEventTapProxy proxy,
                              CGEventType type, CGEventRef event, void *userInfo)
{
    (void)proxy;
    (void)userInfo;

    // Middle click toggles autoscroll mode
    if (type == kCGEventOtherMouseDown
        && CGEventGetFlags(event) == 0
        && CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber) == BUTTON) {

        BUTTON_ENABLED = !BUTTON_ENABLED;
        updateAutoscrollActiveState(event);

        // swallow the click so apps don't receive it
        return NULL;
    }

    // Modifier keys enable/disable key-mode scrolling
    if (type == kCGEventFlagsChanged) {
        bool was = KEY_ENABLED;
        KEY_ENABLED = ((CGEventGetFlags(event) & (CGEventFlags)KEYS) == (CGEventFlags)KEYS);

        // Only reset anchor when transitioning from inactive -> active
        if (!was && KEY_ENABLED) {
            updateAutoscrollActiveState(event);
        } else if (was && !KEY_ENABLED && !BUTTON_ENABLED) {
            updateAutoscrollActiveState(NULL);
        }

        return event;
    }

    return event;
}

static void displayNoticeAndExit(CFStringRef alertHeader)
{
    CFUserNotificationDisplayNotice(
        0, kCFUserNotificationCautionAlertLevel,
        NULL, NULL, NULL,
        alertHeader, NULL, NULL
    );

    exit(EXIT_FAILURE);
}

static void notificationCallback(CFNotificationCenterRef center, void *observer,
                                 CFNotificationName name, const void *object,
                                 CFDictionaryRef userInfo)
{
    (void)center; (void)observer; (void)name; (void)object; (void)userInfo;

    CFRunLoopRef runLoop = CFRunLoopGetCurrent();
    CFRunLoopPerformBlock(
        runLoop, kCFRunLoopDefaultMode, ^{
            bool previouslyTrusted = TRUSTED;
            if ((TRUSTED = AXIsProcessTrusted()) && !previouslyTrusted)
                CFRunLoopStop(runLoop);
        }
    );
}

static bool getIntPreference(CFStringRef key, int *valuePtr)
{
    CFNumberRef number = (CFNumberRef)CFPreferencesCopyAppValue(
        key, kCFPreferencesCurrentApplication
    );
    bool got = false;
    if (number) {
        if (CFGetTypeID(number) == CFNumberGetTypeID())
            got = CFNumberGetValue(number, kCFNumberIntType, valuePtr);
        CFRelease(number);
    }

    return got;
}

static bool getArrayPreference(CFStringRef key, CFStringRef *values, int *count, int maxCount)
{
    CFArrayRef array = (CFArrayRef)CFPreferencesCopyAppValue(
        key, kCFPreferencesCurrentApplication
    );
    bool got = false;
    if (array) {
        if (CFGetTypeID(array) == CFArrayGetTypeID()) {
            CFIndex c = CFArrayGetCount(array);
            if (c <= maxCount) {
                CFArrayGetValues(array, CFRangeMake(0, c), (const void **)values);
                *count = (int)c;
                got = true;
            }
        }
        CFRelease(array);
    }

    return got;
}

int main(void)
{
    CFNotificationCenterRef center = CFNotificationCenterGetDistributedCenter();
    char observer;
    CFNotificationCenterAddObserver(
        center, &observer, notificationCallback, AX_NOTIFICATION, NULL,
        CFNotificationSuspensionBehaviorDeliverImmediately
    );
    CFDictionaryRef options = CFDictionaryCreate(
        kCFAllocatorDefault,
        (const void **)&kAXTrustedCheckOptionPrompt, (const void **)&kCFBooleanTrue, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks
    );
    TRUSTED = AXIsProcessTrustedWithOptions(options);
    CFRelease(options);
    if (!TRUSTED)
        CFRunLoopRun();
    CFNotificationCenterRemoveObserver(center, &observer, AX_NOTIFICATION, NULL);

    if (!(getIntPreference(CFSTR("button"), &BUTTON)
          && (BUTTON == 0 || (BUTTON >= 3 && BUTTON <= 32))))
        BUTTON = DEFAULT_BUTTON;

    CFStringRef keyNames[MAX_KEY_COUNT];
    int keyCount;
    if (getArrayPreference(CFSTR("keys"), keyNames, &keyCount, MAX_KEY_COUNT)) {
        KEYS = 0;
        for (int i = 0; i < keyCount; i++) {
            if (EQ(keyNames[i], CFSTR("capslock"))) {
                KEYS |= kCGEventFlagMaskAlphaShift;
            } else if (EQ(keyNames[i], CFSTR("shift"))) {
                KEYS |= kCGEventFlagMaskShift;
            } else if (EQ(keyNames[i], CFSTR("control"))) {
                KEYS |= kCGEventFlagMaskControl;
            } else if (EQ(keyNames[i], CFSTR("option"))) {
                KEYS |= kCGEventFlagMaskAlternate;
            } else if (EQ(keyNames[i], CFSTR("command"))) {
                KEYS |= kCGEventFlagMaskCommand;
            } else {
                KEYS = DEFAULT_KEYS;
                break;
            }
        }
    } else {
        KEYS = DEFAULT_KEYS;
    }

    if (!getIntPreference(CFSTR("speed"), &SPEED))
        SPEED = DEFAULT_SPEED;

    // Event tap: we no longer need MouseMoved for scrolling (timer drives it).
    CGEventMask events = 0;
    if (BUTTON != 0) {
        events |= CGEventMaskBit(kCGEventOtherMouseDown);
        BUTTON--; // convert preference to 0-based button number
    }
    if (KEYS != 0)
        events |= CGEventMaskBit(kCGEventFlagsChanged);

    CFMachPortRef tap = CGEventTapCreate(
        kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault,
        events, tapCallback, NULL
    );
    if (!tap)
        displayNoticeAndExit(CFSTR("DragScroll could not create an event tap."));
    CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0);
    if (!source)
        displayNoticeAndExit(CFSTR("DragScroll could not create a run loop source."));
    CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopDefaultMode);
    CFRelease(tap);
    CFRelease(source);

    CFRunLoopRun();
    return EXIT_SUCCESS;
}
