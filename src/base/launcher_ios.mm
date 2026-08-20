// LibreSprite
// Copyright (C) 2026  LibreSprite contributors
//
// This file is released under the terms of the MIT license.
// Read LICENSE.txt for more information.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "base/launcher.h"

#import <UIKit/UIKit.h>

namespace base {
namespace launcher {

// iOS has no std::system(), and an app may only ask the system to hand a URL
// over to whichever app is registered for it. The call is asynchronous and
// reports nothing back synchronously, so we can only report that the request
// was handed off, not that it succeeded.
bool ios_open_url(const std::string& url)
{
  NSString* str = [NSString stringWithUTF8String:url.c_str()];
  if (!str)
    return false;

  NSURL* nsurl = [NSURL URLWithString:str];
  if (!nsurl)
    return false;

  UIApplication* app = [UIApplication sharedApplication];
  if (![app canOpenURL:nsurl])
    return false;

  [app openURL:nsurl options:@{} completionHandler:nil];
  return true;
}

} // namespace launcher
} // namespace base
