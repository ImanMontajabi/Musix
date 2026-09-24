#include "releasenotifier.h"
#include <QMetaObject>
#include <QPointer>
#import <UserNotifications/UserNotifications.h>

// Shows a notification even while Musix is the app in front, which is when an
// automatic check is most likely to finish.
@interface MusixNotificationPresenter : NSObject <UNUserNotificationCenterDelegate>
@end
@implementation MusixNotificationPresenter
- (void)userNotificationCenter:(UNUserNotificationCenter *)center
       willPresentNotification:(UNNotification *)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler {
  Q_UNUSED(center); Q_UNUSED(notification);
  completionHandler(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList);
}
@end

namespace {
UNUserNotificationCenter *center() {
  static MusixNotificationPresenter *presenter = [[MusixNotificationPresenter alloc] init];
  UNUserNotificationCenter *c = UNUserNotificationCenter.currentNotificationCenter;
  c.delegate = presenter;
  return c;
}
} // namespace

void ReleaseNotifier::requestPermission() {
  QPointer<ReleaseNotifier> self(this);
  [center() requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                          completionHandler:^(BOOL granted, NSError *error) {
                            const QString reason = error ? QString::fromNSString(error.localizedDescription) : QString();
                            QMetaObject::invokeMethod(self, [self, granted, reason] {
                              if (self) emit self->permissionAnswered(granted, reason);
                            }, Qt::QueuedConnection);
                          }];
}

void ReleaseNotifier::notify(const QString &title, const QString &body) {
  UNMutableNotificationContent *content = [[UNMutableNotificationContent alloc] init];
  content.title = title.toNSString();
  content.body = body.toNSString();
  // One at a time: a newer summary replaces an older one rather than piling up.
  UNNotificationRequest *request = [UNNotificationRequest requestWithIdentifier:@"musix.new-releases" content:content trigger:nil];
  [center() addNotificationRequest:request withCompletionHandler:nil];
}
