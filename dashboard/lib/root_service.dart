import 'dart:convert';
import 'dart:io';

import 'package:flutter/services.dart';

/// Talks to the native Kotlin side (`wa_pro.blast/root`) for root checks and
/// running shell commands via `su -c`.
class RootService {
  static const _channel = MethodChannel('wa_pro.blast/root');

  /// Device path where the binary + config are installed.
  static const workDir = '/data/local/tmp/wa-cli';
  static const binaryPath = '$workDir/wa_apid';
  static const configPath = '$workDir/config.json';

  /// True if the device is rooted and `su` is available.
  static Future<bool> checkRoot() async {
    try {
      final result = await _channel.invokeMethod<bool>('checkRoot');
      return result ?? false;
    } on MissingPluginException {
      // Running on a platform without the channel (web/desktop).
      return false;
    } catch (_) {
      return false;
    }
  }

  /// Run a command as root. Returns a map {exitCode, stdout, stderr}.
  static Future<Map<String, dynamic>> execAsRoot(String cmd) async {
    try {
      final result = await _channel.invokeMethod<Map<dynamic, dynamic>>(
        'execAsRoot',
        {'cmd': cmd},
      );
      if (result == null) {
        return {'exitCode': -1, 'stdout': '', 'stderr': 'no response'};
      }
      return {
        'exitCode': result['exitCode'] ?? -1,
        'stdout': result['stdout'] ?? '',
        'stderr': result['stderr'] ?? '',
      };
    } catch (e) {
      return {'exitCode': -1, 'stdout': '', 'stderr': e.toString()};
    }
  }

  /// Install the bundled `wa_apid` binary + config from Flutter assets to
  /// /data/local/tmp/wa-cli and chmod +x. Returns the binary path on success.
  ///
  /// The asset bytes are base64-encoded and piped through `su -c` to write
  /// the file on-device (assets can't be written directly without root).
  static Future<String?> installBinary() async {
    final mkdir = await execAsRoot('mkdir -p $workDir');
    if ((mkdir['exitCode'] as int? ?? -1) != 0) return null;

    // Extract binary asset -> base64 -> su write.
    try {
      final binData = await rootBundle.load('assets/wa_apid');
      final b64 = base64Encode(binData.buffer.asUint8List());
      final writeBin = await execAsRoot(
        "echo $b64 | base64 -d > $binaryPath && chmod +x $binaryPath",
      );
      if ((writeBin['exitCode'] as int? ?? -1) != 0) return null;
    } catch (_) {
      // Binary asset missing (not yet built) — tolerate for dev.
    }

    // Extract config asset.
    try {
      final cfgData = await rootBundle.load('assets/config.json');
      final cfgB64 = base64Encode(cfgData.buffer.asUint8List());
      await execAsRoot("echo $cfgB64 | base64 -d > $configPath");
    } catch (_) {
      // config optional.
    }

    // Verify binary is executable.
    final test = await execAsRoot('test -x $binaryPath && echo ok');
    if ((test['stdout'] as String? ?? '').contains('ok')) {
      return binaryPath;
    }
    return null;
  }

  /// Run a wa-cli command (e.g. "status", "server start").
  static Future<Map<String, dynamic>> runCli(String args) async {
    return execAsRoot('$binaryPath $args');
  }

  /// Send via the Accessibility Service (non-root automation).
  /// Returns the status string: "queued", "accessibility_not_enabled",
  /// "whatsapp_not_installed".
  static Future<String> sendViaAccessibility(String number, String message) async {
    try {
      final result = await _channel.invokeMethod<String>(
        'sendViaAccessibility',
        {'number': number, 'message': message},
      );
      return result ?? 'error';
    } on MissingPluginException {
      return 'not_supported';
    } catch (e) {
      return 'error: $e';
    }
  }

  // Fallback for non-rooted platforms: adb via local process (host dev).
  static Future<Map<String, dynamic>> runAdbHost(String args) async {
    try {
      final p = await Process.run('adb', args.split(' '));
      return {
        'exitCode': p.exitCode,
        'stdout': p.stdout.toString(),
        'stderr': p.stderr.toString(),
      };
    } catch (e) {
      return {'exitCode': -1, 'stdout': '', 'stderr': e.toString()};
    }
  }
}
