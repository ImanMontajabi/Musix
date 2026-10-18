// What macOS itself reports as the Now Playing client and its info, read through
// MediaRemote (private) from a library loaded into /usr/bin/perl: the system
// answers an Apple-signed process, and nothing else, since macOS 15.4.
// Built and run by scripts/nowplaying-probe.sh. It sends a command only when
// MR_SEND is set (0 play, 1 pause, 2 toggle, 4 next, 5 previous) and the client's
// description contains MR_CLIENT, so it can never reach another app's player.
#import <Foundation/Foundation.h>
#import <dlfcn.h>
#import <stdlib.h>
typedef void (^Handler)(NSDictionary *);
void probe_main(void){ @autoreleasepool {
  void *h = dlopen("/System/Library/PrivateFrameworks/MediaRemote.framework/MediaRemote", RTLD_NOW);
  void (*getInfo)(dispatch_queue_t, Handler) = dlsym(h,"MRMediaRemoteGetNowPlayingInfo");
  void (*getApp)(dispatch_queue_t, void(^)(id)) = dlsym(h,"MRMediaRemoteGetNowPlayingClient");
  getInfo(dispatch_get_main_queue(), ^(NSDictionary *i){ NSLog(@"info: %@", i); });
  getApp(dispatch_get_main_queue(), ^(id c){ NSLog(@"client: %@", c); });
  const char *send = getenv("MR_SEND"), *only = getenv("MR_CLIENT");
  if (send && only) {
    void (*sendCommand)(int, id) = dlsym(h, "MRMediaRemoteSendCommand");
    getApp(dispatch_get_main_queue(), ^(id c) {
      if ([[NSString stringWithFormat:@"%@", c] containsString:@(only)]) { sendCommand(atoi(send), nil); NSLog(@"sent %s to %@", send, c); }
      else NSLog(@"not sent: client is %@", c);
    });
  }
  [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:3]];
}}
