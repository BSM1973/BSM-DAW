#include "NativePluginCapture.h"
#import <AppKit/AppKit.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreFoundation/CoreFoundation.h>
#include <unistd.h>

bool libertyHasScreenCapturePermission()
{
    return CGPreflightScreenCaptureAccess();
}

bool libertyRequestScreenCapturePermission()
{
    return CGRequestScreenCaptureAccess();
}

// ScreenCaptureKit completion handlers may require the main run loop to turn.
// This helper is called only inside Liberty's disposable artwork subprocess.
// Blocking its JUCE message thread on a semaphore prevents those callbacks
// from completing on some macOS versions.
static bool libertyWaitForScreenshot(dispatch_semaphore_t semaphore, double seconds)
{
    if (![NSThread isMainThread])
        return dispatch_semaphore_wait(semaphore,
            dispatch_time(DISPATCH_TIME_NOW, (int64_t)(seconds * NSEC_PER_SEC))) == 0;

    const CFAbsoluteTime until = CFAbsoluteTimeGetCurrent() + seconds;
    while (CFAbsoluteTimeGetCurrent() < until)
    {
        if (dispatch_semaphore_wait(semaphore, DISPATCH_TIME_NOW) == 0) return true;
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.03, true);
    }
    return dispatch_semaphore_wait(semaphore, DISPATCH_TIME_NOW) == 0;
}

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
            if (!libertyWaitForScreenshot(discovery, 8.0))
            {
                error = "Delai depasse lors de la recherche des fenetres macOS.";
                return false;
            }
            if (shareable == nil)
            {
                error = "ScreenCaptureKit : " + juce::String(discoveryError.localizedDescription.UTF8String ?: "fenetres indisponibles");
                return false;
            }

            // Never call AppKit window APIs from this capture worker.
            // In particular, [NSApp orderedWindows] on a background thread
            // caused an EXC_BAD_ACCESS in the macOS crash report.
            // ScreenCaptureKit already provides process-owned window IDs.
            NSString* wanted = [NSString stringWithUTF8String:pluginName.toRawUTF8()];
            SCWindow* target = nil;
            CGFloat bestScore = -1.0;
            for (SCWindow* window in shareable.windows)
            {
                if (window.owningApplication.processID != getpid()) continue;
                const CGFloat width = window.frame.size.width;
                const CGFloat height = window.frame.size.height;
                if (width < 80 || height < 60 || width > 4096 || height > 4096) continue;
                const BOOL titleMatches = wanted.length > 0 && window.title.length > 0
                    && [window.title localizedCaseInsensitiveContainsString:wanted];
                // Avoid selecting the DAW's large main window when possible.
                const CGFloat area = width * height;
                const CGFloat score = (titleMatches ? 100000000.0 : 0.0)
                    + ((area <= 1800.0 * 1400.0) ? area : -area);
                if (score > bestScore)
                {
                    bestScore = score;
                    target = window;
                }
            }
            if (target == nil)
            {
                error = "Aucune fenetre macOS du plugin disponible pour la capture.";
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
            if (!libertyWaitForScreenshot(done, 10.0))
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
