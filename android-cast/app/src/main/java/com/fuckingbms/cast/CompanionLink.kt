package com.fuckingbms.cast

import android.content.Context

/** Activities and the background companion share one encrypted GATT session. */
internal object CompanionLink {
    private var session: DeviceBleSession? = null
    private val listeners = mutableMapOf<Any, (DeviceBleState, String?) -> Unit>()

    fun session(context: Context): DeviceBleSession {
        session?.let { return it }
        val app = context.applicationContext
        return DeviceBleSession(app) { state, detail ->
            if (state == DeviceBleState.CONNECTED) session?.connectedAddress?.let {
                app.getSharedPreferences("ble", Context.MODE_PRIVATE).edit().putString("last_mac", it).apply()
            }
            listeners.values.toList().forEach { it(state, detail) }
        }.also { session = it }
    }

    fun observe(owner: Any, context: Context, listener: (DeviceBleState, String?) -> Unit): DeviceBleSession {
        listeners[owner] = listener
        val current=session(context)
        if (current.connected) android.os.Handler(android.os.Looper.getMainLooper()).post {
            if (listeners[owner] === listener && current.connected) listener(DeviceBleState.CONNECTED,null)
        }
        return current
    }

    fun remove(owner: Any) { listeners.remove(owner) }
}
