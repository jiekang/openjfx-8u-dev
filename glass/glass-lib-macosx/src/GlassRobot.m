/*
 * Copyright (c) 2011, 2013, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#import "common.h"
#import "com_sun_glass_ui_mac_MacRobot.h"

#import <CoreServices/CoreServices.h>

#import "GlassMacros.h"
#import "GlassKey.h"

//#define VERBOSE
#ifndef VERBOSE
    #define LOG(MSG, ...)
#else
    #define LOG(MSG, ...) GLASS_LOG(MSG, ## __VA_ARGS__);
#endif

#define kMouseButtonNone 0

static inline void DumpImage(CGImageRef image)
{
    fprintf(stderr, "CGImageRef: %p\n", image);
    if (image != NULL)
    {
        fprintf(stderr, "    CGImageGetWidth(): %d\n", (int)CGImageGetWidth(image));
        fprintf(stderr, "    CGImageGetHeight(): %d\n", (int)CGImageGetHeight(image));
        fprintf(stderr, "    CGImageGetBitsPerComponent(): %d\n", (int)CGImageGetBitsPerComponent(image));
        fprintf(stderr, "    CGImageGetBitsPerPixel(): %d\n", (int)CGImageGetBitsPerPixel(image));
        fprintf(stderr, "    CGImageGetBytesPerRow(): %d\n", (int)CGImageGetBytesPerRow(image));
        CGImageAlphaInfo alpha = CGImageGetAlphaInfo(image) & kCGBitmapAlphaInfoMask;
        switch (alpha)
        {
            case kCGImageAlphaNone: fprintf(stderr, "    CGImageGetAlphaInfo(): kCGImageAlphaNone\n"); break;
            case kCGImageAlphaPremultipliedLast: fprintf(stderr, "    CGImageGetAlphaInfo(): kCGImageAlphaPremultipliedLast\n"); break;
            case kCGImageAlphaPremultipliedFirst: fprintf(stderr, "    CGImageGetAlphaInfo(): kCGImageAlphaPremultipliedFirst\n"); break;
            case kCGImageAlphaLast: fprintf(stderr, "    CGImageGetAlphaInfo(): kCGImageAlphaLast\n"); break;
            case kCGImageAlphaFirst: fprintf(stderr, "    CGImageGetAlphaInfo(): kCGImageAlphaFirst\n"); break;
            case kCGImageAlphaNoneSkipLast: fprintf(stderr, "    CGImageGetAlphaInfo(): kCGImageAlphaNoneSkipLast\n"); break;
            case kCGImageAlphaNoneSkipFirst: fprintf(stderr, "    CGImageGetAlphaInfo(): kCGImageAlphaNoneSkipFirst\n"); break;
            case kCGImageAlphaOnly: fprintf(stderr, "    CGImageGetAlphaInfo(): kCGImageAlphaOnly\n"); break;
            default: fprintf(stderr, "    CGImageGetAlphaInfo(): unknown\n");
        }
        CGBitmapInfo bitmap = CGImageGetBitmapInfo(image) & kCGBitmapByteOrderMask;
        switch (bitmap)
        {
            case kCGBitmapByteOrderDefault: fprintf(stderr, "    CGImageGetBitmapInfo(): kCGBitmapByteOrderDefault\n"); break;
            case kCGBitmapByteOrder16Little: fprintf(stderr, "    CGImageGetBitmapInfo(): kCGBitmapByteOrder16Little\n"); break;
            case kCGBitmapByteOrder32Little: fprintf(stderr, "    CGImageGetBitmapInfo(): kCGBitmapByteOrder32Little\n"); break;
            case kCGBitmapByteOrder16Big: fprintf(stderr, "    CGImageGetBitmapInfo(): kCGBitmapByteOrder16Big\n"); break;
            case kCGBitmapByteOrder32Big: fprintf(stderr, "    CGImageGetBitmapInfo(): kCGBitmapByteOrder32Big\n"); break;
            default: fprintf(stderr, "    CGImageGetBitmapInfo(): unknown\n");
        }
    }
}

static inline void PostGlassMouseEvent(CGPoint location, UInt32 buttons, BOOL buttonPressed)
{
    // for each one bit in buttons, post a new mouse {press/release} event
    if (buttons != 0) {
        for (UInt32 index = 0; buttons != 0; index++, buttons >>= 1) {
            if (buttons & 1) {
                CGEventType type;
                switch (index) {
                    case 0:
                        type = buttonPressed ? kCGEventLeftMouseDown : kCGEventLeftMouseUp;
                        break;
                    case 1:
                        type = buttonPressed ? kCGEventRightMouseDown : kCGEventRightMouseUp;
                        break;
                    default:
                        type = buttonPressed ? kCGEventOtherMouseDown : kCGEventOtherMouseUp;
                        break;
                }
                
                CGEventRef newEvent = CGEventCreateMouseEvent(NULL, type, location, (CGMouseButton)index);
                CGEventPost(kCGHIDEventTap, newEvent);
                CFRelease(newEvent);
            }
        }
    }
}

@interface GlassRobot : NSObject
{
    UInt32 mouseButtons;
}

- (void)mouseMove:(NSValue*)p;
- (void)mousePress:(NSNumber*)buttons;
- (void)mouseRelease:(NSNumber*)buttons;
- (void)getMousePos:(NSMutableArray*)args;
@end

@implementation GlassRobot

- (id)init
{
    self = [super init];
    if (self != nil)
    {
        self->mouseButtons = kMouseButtonNone;
    }
    return self;
}


- (void)mouseMove:(NSValue*)p
{
    CGPoint location = NSPointToCGPoint([p pointValue]);
    UInt32 buttons = self->mouseButtons;
    CGEventType type=kCGEventMouseMoved;
    UInt32 index=0;
    for (; buttons != 0; index++, buttons >>= 1)
    {
        if (buttons & 1)
        {        
            switch (index)
            {
                case 0:
                    type = kCGEventLeftMouseDragged;
                    break;
                case 1:
                    type = kCGEventRightMouseDragged;
                    break;
                default:
                    type = kCGEventOtherMouseDragged;
                    break;
            }
        }
    }
    CGEventRef newEvent = CGEventCreateMouseEvent(NULL, type, location, (CGMouseButton)index);
    CGEventPost(kCGHIDEventTap, newEvent);
    CGWarpMouseCursorPosition(location);
    CFRelease(newEvent);

}

- (CGPoint)getMousePosFlipped
{
    CGPoint where = NSPointToCGPoint([NSEvent mouseLocation]);
    NSScreen * screen = [[NSScreen screens] objectAtIndex: 0];
    NSRect screenFrame = screen.frame;
    where.y = screenFrame.size.height - where.y;
    return where;
}

- (void)mousePress:(NSNumber*)buttons
{
    UInt32 newPressed = (UInt32)[buttons unsignedLongValue];
    //Add new pressed buttons
    self->mouseButtons = self->mouseButtons | newPressed;
    PostGlassMouseEvent([self getMousePosFlipped], newPressed, YES);
}

- (void)mouseRelease:(NSNumber*)buttons
{
    UInt32 newReleased = (UInt32)[buttons unsignedLongValue];
    PostGlassMouseEvent([self getMousePosFlipped], newReleased, NO);
    //reset buttons
    self->mouseButtons = self->mouseButtons & (~newReleased);
}

- (void)getMousePos:(NSMutableArray*)args
{
    CGPoint where = [self getMousePosFlipped];
    [args addObject: [NSValue valueWithPoint: *(NSPoint*)&where]];
}

@end

/*
 * Class:     com_sun_glass_ui_mac_MacRobot
 * Method:    _init
 * Signature: ()J
 */
