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
    [nativeWindow setCollectionBehavior:(existing & ~NSWindowCollectionBehaviorCanJoinAllSpaces) | NSWindowCollectionBehaviorMoveToActiveSpace];
    [nativeWindow setIgnoresMouseEvents:YES];
}
