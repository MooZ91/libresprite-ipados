// LibreSprite
// Copyright (C) 2026  LibreSprite contributors
//
// This file is released under the terms of the MIT license.
// Read LICENSE.txt for more information.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#import <UIKit/UIKit.h>

#include <string>

namespace app {

namespace {

// The share sheet has to be presented by whatever view controller is on
// screen. SDL owns the only one LibreSprite has, and iOS refuses to present
// from a controller that is already presenting something, so walk down to the
// topmost one.
UIViewController* top_view_controller()
{
  UIWindow* keyWindow = nil;
  for (UIScene* scene in [UIApplication sharedApplication].connectedScenes) {
    if (![scene isKindOfClass:[UIWindowScene class]])
      continue;
    for (UIWindow* window in ((UIWindowScene*)scene).windows) {
      if (window.isKeyWindow) {
        keyWindow = window;
        break;
      }
    }
    if (keyWindow)
      break;
  }
  if (!keyWindow)
    return nil;

  UIViewController* controller = keyWindow.rootViewController;
  while (controller.presentedViewController)
    controller = controller.presentedViewController;
  return controller;
}

} // anonymous namespace

// Hands a file to the iOS share sheet, which is the only way out of the
// sandbox: the user picks Files, Photos, AirDrop or another app from there.
// The sheet is presented asynchronously, so this only reports that it was
// handed over to UIKit, not what the user did with it.
bool ios_share_file(const std::string& path)
{
  NSString* nspath = [NSString stringWithUTF8String:path.c_str()];
  if (!nspath)
    return false;

  NSURL* url = [NSURL fileURLWithPath:nspath];
  if (!url)
    return false;

  // Presenting from inside the command that saved the file would fight with
  // the dialog that is still closing, and UIKit only talks to the main
  // thread. Both are solved by waiting for the next turn of the run loop,
  // which SDL pumps from LibreSprite's own loop.
  dispatch_async(dispatch_get_main_queue(), ^{
    UIViewController* presenter = top_view_controller();
    if (!presenter)
      return;

    UIActivityViewController* sheet =
      [[UIActivityViewController alloc] initWithActivityItems:@[url]
                                       applicationActivities:nil];

    // On an iPad the sheet is a popover, and UIKit raises an exception if it
    // has nothing to point at. LibreSprite draws its whole UI into one SDL
    // view, so there is no widget to anchor to: put it in the middle of the
    // screen and ask for no arrow (an empty direction set, not "unknown").
    UIPopoverPresentationController* popover = sheet.popoverPresentationController;
    if (popover) {
      UIView* view = presenter.view;
      popover.sourceView = view;
      popover.sourceRect = CGRectMake(CGRectGetMidX(view.bounds),
                                      CGRectGetMidY(view.bounds), 0, 0);
      popover.permittedArrowDirections = (UIPopoverArrowDirection)0;
    }

    [presenter presentViewController:sheet animated:YES completion:nil];
  });

  return true;
}

} // namespace app