JNIEXPORT jlong JNICALL Java_com_sun_glass_ui_mac_MacRobot__1init
(JNIEnv *env, jobject jrobot)
{
    LOG("Java_com_sun_glass_ui_mac_MacRobot__1init");
    
    return ptr_to_jlong([[GlassRobot alloc] init]);
}

/*
 * Class:     com_sun_glass_ui_mac_MacRobot
 * Method:    _destroy
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_com_sun_glass_ui_mac_MacRobot__1destroy
(JNIEnv *env, jobject jThis, jlong ptr)
{
    LOG("Java_com_sun_glass_ui_mac_MacRobot__1destroy");

    [(GlassRobot*)jlong_to_ptr(ptr) release];
}

/*
 * Class:     com_sun_glass_ui_mac_MacRobot
 * Method:    _keyPress
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_com_sun_glass_ui_mac_MacRobot__1keyPress
(JNIEnv *env, jobject jrobot, jint code)
{
    LOG("Java_com_sun_glass_ui_mac_MacRobot__1keyPress");
    
    GLASS_ASSERT_MAIN_JAVA_THREAD(env);
    GLASS_POOL_ENTER
    {
        unsigned short macCode;
        if (GetMacKey(code, &macCode)) {
            CGEventRef newEvent = CGEventCreateKeyboardEvent(NULL, macCode, true);
            CGEventPost(kCGHIDEventTap, newEvent);
            CFRelease(newEvent);
        }
    }
    GLASS_POOL_EXIT;
}

/*
 * Class:     com_sun_glass_ui_mac_MacRobot
 * Method:    _keyRelease
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_com_sun_glass_ui_mac_MacRobot__1keyRelease
(JNIEnv *env, jobject jrobot, jint code)
{
    LOG("Java_com_sun_glass_ui_mac_MacRobot__1keyRelease");
    
    GLASS_ASSERT_MAIN_JAVA_THREAD(env);
    GLASS_POOL_ENTER
    {
        unsigned short macCode;
        if (GetMacKey(code, &macCode)) {
            CGEventRef newEvent = CGEventCreateKeyboardEvent(NULL, macCode, false);
            CGEventPost(kCGHIDEventTap, newEvent);
            CFRelease(newEvent);
        }
    }
    GLASS_POOL_EXIT;
}

/*
 * Class:     com_sun_glass_ui_mac_MacRobot
 * Method:    _mouseMove
 * Signature: (JII)V
 */
