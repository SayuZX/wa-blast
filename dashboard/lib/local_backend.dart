import 'dart:convert';

import 'root_service.dart';

/// Local backend — all operations are REAL (no simulation).
///
/// - root mode (default on rooted device): run the bundled `wa_apid` C++
///   binary via `su` for profiles/messages/logs (ADB automation).
/// - non-root mode: message sending falls back to the Accessibility Service;
///   profiles/logs still run through the binary (which auto-detects and runs
///   in its own non-root path) — no fake/in-memory data anywhere.
class LocalBackend {
  LocalBackend() {
    _rootMode = true;
  }

  bool _rootMode = true;
  bool get rootMode => _rootMode;
  void setRootMode(bool v) => _rootMode = v;

  static const _binary = RootService.binaryPath;

  /// Run a CLI command against the real binary.
  Future<Map<String, dynamic>> _run(String args) async {
    final result = await RootService.execAsRoot('$_binary $args');
    final exitCode = result['exitCode'] as int? ?? -1;
    final stdout = (result['stdout'] as String? ?? '').trim();
    final stderr = (result['stderr'] as String? ?? '').trim();

    if (exitCode != 0) {
      throw LocalBackendException(stderr.isNotEmpty ? stderr : 'command failed (exit $exitCode)');
    }
    if (stdout.isEmpty) return {};
    try {
      return jsonDecode(stdout) as Map<String, dynamic>;
    } catch (_) {
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

  // ---- status / profiles (real, from binary) ----
  Future<Map<String, dynamic>> status() => _run('status');
  Future<Map<String, dynamic>> listProfiles() => _run('status');

  Future<Map<String, dynamic>> switchProfile(String name) => _run('switch $name');
  Future<Map<String, dynamic>> createProfile(String name) => _run('profile add $name');
  Future<Map<String, dynamic>> restoreProfile(String name) => _run('profile restore $name');
  Future<Map<String, dynamic>> deleteProfile(String name) => _run('profile delete $name');
  Future<Map<String, dynamic>> exportProfile(String name) => _run('profile export $name');
  Future<Map<String, dynamic>> editProfile(String name, Map<String, dynamic> fields) =>
      _run('profile edit $name ${jsonEncode(fields)}');

  // ---- messages (real) ----
  Future<Map<String, dynamic>> sendMessage(String number, String message) async {
    if (_rootMode) {
      // Root: ADB automation via binary.
      return _run('send $number ${_shellQuote(message)}');
    }
    // Non-root: Accessibility Service automation.
    final acc = await RootService.sendViaAccessibility(number, message);
    if (acc == 'queued') {
      return {'phone': number, 'status': 'SUCCESS', 'method': 'accessibility'};
    }
    throw LocalBackendException('send failed ($acc)');
  }

  Future<Map<String, dynamic>> blast(List<String> targets, String message) async {
    if (_rootMode) {
      return _run('blast ${_shellQuote(jsonEncode(targets))} ${_shellQuote(message)}');
    }
    // Non-root: iterate via accessibility.
    int ok = 0, fail = 0;
    for (final t in targets) {
      final acc = await RootService.sendViaAccessibility(t, message);
      if (acc == 'queued') {
        ok++;
      } else {
        fail++;
      }
      await Future.delayed(const Duration(seconds: 5));
    }
    return {'total': targets.length, 'succeeded': ok, 'failed': fail};
  }

  Future<Map<String, dynamic>> logs() => _run('logs');

  static String _shellQuote(String s) => "'${s.replaceAll("'", "'\\''")}'";
}

class LocalBackendException implements Exception {
  LocalBackendException(this.message);
  final String message;
  @override
  String toString() => message;
}
