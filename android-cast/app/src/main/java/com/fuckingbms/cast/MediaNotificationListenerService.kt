package com.fuckingbms.cast

import android.content.Intent
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification

class MediaNotificationListenerService : NotificationListenerService() {
    override fun onListenerConnected() = notifyMediaSessionsChanged()

    override fun onNotificationPosted(notification: StatusBarNotification) = notifyMediaSessionsChanged()

    override fun onNotificationRemoved(notification: StatusBarNotification) = notifyMediaSessionsChanged()

    private fun notifyMediaSessionsChanged() {
        sendBroadcast(Intent(MediaControlService.ACTION_MEDIA_SESSIONS_CHANGED).setPackage(packageName))
    }
}
