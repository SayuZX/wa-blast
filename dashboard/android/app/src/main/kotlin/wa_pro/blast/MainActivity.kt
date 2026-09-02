package wa_pro.blast

import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.provider.Settings
import android.view.accessibility.AccessibilityManager
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel
import java.io.BufferedReader
import java.io.File
import java.io.InputStreamReader

/**
 * MainActivity — exposes a platform channel (`wa_pro.blast/root`) to Dart for:
 *   - checkRoot()   : is the device rooted / can we elevate via `su`?
 *   - execAsRoot(cmd): run a shell command (via `su` if available, else `/system/bin/sh`).
 *
 * If the device is NOT rooted, commands run WITHOUT elevation (simulation
 * mode) instead of throwing "Cannot run program su".
 */
class MainActivity : FlutterActivity() {

    companion object {
        private const val CHANNEL = "wa_pro.blast/root"
    }

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)

        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, CHANNEL)
            .setMethodCallHandler { call, result ->
                when (call.method) {
                    "checkRoot" -> result.success(checkRoot())
                    "sendViaAccessibility" -> {
                        val number = call.argument<String>("number") ?: ""
                        val message = call.argument<String>("message") ?: ""
                        result.success(sendViaAccessibility(number, message))
                    }
                    "execAsRoot" -> {
                        val cmd = call.argument<String>("cmd") ?: ""
                        execShell(cmd) { code, out, err ->
                            result.success(
                                mapOf(
                                    "exitCode" to code,
                                    "stdout" to out,
                                    "stderr" to err
                                )
                            )
                        }
                    }
                    else -> result.notImplemented()
                }
            }
    }

    private fun suAvailable(): Boolean {
        for (p in listOf("/system/bin/su", "/system/xbin/su", "/sbin/su", "/su/bin/su")) {
            if (File(p).exists()) return true
        }
        return false
    }

    private fun checkRoot(): Boolean {
        return try {
            if (!suAvailable()) return false
            val p = Runtime.getRuntime().exec(arrayOf("su", "-c", "id"))
            val code = p.waitFor()
            code == 0
        } catch (e: Exception) {
            false
        }
    }

    /**
     * Run a command. Uses `su -c` if rooted, else `/system/bin/sh -c` (no root).
     * Never throws to the caller — errors are returned via exitCode/stderr.
     */
    private fun execShell(cmd: String, cb: (Int, String, String) -> Unit) {
        Thread {
            try {
                val argv = if (suAvailable())
                    arrayOf("su", "-c", cmd)
                else
                    arrayOf("/system/bin/sh", "-c", cmd)
                val p = Runtime.getRuntime().exec(argv)
                val out = BufferedReader(InputStreamReader(p.inputStream)).readText()
                val err = BufferedReader(InputStreamReader(p.errorStream)).readText()
                val code = p.waitFor()
                cb(code, out, err)
            } catch (e: Exception) {
                cb(-1, "", e.message ?: "exec failed")
            }
        }.start()
    }

    private fun isAccessibilityEnabled(): Boolean {
        val am = getSystemService(Context.ACCESSIBILITY_SERVICE) as AccessibilityManager
        val services = am.getEnabledAccessibilityServiceList(
            android.accessibilityservice.AccessibilityServiceInfo.FEEDBACK_ALL_MASK
        )
        for (s in services) {
            if (s.resolveInfo.serviceInfo.packageName == packageName) return true
        }
        return false
    }

    /**
     * Send via the accessibility service (non-root automation).
     * If the service is not enabled, opens the accessibility settings screen.
     * Returns a status string.
     */
    private fun sendViaAccessibility(number: String, message: String): String {
        if (!isAccessibilityEnabled()) {
            val intent = Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS)
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            startActivity(intent)
            return "accessibility_not_enabled"
        }
        WaAutomationService.pendingNumber = number
        WaAutomationService.pendingMessage = message
        // Open WhatsApp (trigger onAccessibilityEvent).
        try {
            val intent = Intent(Intent.ACTION_VIEW, android.net.Uri.parse("https://wa.me/$number"))
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            startActivity(intent)
        } catch (e: Exception) {
            return "whatsapp_not_installed"
        }
        return "queued"
    }
}
