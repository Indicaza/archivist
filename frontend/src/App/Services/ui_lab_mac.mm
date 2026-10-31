#import <AppKit/AppKit.h>

#include <QQuickWindow>

#include "ui_lab_mac.h"

void configureUiLabMacApplication()
{
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
}

void configureUiLabMacWindow(QQuickWindow *window)
{
    if (!window) {
        return;
    }
    auto *nativeView = reinterpret_cast<NSView *>(window->winId());
    NSWindow *nativeWindow = nativeView ? [nativeView window] : nil;
    if (!nativeWindow) {
        return;
    }
    const NSWindowCollectionBehavior existing = [nativeWindow collectionBehavior];
    const NSWindowCollectionBehavior behavior = static_cast<NSWindowCollectionBehavior>(
        (static_cast<NSUInteger>(existing) & ~static_cast<NSUInteger>(NSWindowCollectionBehaviorCanJoinAllSpaces))
        | static_cast<NSUInteger>(NSWindowCollectionBehaviorMoveToActiveSpace));
    [nativeWindow setCollectionBehavior:behavior];
    [nativeWindow setLevel:(NSNormalWindowLevel - 1)];
    [nativeWindow orderBack:nil];
    [nativeWindow setIgnoresMouseEvents:NO];
}
