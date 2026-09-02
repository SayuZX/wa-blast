import 'dart:async';

import 'package:flutter/foundation.dart';

import 'local_backend.dart';
import 'models.dart';

/// Application state (ChangeNotifier) shared across the dashboard.
///
/// Talks to the bundled C++ binary (`wa_apid`) via the platform channel —
/// no HTTP server / URL / API key needed (the binary is inside the APK).
class HarnessState extends ChangeNotifier {
  HarnessState({LocalBackend? backend}) : _backend = backend ?? LocalBackend();

  final LocalBackend _backend;

  bool _loading = false;
  bool _initialized = false;
  String _error = '';
  String _activeProfile = '';
  List<Profile> _profiles = [];
  List<LogEntry> _logs = [];
  bool _darkMode = false;

  // Settings toggles.
  bool _autoRefreshLogs = true;
  bool _preflightCheck = true;
  bool _clipboardFallback = true;
  bool _retryOnFail = true;
  bool _jsonLogging = true;
  bool _rootMode = true;  // true = root (binary ADB), false = non-root (accessibility)

  Timer? _logTimer;

  bool get loading => _loading;
  bool get initialized => _initialized;
  String get error => _error;
  String get activeProfile => _activeProfile;
  List<Profile> get profiles => _profiles;
  List<LogEntry> get logs => _logs;
  bool get darkMode => _darkMode;
  bool get autoRefreshLogs => _autoRefreshLogs;
  bool get preflightCheck => _preflightCheck;
  bool get clipboardFallback => _clipboardFallback;
  bool get retryOnFail => _retryOnFail;
  bool get jsonLogging => _jsonLogging;
  bool get rootMode => _rootMode;
  LocalBackend get backend => _backend;

  Future<void> init() async {
    _darkMode = true;
    _initialized = true;
    notifyListeners();
    await refresh();
    _startLogPolling();
  }

  void setDarkMode(bool value) {
    _darkMode = value;
    notifyListeners();
  }

  void setRootMode(bool value) {
    _rootMode = value;
    _backend.setRootMode(value);
    notifyListeners();
    refresh();
  }

  void setAutoRefreshLogs(bool value) {
    _autoRefreshLogs = value;
    if (value) {
      _startLogPolling();
    } else {
      _logTimer?.cancel();
      _logTimer = null;
    }
    notifyListeners();
  }

  void setPreflightCheck(bool value) {
    _preflightCheck = value;
    notifyListeners();
  }

  void setClipboardFallback(bool value) {
    _clipboardFallback = value;
    notifyListeners();
  }

  void setRetryOnFail(bool value) {
    _retryOnFail = value;
    notifyListeners();
  }

  void setJsonLogging(bool value) {
    _jsonLogging = value;
    notifyListeners();
  }

  /// Refresh profiles + active status from the local binary.
  Future<void> refresh() async {
    _loading = true;
    _error = '';
    notifyListeners();
    try {
      final data = await _backend.status();
      _activeProfile = data['active_profile'] as String? ?? '';
      final names = (data['profiles'] as List? ?? []).cast<String>();
      _profiles = names
          .map((n) => Profile(
                name: n,
                androidUser: 0,
                status: n == _activeProfile ? 'active' : 'inactive',
              ))
          .toList();
    } catch (e) {
      _error = e.toString();
    } finally {
      _loading = false;
      notifyListeners();
    }
  }

  Future<bool> switchProfile(String name) async {
    try {
      _loading = true;
      notifyListeners();
      await _backend.switchProfile(name);
      await refresh();
      return true;
    } catch (e) {
      _error = e.toString();
      return false;
    } finally {
      _loading = false;
      notifyListeners();
    }
  }

  Future<bool> createProfile(String name) async {
    try {
      _loading = true;
      notifyListeners();
      await _backend.createProfile(name);
      await refresh();
      return true;
    } catch (e) {
      _error = e.toString();
      return false;
    } finally {
      _loading = false;
      notifyListeners();
    }
  }

  Future<bool> restoreProfile(String name) async {
    try {
      await _backend.restoreProfile(name);
      await refresh();
      return true;
    } catch (e) {
      _error = e.toString();
      return false;
    } finally {
      notifyListeners();
    }
  }

  Future<bool> deleteProfile(String name) async {
    try {
      await _backend.deleteProfile(name);
      await refresh();
      return true;
    } catch (e) {
      _error = e.toString();
      return false;
    } finally {
      notifyListeners();
    }
  }

  Future<bool> exportProfile(String name) async {
    try {
      await _backend.exportProfile(name);
      return true;
    } catch (e) {
      _error = e.toString();
      return false;
    } finally {
      notifyListeners();
    }
  }

  Future<bool> triggerMessage(String number, String message) async {
    try {
      await _backend.sendMessage(number, message);
      return true;
    } catch (e) {
      _error = e.toString();
      return false;
    } finally {
      notifyListeners();
    }
  }

  Future<BatchOutcome?> batchUpload(String filename, List<int> bytes) async {
    try {
      final text = String.fromCharCodes(bytes);
      final targets = _parseTargets(filename, text);
      if (targets.isEmpty) {
        _error = 'No valid rows parsed';
        return null;
      }
      final data = await _backend.blast(targets, '');
      return BatchOutcome(
        total: targets.length,
        succeeded: data['succeeded'] as int? ?? 0,
        failed: data['failed'] as int? ?? 0,
        message: 'Parsed ${targets.length} rows',
      );
    } catch (e) {
      _error = e.toString();
      return null;
    } finally {
      notifyListeners();
    }
  }

  List<String> _parseTargets(String filename, String text) {
    final out = <String>[];
    for (final line in text.split('\n')) {
      final l = line.trim();
      if (l.isEmpty || l.startsWith('#')) continue;
      // CSV: number,message  |  TXT: number<TAB>message or number,message
      String number;
      if (l.contains('\t')) {
        number = l.substring(0, l.indexOf('\t')).trim();
      } else if (l.contains(',')) {
        number = l.substring(0, l.indexOf(',')).trim();
      } else {
        number = l;
      }
      if (number.isNotEmpty) out.add(number);
    }
    return out;
  }

  Future<void> _pollLogs() async {
    try {
      final data = await _backend.logs();
      final lines = (data['logs'] as List? ?? []).map((e) => e.toString()).toList();
      _logs = lines.map(LogEntry.fromJson).toList();
      notifyListeners();
    } catch (_) {
      // best-effort
    }
  }

  void _startLogPolling() {
    _logTimer?.cancel();
    _logTimer = Timer.periodic(const Duration(seconds: 4), (_) => _pollLogs());
  }

  @override
  void dispose() {
    _logTimer?.cancel();
    super.dispose();
  }
}
