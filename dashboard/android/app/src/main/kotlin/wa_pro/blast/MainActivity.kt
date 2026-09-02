package wa_pro.blast

import android.os.Bundle
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel
import java.io.BufferedReader
import java.io.InputStreamReader

/**
 * MainActivity — exposes a platform channel (`wa_pro.blast/root`) to Dart for:
 *   - checkRoot()   : is the device rooted / can we elevate via `su`?
 *   - execAsRoot(cmd): run a shell command via `su -c`, return stdout/stderr/exit.
 *
 * Used by the dashboard's "root check" gate screen and to run the bundled
 * `wa_apid` native binary on the device.
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
                    "execAsRoot" -> {
                        val cmd = call.argument<String>("cmd") ?: ""
                        execAsRoot(cmd) { code, out, err ->
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

    private fun checkRoot(): Boolean {
        return try {
            val p = Runtime.getRuntime().exec(arrayOf("su", "-c", "id"))
            val code = p.waitFor()
            code == 0
        } catch (e: Exception) {
            false
        }
    }

    private fun execAsRoot(cmd: String, cb: (Int, String, String) -> Unit) {
        Thread {
            try {
                val p = Runtime.getRuntime().exec(arrayOf("su", "-c", cmd))
                val out = BufferedReader(InputStreamReader(p.inputStream)).readText()
                val err = BufferedReader(InputStreamReader(p.errorStream)).readText()
                val code = p.waitFor()
                cb(code, out, err)
            } catch (e: Exception) {
                cb(-1, "", e.message ?: "exec failed")
            }
        }.start()
    }
}
