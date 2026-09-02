# LSPosed Spoof Module (companion skeleton)

This is the **companion LSPosed module** for the QA harness. The backend writes
a per-profile identity config to the device (`QA_SPOOF_CONFIG_PATH`, default
`/data/local/tmp/qa_harness/spoof.json`) and this module reads it to override
`Build` / `TelephonyManager` / `Settings.Secure` values **for the target app
only**.

> ⚠️ **Do not** use this to impersonate a real device or person. The values are
> synthetic test data, used solely to observe how the target app reacts to
> environment variation in a lab.

## Contract

Config file (JSON):

```json
{
  "profile": "WA_2",
  "package": "com.whatsapp",
  "identity": {
    "imei": "356938035643809",
    "android_id": "a1b2c3d4e5f60718",
    "device_model": "QA-Device-1234",
    "manufacturer": "QALab",
    "serial": "...",
    "mac": "aa:bb:cc:dd:ee:ff"
  }
}
```

The module must:

1. Hook the relevant getters **only** for `package` in the config.
2. Load the JSON at process attach (Xposed `handleLoadPackage`) and cache it.
3. Re-load on broadcast `com.example.spoofmodule.RELOAD_SPOOF`.
4. Return the spoofed values for `IMEI`, `Android ID`, `Build.MODEL`,
   `Build.MANUFACTURER`, `Build.SERIAL`, and (optionally) MAC.

## Skeleton (Kotlin, LSPosed API)

```kotlin
// SpoofHook.kt — illustrate the shape; adapt to your exact target.
package com.example.spoofmodule

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import de.robv.android.xposed.IXposedHookLoadPackage
import de.robv.android.xposed.XC_MethodHook
import de.robv.android.xposed.XposedHelpers
import de.robv.android.xposed.callbacks.XC_LoadPackage.LoadPackageParam
import org.json.JSONObject
import java.io.File

class SpoofHook : IXposedHookLoadPackage {
    private var identity: JSONObject? = null
    private var targetPkg: String = "com.whatsapp"

    override fun handleLoadPackage(lpparam: LoadPackageParam) {
        // Only hook the target app.
        if (lpparam.packageName != targetPkg) return
        loadConfig()

        // Build.MODEL / MANUFACTURER / SERIAL
        listOf("MODEL", "MANUFACTURER", "SERIAL").forEach { field ->
            hookStatic(lpparam, "android.os.Build", field)
        }

        // TelephonyManager.getDeviceId (IMEI)
        XposedHelpers.findAndHookMethod(
            "android.telephony.TelephonyManager", lpparam.classLoader,
            "getDeviceId", object : XC_MethodHook() {
                override fun beforeHookedMethod(param: MethodHookParam) {
                    identity?.optString("imei")?.let { param.result = it }
                }
            })

        // Settings.Secure.getString (Android ID)
        XposedHelpers.findAndHookMethod(
            "android.provider.Settings\$Secure", lpparam.classLoader,
            "getString", Context::class.java, String::class.java,
            object : XC_MethodHook() {
                override fun beforeHookedMethod(param: MethodHookParam) {
                    if (param.args[1] == "android_id") {
                        identity?.optString("android_id")?.let { param.result = it }
                    }
                }
            })
    }

    private fun hookStatic(lpparam: LoadPackageParam, cls: String, field: String) {
        try {
            XposedHelpers.setStaticObjectField(
                XposedHelpers.findClass(cls, lpparam.classLoader),
                field, identity?.optString(field.lowercase()) ?: return
            )
        } catch (_: Throwable) {}
    }

    private fun loadConfig() {
        try {
            val f = File("/data/local/tmp/qa_harness/spoof.json")
            if (f.exists()) {
                val obj = JSONObject(f.readText())
                targetPkg = obj.optString("package", targetPkg)
                identity = obj.optJSONObject("identity")
            }
        } catch (_: Throwable) {}
    }
}
```

### Register the reload broadcast

```kotlin
class ReloadReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        // Re-read /data/local/tmp/qa_harness/spoof.json and update cache.
    }
}
```

Register in your module's `AndroidManifest.xml` with an intent filter for
`com.example.spoofmodule.RELOAD_SPOOF`.

## Building

This is a standard LSPosed module; use Android Studio with the LSPosed API
dependency. See LSPosed's official docs for the current `api` artifact.

> The exact hook points change across Android versions and app releases.
> Treat this skeleton as a starting point for your controlled environment.