JNIEXPORT void JNICALL Java_com_sun_glass_ui_mac_MacRobot__1mouseMove
(JNIEnv *env, jobject jrobot, jlong ptr, jint x, jint y)
{
        LOG("Java_com_sun_glass_ui_mac_MacRobot__1mouseMove");
    
    GLASS_ASSERT_MAIN_JAVA_THREAD(env);
    GLASS_POOL_ENTER
    {
        GLASS_PERFORM_WITH_ARG((GlassRobot*)jlong_to_ptr(ptr),
                mouseMove, [NSValue valueWithPoint: NSMakePoint((float)x, (float)y)], NO);
    }
    GLASS_POOL_EXIT;
}

/*
 * Class:     com_sun_glass_ui_mac_MacRobot
 * Method:    _getMouseX
 * Signature: (J)I
 */
JNIEXPORT jint JNICALL Java_com_sun_glass_ui_mac_MacRobot__1getMouseX
(JNIEnv *env, jobject jrobot, jlong ptr)
{
        LOG("Java_com_sun_glass_ui_mac_MacRobot__1getMouseX");
    
    jint x = 0;
    
    GLASS_ASSERT_MAIN_JAVA_THREAD(env);
    GLASS_POOL_ENTER
    {
        NSMutableArray * args = [NSMutableArray arrayWithCapacity: 0];

        GLASS_PERFORM_WITH_ARG((GlassRobot*)jlong_to_ptr(ptr),
                getMousePos, args, YES);

        x = (jint)[(NSValue*)[args lastObject] pointValue].x;
    }
    GLASS_POOL_EXIT;
    
    return x;
}

/*
 * Class:     com_sun_glass_ui_mac_MacRobot
 * Method:    _getMouseY
 * Signature: (J)I
 */
JNIEXPORT jint JNICALL Java_com_sun_glass_ui_mac_MacRobot__1getMouseY
(JNIEnv *env, jobject jrobot, jlong ptr)
{
        LOG("Java_com_sun_glass_ui_mac_MacRobot__1getMouseY");
    
    jint y = 0;
    
    GLASS_ASSERT_MAIN_JAVA_THREAD(env);
    GLASS_POOL_ENTER
    {
        NSMutableArray * args = [NSMutableArray arrayWithCapacity: 0];

        GLASS_PERFORM_WITH_ARG((GlassRobot*)jlong_to_ptr(ptr),
                getMousePos, args, YES);

        y = (jint)[(NSValue*)[args lastObject] pointValue].y;
    }
    GLASS_POOL_EXIT;
    
    return y;
}

/*
 * Class:     com_sun_glass_ui_mac_MacRobot
 * Method:    _mousePress
 * Signature: (JI)V
 */
JNIEXPORT void JNICALL Java_com_sun_glass_ui_mac_MacRobot__1mousePress
(JNIEnv *env, jobject jrobot, jlong ptr, jint buttons)
{
    LOG("Java_com_sun_glass_ui_mac_MacRobot__1mousePress");
    
    GLASS_ASSERT_MAIN_JAVA_THREAD(env);
    GLASS_POOL_ENTER
    {
        GLASS_PERFORM_WITH_ARG((GlassRobot*)jlong_to_ptr(ptr),
                mousePress, [NSNumber numberWithInt: buttons], NO);
    }
    GLASS_POOL_EXIT;
}

/*
 * Class:     com_sun_glass_ui_mac_MacRobot
 * Method:    _mouseRelease
 * Signature: (JI)V
 */
