package wa_pro.blast

import android.accessibilityservice.AccessibilityService
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.view.accessibility.AccessibilityEvent
import android.view.accessibility.AccessibilityNodeInfo
import io.flutter.plugin.common.MethodChannel

/**
 * WhatsApp Automation Service — drives WhatsApp via accessibility (non-root).
 *
 * Used as a fallback when the device is NOT rooted. It can:
 *   - open a chat via `wa.me/<number>` deep link
 *   - find the message input field and type text
 *   - press the send button
 *
 * The user must enable this service manually in Settings → Accessibility.
 */
class WaAutomationService : AccessibilityService() {

    companion object {
        var instance: WaAutomationService? = null
        private const val WA_PACKAGE = "com.whatsapp"

        // Simple static command queue (set from the platform channel).
        @Volatile var pendingNumber: String? = null
        @Volatile var pendingMessage: String? = null
    }

    override fun onServiceConnected() {
        super.onServiceConnected()
        instance = this
    }

    override fun onDestroy() {
        instance = null
        super.onDestroy()
    }

    override fun onAccessibilityEvent(event: AccessibilityEvent?) {
        // React to window changes to detect when WhatsApp is in the foreground.
        val number = pendingNumber ?: return
        val message = pendingMessage ?: return
        if (event?.packageName != WA_PACKAGE) return

        // Only act once per pending command.
        pendingNumber = null
        pendingMessage = null
        openChatAndSend(number, message)
    }

    override fun onInterrupt() {}

    /** Open a chat via deep link, then wait and type + send. */
    private fun openChatAndSend(number: String, message: String) {
        val intent = Intent(Intent.ACTION_VIEW, android.net.Uri.parse("https://wa.me/$number"))
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        try {
            startActivity(intent)
        } catch (e: Exception) {
            // WhatsApp may not be installed.
            return
        }

        // Poll for the input field, then type + send.
        Thread {
            val root = findInputField(maxTries = 30, delayMs = 500)
            if (root != null) {
                typeText(root, message)
                Thread.sleep(400)
                pressSend()
            }
        }.start()
    }

    private fun findInputField(maxTries: Int, delayMs: Long): AccessibilityNodeInfo? {
        for (i in 0 until maxTries) {
            val root = rootInActiveWindow ?: return null
            val input = findNodeByClass(root, "android.widget.EditText")
            if (input != null) return input
            Thread.sleep(delayMs)
        }
        return null
    }

    private fun findNodeByClass(node: AccessibilityNodeInfo, className: String): AccessibilityNodeInfo? {
        if (node.className?.toString() == className) return node
        for (i in 0 until node.childCount) {
            val child = node.getChild(i) ?: continue
            val found = findNodeByClass(child, className)
            if (found != null) return found
        }
        return null
    }

    private fun typeText(input: AccessibilityNodeInfo, text: String) {
        val clip = ClipData.newPlainText("wa", text)
        val cm = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        cm.setPrimaryClip(clip)
        // Paste via accessibility ACTION_PASTE, then fallback to ACTION_SET_TEXT.
        input.performAction(AccessibilityNodeInfo.ACTION_FOCUS)
        if (!input.performAction(AccessibilityNodeInfo.ACTION_PASTE)) {
            val args = Bundle()
            args.putCharSequence(AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE, text)
            input.performAction(AccessibilityNodeInfo.ACTION_SET_TEXT, args)
        }
    }

    private fun pressSend() {
        val root = rootInActiveWindow ?: return
        // WhatsApp send button content-desc is usually "Send"/"Kirim".
        val send = findNodeByDescOrText(root, listOf("Send", "Kirim"))
        send?.performAction(AccessibilityNodeInfo.ACTION_CLICK)
    }

    private fun findNodeByDescOrText(node: AccessibilityNodeInfo, needles: List<String>): AccessibilityNodeInfo? {
        val desc = node.contentDescription?.toString() ?: ""
        val text = node.text?.toString() ?: ""
        for (n in needles) {
            if (desc.equals(n, ignoreCase = true) || text.equals(n, ignoreCase = true)) return node
        }
        for (i in 0 until node.childCount) {
            val child = node.getChild(i) ?: continue
            val found = findNodeByDescOrText(child, needles)
            if (found != null) return found
        }
        return null
    }
}
