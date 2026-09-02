import 'dart:async';

import 'package:flutter/foundation.dart';

import 'api_client.dart';
import 'models.dart';

/// Application state (ChangeNotifier) shared across the dashboard.
class HarnessState extends ChangeNotifier {
  HarnessState({ApiClient? client}) : _client = client ?? ApiClient();

  final ApiClient _client;

  bool _loading = false;
  bool _initialized = false;
  String _error = '';
  String _activeProfile = '';
  List<Profile> _profiles = [];
  List<LogEntry> _logs = [];
  bool _darkMode = false;

  // Settings toggles (persisted via shared_preferences).
  bool _autoRefreshLogs = true;
  bool _preflightCheck = true;
  bool _clipboardFallback = true;
  bool _retryOnFail = true;
  bool _jsonLogging = true;

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
  ApiClient get client => _client;

  Future<void> init() async {
    await _client.loadPrefs();
    _darkMode = true; // default to dark (neutral) — can be toggled.
    _initialized = true;
    notifyListeners();
    await refresh();
    _startLogPolling();
  }

  void setDarkMode(bool value) {
    _darkMode = value;
    notifyListeners();
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

  Future<void> saveConfig(String baseUrl, String apiKey) async {
    await _client.savePrefs(baseUrl: baseUrl, apiKey: apiKey);
    notifyListeners();
  }

  Future<void> refresh() async {
    _loading = true;
    _error = '';
    notifyListeners();
    try {
      final data = await _client.listProfiles();
      _activeProfile = data['active'] as String? ?? '';
      final list = (data['profiles'] as List? ?? [])
          .map((e) => Profile.fromJson(e as Map<String, dynamic>))
          .toList();
      _profiles = list;
    } on ApiException catch (e) {
      _error = e.message;
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
      await _client.switchProfile(name);
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
      await _client.createProfile(name);
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

  Future<bool> triggerMessage(String number, String message) async {
    try {
      await _client.triggerMessage(number, message);
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
      final data = await _client.batchUpload(filename, bytes);
      return BatchOutcome(
        total: data['total'] as int? ?? 0,
        succeeded: data['succeeded'] as int? ?? 0,
        failed: data['failed'] as int? ?? 0,
        message: 'Parsed ${data['parsed']} rows',
      );
    } catch (e) {
      _error = e.toString();
      return null;
    } finally {
      notifyListeners();
    }
  }

  Future<void> _pollLogs() async {
    try {
      final data = await _client.logs(lines: 100);
      final lines = (data['lines'] as List? ?? []).cast<String>();
      _logs = lines.map(LogEntry.fromJson).toList();
      notifyListeners();
    } catch (_) {
      // Log polling is best-effort; ignore transient failures.
    }
  }

  void _startLogPolling() {
    _logTimer = Timer.periodic(const Duration(seconds: 4), (_) => _pollLogs());
  }

  @override
  void dispose() {
    _logTimer?.cancel();
    super.dispose();
  }
}