JNIEXPORT void JNICALL Java_com_sun_glass_ui_mac_MacRobot__1mouseRelease
(JNIEnv *env, jobject jrobot, jlong ptr, jint buttons)
{
    LOG("Java_com_sun_glass_ui_mac_MacRobot__1mouseRelease");
    
    GLASS_ASSERT_MAIN_JAVA_THREAD(env);
    GLASS_POOL_ENTER
    {
        GLASS_PERFORM_WITH_ARG((GlassRobot*)jlong_to_ptr(ptr),
                mouseRelease, [NSNumber numberWithInt: buttons], NO);
    }
    GLASS_POOL_EXIT;
}

/*
 * Class:     com_sun_glass_ui_mac_MacRobot
 * Method:    _mouseWheel
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_com_sun_glass_ui_mac_MacRobot__1mouseWheel
(JNIEnv *env, jobject jrobot, jint wheelAmt)
{
    LOG("Java_com_sun_glass_ui_mac_MacRobot__1mouseWheel");
    
    GLASS_ASSERT_MAIN_JAVA_THREAD(env);
    GLASS_POOL_ENTER
    {
        CGEventRef newEvent = CGEventCreateScrollWheelEvent(NULL, kCGScrollEventUnitPixel, 1, (int32_t)wheelAmt);
        CGEventPost(kCGHIDEventTap, newEvent);
        CFRelease(newEvent);
    }
    GLASS_POOL_EXIT;
}

/*
 * Class:     com_sun_glass_ui_mac_MacRobot
 * Method:    _getPixelColor
 * Signature: (II)I
 */
JNIEXPORT jint JNICALL Java_com_sun_glass_ui_mac_MacRobot__1getPixelColor
(JNIEnv *env, jobject jrobot, jint x, jint y)
{
    LOG("Java_com_sun_glass_ui_mac_MacRobot__1getPixelColor");
    
    jint color = 0;
    
    GLASS_ASSERT_MAIN_JAVA_THREAD(env);
    GLASS_POOL_ENTER
    {
        CGRect bounds = CGRectMake((CGFloat)x, (CGFloat)y, 1.0f, 1.0f);
        CGImageRef screenImage = CGWindowListCreateImage(bounds, kCGWindowListOptionOnScreenOnly, kCGNullWindowID, kCGWindowImageDefault);
        if (screenImage != NULL)
        {
            //DumpImage(screenImage);
            CGDataProviderRef provider = CGImageGetDataProvider(screenImage);
            if (provider != NULL)
            {
                CFDataRef data = CGDataProviderCopyData(provider);
                if (data != NULL)
                {
                    jint *pixels = (jint*)CFDataGetBytePtr(data);
                    if (pixels != NULL)
                    {
                        color = *pixels;
                    }
                }
                CFRelease(data);
            }
            CGImageRelease(screenImage);
        }
    }
    GLASS_POOL_EXIT;
    
    return color;
}

/*
 * Class:     com_sun_glass_ui_mac_MacRobot
 * Method:    _getScreenCapture
 * Signature: (IIII[I;)V
 */
JNIEXPORT void JNICALL Java_com_sun_glass_ui_mac_MacRobot__1getScreenCapture
(JNIEnv *env, jobject jrobot, jint x, jint y, jint width, jint height, jintArray pixelArray)
{
    LOG("Java_com_sun_glass_ui_mac_MacRobot__1getScreenCapture");
    
    GLASS_ASSERT_MAIN_JAVA_THREAD(env);
    GLASS_POOL_ENTER
    {
        jint *javaPixels = (jint*)(*env)->GetIntArrayElements(env, pixelArray, 0);
        if (javaPixels != NULL)
        {
            CGRect bounds = CGRectMake((CGFloat)x, (CGFloat)y, (CGFloat)width, (CGFloat)height);
            CGImageRef screenImage = CGWindowListCreateImage(bounds, kCGWindowListOptionOnScreenOnly, kCGNullWindowID, kCGWindowImageDefault);
            if (screenImage != NULL)
            {
                CGDataProviderRef provider = CGImageGetDataProvider(screenImage);
                if (provider != NULL)
                {
                    CFDataRef data = CGDataProviderCopyData(provider);
                    if (data != NULL)
                    {
                        jint *screenPixels = (jint*)CFDataGetBytePtr(data);
                        if (screenPixels != NULL)
                        {
                            memcpy(javaPixels, screenPixels, width*height);
                        }
                    }
                    CFRelease(data);
                }
                CGImageRelease(screenImage);
            }
        }
        (*env)->ReleaseIntArrayElements(env, pixelArray, javaPixels, 0);
    }
    GLASS_POOL_EXIT;
}