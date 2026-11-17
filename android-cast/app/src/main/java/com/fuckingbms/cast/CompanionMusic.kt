package com.fuckingbms.cast

import android.content.ComponentName
import android.content.Context
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.os.SystemClock
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification

internal object CompanionMusic {
    data class State(val title: String = "", val artist: String = "", val line: String = "", val playing: Boolean = false,
                     val packageName: String = "", val positionMs: Long = 0)
    private val notices = mutableMapOf<String, Pair<String,String>>()
    private var controller: MediaController? = null
    private var localTitle = ""
    private var localArtist = ""
    private var cachedRaw = ""
    private var cachedLines = emptyList<Lyrics.Line>()
    private var localLines = emptyList<Lyrics.Line>()
    @Volatile var state = State(); private set

    fun importLyrics(title: String, text: String) {
        localTitle = title
        localArtist = state.artist
        localLines = Lyrics.parse(text)
    }

    fun refresh(context: Context): State {
        val sessions = runCatching {
            (context.getSystemService(Context.MEDIA_SESSION_SERVICE) as MediaSessionManager)
                .getActiveSessions(ComponentName(context, CompanionNotificationListener::class.java))
        }.getOrDefault(emptyList())
        val chosen = sessions.firstOrNull { it.playbackState?.state == PlaybackState.STATE_PLAYING }
            ?: sessions.firstOrNull { it.sessionToken == controller?.sessionToken } ?: sessions.firstOrNull()
        controller = chosen
        if (chosen == null) return State().also { state = it }
        val meta = chosen.metadata
        val playback = chosen.playbackState
        val title = meta?.getString(MediaMetadata.METADATA_KEY_TITLE).orEmpty().take(200)
        val artist = meta?.getString(MediaMetadata.METADATA_KEY_ARTIST).orEmpty().take(200)
        val playing = playback?.state == PlaybackState.STATE_PLAYING
        val elapsed = if (playing && playback != null && playback.lastPositionUpdateTime > 0)
            ((SystemClock.elapsedRealtime() - playback.lastPositionUpdateTime) * playback.playbackSpeed).toLong() else 0L
        val position = ((playback?.position ?: 0) + elapsed).coerceAtLeast(0)
        val raw = listOf("android.media.metadata.LYRIC", "android.media.metadata.LYRICS", "lyrics", "lyric")
            .firstNotNullOfOrNull { key -> runCatching { meta?.getString(key) }.getOrNull()?.takeIf { it.isNotBlank() } }.orEmpty()
        if (raw != cachedRaw) { cachedRaw=raw; cachedLines=Lyrics.parse(raw) }
        val lines = if (title.isNotBlank() && title == localTitle && artist == localArtist) localLines else cachedLines
        val line = if (lines.isNotEmpty()) Lyrics.current(lines, position)
            else raw.takeIf { it.isNotBlank() && !it.contains('[') }?.take(200) ?: synchronized(notices) { notices[chosen.packageName]?.takeIf { it.first == title }?.second.orEmpty() }
        return State(title, artist, line, playing, chosen.packageName, position).also { state = it }
    }

    fun control(action: String) {
        val controls = controller?.transportControls ?: return
        when (action) {
            "previous" -> controls.skipToPrevious()
            "next" -> controls.skipToNext()
            "toggle" -> if (state.playing) controls.pause() else controls.play()
        }
    }

    fun notice(notification: StatusBarNotification, removed: Boolean = false) {
        // Only explicit lyric fields are used; ordinary notification text often contains the artist.
        val extras = notification.notification.extras
        val text = listOf("android.lyrics", "lyrics", "lyric", "android.media.metadata.LYRICS")
            .firstNotNullOfOrNull { extras.getCharSequence(it)?.toString()?.takeIf(String::isNotBlank) }
        synchronized(notices) {
            if (removed) notices.remove(notification.packageName)
            else if (text != null) notices[notification.packageName] = extras.getCharSequence(android.app.Notification.EXTRA_TITLE)?.toString().orEmpty() to text.take(200)
            else notices.remove(notification.packageName)
        }
    }
}

class CompanionNotificationListener : NotificationListenerService() {
    override fun onNotificationPosted(sbn: StatusBarNotification) { CompanionMusic.notice(sbn) }
    override fun onNotificationRemoved(sbn: StatusBarNotification) { CompanionMusic.notice(sbn, true) }
}
