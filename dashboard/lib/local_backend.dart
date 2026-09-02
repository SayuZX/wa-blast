import 'dart:convert';

import 'root_service.dart';

/// Local backend — talks to the bundled `wa_apid` C++ binary via the platform
/// channel. When [simulate] is true (non-root mode), all operations run
/// against an in-memory store instead of the binary, so the dashboard is
/// fully usable on a non-rooted device/emulator for QA.
class LocalBackend {
  LocalBackend() {
    _simulate = true;  // default safe on emulator
  }

  bool _simulate = true;
  bool get simulate => _simulate;
  void setSimulate(bool v) => _simulate = v;

  // In-memory simulation store.
  final Map<String, Map<String, dynamic>> _profiles = {};
  final List<Map<String, dynamic>> _logs = [];
  String _active = '';

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

  void _simLog(String status, String msg, {String profile = '', String target = ''}) {
    _logs.insert(0, {
      'timestamp': DateTime.now().toIso8601String(),
      'profile': profile,
      'target': target,
      'status': status,
      'message': msg,
    });
  }

  // ---- status ----
  Future<Map<String, dynamic>> status() async {
    if (!_simulate) return _run('status');
    return {
      'active_profile': _active,
      'profiles': _profiles.keys.toList(),
      'mode': 'simulation',
    };
  }

  Future<Map<String, dynamic>> listProfiles() => status();

  // ---- profiles ----
  Future<Map<String, dynamic>> switchProfile(String name) async {
    if (!_simulate) return _run('switch $name');
    _active = name;
    _simLog('SUCCESS', 'Switched to $name', profile: name);
    return {'active': name};
  }

  Future<Map<String, dynamic>> createProfile(String name) async {
    if (!_simulate) return _run('profile add $name');
    _profiles[name] = {
      'name': name,
      'status': 'inactive',
      'created_at': DateTime.now().toIso8601String(),
    };
    _simLog('SUCCESS', 'Created profile $name', profile: name);
    return {'created': name};
  }

  Future<Map<String, dynamic>> restoreProfile(String name) async {
    if (!_simulate) return _run('profile restore $name');
    _simLog('SUCCESS', 'Restored $name', profile: name);
    return {'restored': name};
  }

  Future<Map<String, dynamic>> deleteProfile(String name) async {
    if (!_simulate) return _run('profile delete $name');
    _profiles.remove(name);
    if (_active == name) _active = '';
    _simLog('SUCCESS', 'Deleted $name', profile: name);
    return {'deleted': name};
  }

  Future<Map<String, dynamic>> exportProfile(String name) async {
    if (!_simulate) return _run('profile export $name');
    _simLog('SUCCESS', 'Exported $name', profile: name);
    return {'exported': name};
  }

  Future<Map<String, dynamic>> editProfile(String name, Map<String, dynamic> fields) async {
    if (!_simulate) return _run('profile edit $name ${jsonEncode(fields)}');
    final newName = fields['name'] as String? ?? name;
    if (newName != name && _profiles.containsKey(name)) {
      _profiles[newName] = _profiles.remove(name)!;
      _profiles[newName]!['name'] = newName;
      if (_active == name) _active = newName;
    }
    _simLog('SUCCESS', 'Renamed $name -> $newName', profile: name);
    return {'edited': name, 'new_name': newName};
  }

  // ---- messages ----
  Future<Map<String, dynamic>> sendMessage(String number, String message) async {
    if (_simulate) {
      _simLog('SUCCESS', '[SIMULASI] Mengirim ke $number : $message',
          profile: _active, target: number);
      return {'phone': number, 'status': 'SUCCESS'};
    }
    // Real automation. Prefer the binary (root); if it fails, try accessibility.
    try {
      return await _run('send $number ${_shellQuote(message)}');
    } catch (_) {
      final acc = await RootService.sendViaAccessibility(number, message);
      if (acc == 'queued') {
        _simLog('SUCCESS', 'Via accessibility: $number', profile: _active, target: number);
        return {'phone': number, 'status': 'SUCCESS'};
      }
      throw LocalBackendException('automation failed ($acc)');
    }
  }

  Future<Map<String, dynamic>> blast(List<String> targets, String message) async {
    if (!_simulate) return _run('blast ${_shellQuote(jsonEncode(targets))} ${_shellQuote(message)}');
    int ok = 0;
    for (final t in targets) {
      _simLog('SUCCESS', '[SIMULASI] Mengirim ke $t : $message',
          profile: _active, target: t);
      ok++;
    }
    return {'total': targets.length, 'succeeded': ok, 'failed': 0};
  }

  Future<Map<String, dynamic>> logs() async {
    if (!_simulate) return _run('logs');
    return {'logs': _logs};
  }

  static String _shellQuote(String s) => "'${s.replaceAll("'", "'\\''")}'";
}

class LocalBackendException implements Exception {
  LocalBackendException(this.message);
  final String message;
  @override
  String toString() => message;
}
