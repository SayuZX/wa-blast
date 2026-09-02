import 'dart:convert';

import 'root_service.dart';

/// Local backend — talks to the bundled `wa_apid` C++ binary directly via the
/// Kotlin platform channel (`su -c wa_apid ...`), instead of HTTP.
///
/// Since the binary ships inside the APK and runs on-device, there is no
/// separate HTTP server / URL / API key to configure.
class LocalBackend {
  static const _binary = RootService.binaryPath;

  /// Run a CLI command and parse the JSON stdout. Returns the parsed map, or
  /// throws [LocalBackendException] on failure.
  Future<Map<String, dynamic>> _run(String args) async {
    final result = await RootService.execAsRoot('$_binary $args');
    final exitCode = result['exitCode'] as int? ?? -1;
    final stdout = (result['stdout'] as String? ?? '').trim();
    final stderr = (result['stderr'] as String? ?? '').trim();

    if (exitCode != 0) {
      throw LocalBackendException(stderr.isNotEmpty ? stderr : 'command failed (exit $exitCode)');
    }
    if (stdout.isEmpty) return {};
    // The binary may print non-JSON (log lines); find the first JSON object.
    try {
      return jsonDecode(stdout) as Map<String, dynamic>;
    } catch (_) {
      // Try to extract a JSON object/array from mixed output.
      final start = stdout.indexOf('{');
      final end = stdout.lastIndexOf('}');
      if (start >= 0 && end > start) {
        try {
          return jsonDecode(stdout.substring(start, end + 1)) as Map<String, dynamic>;
        } catch (_) {}
      }
      return {'raw': stdout};
    }
  }

  // ---- status / health ----
  Future<Map<String, dynamic>> status() => _run('status');

  // ---- profiles ----
  Future<Map<String, dynamic>> listProfiles() => _run('status');

  Future<Map<String, dynamic>> switchProfile(String name) =>
      _run('switch $name');

  Future<Map<String, dynamic>> createProfile(String name) =>
      _run('profile add $name');

  Future<Map<String, dynamic>> restoreProfile(String name) =>
      _run('profile restore $name');

  Future<Map<String, dynamic>> deleteProfile(String name) =>
      _run('profile delete $name');

  Future<Map<String, dynamic>> exportProfile(String name) =>
      _run('profile export $name');

  Future<Map<String, dynamic>> editProfile(String name, Map<String, dynamic> fields) =>
      _run('profile edit $name ${jsonEncode(fields)}');

  // ---- messages ----
  Future<Map<String, dynamic>> sendMessage(String number, String message) =>
      _run('send $number ${_shellQuote(message)}');

  Future<Map<String, dynamic>> blast(List<String> targets, String message) =>
      _run('blast ${_shellQuote(jsonEncode(targets))} ${_shellQuote(message)}');

  Future<Map<String, dynamic>> logs() => _run('logs');

  static String _shellQuote(String s) => "'${s.replaceAll("'", "'\\''")}'";
}

class LocalBackendException implements Exception {
  LocalBackendException(this.message);
  final String message;
  @override
  String toString() => message;
}
