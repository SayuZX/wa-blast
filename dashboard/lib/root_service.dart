import 'dart:io';

import 'package:flutter/services.dart';

/// Talks to the native Kotlin side (`wa_pro.blast/root`) for root checks and
/// running shell commands via `su -c`.
class RootService {
  static const _channel = MethodChannel('wa_pro.blast/root');

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

  /// Install the bundled `wa_apid` binary to /data/local/tmp and chmod +x.
  /// Returns the device path on success, or null on failure.
  static Future<String?> installBinary() async {
    const workDir = '/data/local/tmp/wa-cli';
    final mkdir = await execAsRoot('mkdir -p $workDir');
    if ((mkdir['exitCode'] as int? ?? -1) != 0) return null;
    // The binary is bundled as a Flutter asset; we can't write it directly
    // from Dart to /data/local/tmp without root, so we base64-stream it.
    // For simplicity, the deploy step pushes the binary via adb. This method
    // is a placeholder that verifies the directory exists.
    final test = await execAsRoot('test -x $workDir/wa_apid && echo ok');
    if ((test['stdout'] as String? ?? '').contains('ok')) {
      return '$workDir/wa_apid';
    }
    return null;
  }

  /// Run a wa-cli command (e.g. "status", "server start").
  static Future<Map<String, dynamic>> runCli(String args) async {
    const binary = '/data/local/tmp/wa-cli/wa_apid';
    return execAsRoot('$binary $args');
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
