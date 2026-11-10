#include "NativePluginCapture.h"
#import <AppKit/AppKit.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreGraphics/CoreGraphics.h>
#include <unistd.h>

bool captureLibertyPluginWindowNative(const juce::String& pluginName,
                                      const juce::File& output,
                                      juce::String& error)
{
    if (@available(macOS 14.0, *))
    {
        @autoreleasepool
        {
            __block SCShareableContent* shareable = nil;
            __block NSError* discoveryError = nil;
            dispatch_semaphore_t discovery = dispatch_semaphore_create(0);
            [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                onScreenWindowsOnly:YES completionHandler:^(SCShareableContent* content, NSError* e) {
                    shareable = content;
                    discoveryError = e;
                    dispatch_semaphore_signal(discovery);
                }];
            if (dispatch_semaphore_wait(discovery, dispatch_time(DISPATCH_TIME_NOW, 8 * NSEC_PER_SEC)) != 0)
            {
                error = "Delai depasse lors de la recherche des fenetres macOS.";
                return false;
            }
            if (shareable == nil)
            {
                error = "ScreenCaptureKit : " + juce::String(discoveryError.localizedDescription.UTF8String ?: "fenetres indisponibles");
                return false;
            }

            // Match the actual NSWindow peer rather than assuming a plugin
            // window's title contains the plug-in name. Many AU/VST3 hosts
            // expose only a generic title (or no title at all).
            NSString* wanted = [NSString stringWithUTF8String:pluginName.toRawUTF8()];
            NSMutableSet<NSNumber*>* eligibleWindowIds = [NSMutableSet set];
            NSNumber* preferredWindowId = nil;
            for (NSWindow* nativeWindow in [NSApp orderedWindows])
            {
                if (![nativeWindow isVisible] || [nativeWindow isMiniaturized]) continue;
                if (nativeWindow.frame.size.width < 80 || nativeWindow.frame.size.height < 60) continue;
                if ([nativeWindow isKindOfClass:[NSPanel class]]) continue;
                NSNumber* windowId = @([nativeWindow windowNumber]);
                [eligibleWindowIds addObject:windowId];
                if (wanted.length > 0
                    && [nativeWindow.title localizedCaseInsensitiveContainsString:wanted])
                    preferredWindowId = windowId;
            }

            SCWindow* target = nil;
            CGFloat bestArea = 0;
            for (SCWindow* window in shareable.windows)
            {
                if (window.owningApplication.processID != getpid()) continue;
                if (window.frame.size.width < 80 || window.frame.size.height < 60) continue;
                NSNumber* windowId = @(window.windowID);
                if (![eligibleWindowIds containsObject:windowId]) continue;
                if (preferredWindowId != nil && [windowId isEqualToNumber:preferredWindowId])
                {
                    target = window;
                    break;
                }
                // In the absence of a title match, prefer a smaller
                // secondary editor window over the DAW's large main window.
                const CGFloat area = window.frame.size.width * window.frame.size.height;
                if (area > bestArea && area < 1800.0 * 1400.0)
                {
                    bestArea = area;
                    target = window;
                }
            }
            if (target == nil)
            {
                error = "Aucune fenetre de plugin identifiable. La capture n'a pas ete lancee.";
                return false;
            }
            SCContentFilter* filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:target];
            SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
            config.width = (size_t) juce::jlimit(80, 4096, (int) target.frame.size.width);
            config.height = (size_t) juce::jlimit(60, 4096, (int) target.frame.size.height);
            config.showsCursor = NO;

            __block CGImageRef captured = nullptr;
            __block NSError* captureError = nil;
            dispatch_semaphore_t done = dispatch_semaphore_create(0);
            [SCScreenshotManager captureImageWithFilter:filter configuration:config
                completionHandler:^(CGImageRef image, NSError* e) {
                    if (image != nullptr) captured = CGImageRetain(image);
                    captureError = e;
                    dispatch_semaphore_signal(done);
                }];
            if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC)) != 0)
            {
                error = "Delai depasse pendant la capture ScreenCaptureKit.";
                return false;
            }
            if (captured == nullptr)
            {
                error = "ScreenCaptureKit : " + juce::String(captureError.localizedDescription.UTF8String ?: "image indisponible");
                return false;
            }
            NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc] initWithCGImage:captured];
            CGImageRelease(captured);
            NSData* png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
            if (png == nil || png.length == 0 || png.length > 5 * 1024 * 1024)
            {
                error = "Image PNG invalide ou trop volumineuse.";
                return false;
            }
            if (![png writeToFile:[NSString stringWithUTF8String:output.getFullPathName().toRawUTF8()] atomically:YES])
            {
                error = "Impossible d'enregistrer la miniature.";
                return false;
            }
            return true;
        }
    }
    error = "La capture native necessite macOS 14 ou une version ulterieure.";
    return false;
}
