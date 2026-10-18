// What macOS itself reports as the Now Playing client and its info, read through
// MediaRemote (private) from a library loaded into /usr/bin/perl: the system
// answers an Apple-signed process, and nothing else, since macOS 15.4.
// Built and run by scripts/nowplaying-probe.sh; it never sends a command.
#import <Foundation/Foundation.h>
#import <dlfcn.h>
typedef void (^Handler)(NSDictionary *);
void probe_main(void){ @autoreleasepool {
  void *h = dlopen("/System/Library/PrivateFrameworks/MediaRemote.framework/MediaRemote", RTLD_NOW);
  void (*getInfo)(dispatch_queue_t, Handler) = dlsym(h,"MRMediaRemoteGetNowPlayingInfo");
  void (*getApp)(dispatch_queue_t, void(^)(id)) = dlsym(h,"MRMediaRemoteGetNowPlayingClient");
  getInfo(dispatch_get_main_queue(), ^(NSDictionary *i){ NSLog(@"info: %@", i); });
  getApp(dispatch_get_main_queue(), ^(id c){ NSLog(@"client: %@", c); });
  [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:3]];
}}
