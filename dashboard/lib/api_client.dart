import 'dart:convert';

import 'package:http/http.dart' as http;
import 'package:shared_preferences/shared_preferences.dart';

/// Thin API client for the QA harness backend.
///
/// Persists server URL + API key locally. Every mutating call attaches the
/// `X-API-Key` header; reads of `/health` are unauthenticated.
class ApiClient {
  ApiClient({http.Client? client}) : _client = client ?? http.Client();

  final http.Client _client;

  String _baseUrl = 'http://127.0.0.1:8000';
  String _apiKey = '';

  String get baseUrl => _baseUrl;
  String get apiKey => _apiKey;

  Future<void> loadPrefs() async {
    final prefs = await SharedPreferences.getInstance();
    _baseUrl = prefs.getString('baseUrl') ?? _baseUrl;
    _apiKey = prefs.getString('apiKey') ?? '';
  }

  Future<void> savePrefs({String? baseUrl, String? apiKey}) async {
    final prefs = await SharedPreferences.getInstance();
    if (baseUrl != null) {
      _baseUrl = baseUrl;
      await prefs.setString('baseUrl', baseUrl);
    }
    if (apiKey != null) {
      _apiKey = apiKey;
      await prefs.setString('apiKey', apiKey);
    }
  }

  Uri _uri(String path, [Map<String, String>? query]) {
    final uri = Uri.parse('$_baseUrl$path');
    return query == null ? uri : uri.replace(queryParameters: query);
  }

  Map<String, String> _headers({bool json = false}) => {
        if (json) 'Content-Type': 'application/json',
        if (_apiKey.isNotEmpty) 'X-API-Key': _apiKey,
      };

  Future<dynamic> _decode(http.Response res) async {
    if (res.body.isEmpty) return null;
    return jsonDecode(res.body);
  }

  // ---- health (no auth) ----
  Future<Map<String, dynamic>> health() async {
    final res = await _client.get(_uri('/health'));
    return (await _decode(res)) as Map<String, dynamic>;
  }

  // ---- profiles ----
  Future<Map<String, dynamic>> listProfiles() async {
    final res = await _client.get(_uri('/profiles'), headers: _headers());
    _throwIfError(res);
    return (await _decode(res)) as Map<String, dynamic>;
  }

  Future<Map<String, dynamic>> createProfile(String name) async {
    final res = await _client.post(
      _uri('/profile'),
      headers: _headers(json: true),
      body: jsonEncode({'name': name}),
    );
    _throwIfError(res);
    return (await _decode(res)) as Map<String, dynamic>;
  }

  Future<Map<String, dynamic>> switchProfile(
    String name, {
    bool snapshotCurrent = true,
  }) async {
    final res = await _client.post(
      _uri('/profile/switch'),
      headers: _headers(json: true),
      body: jsonEncode({'profile': name, 'snapshot_current': snapshotCurrent}),
    );
    _throwIfError(res);
    return (await _decode(res)) as Map<String, dynamic>;
  }

  // ---- messages ----
  Future<Map<String, dynamic>> triggerMessage(
    String number,
    String message, {
    String method = 'auto',
  }) async {
    final res = await _client.post(
      _uri('/message/trigger'),
      headers: _headers(json: true),
      body: jsonEncode(
        {'number': number, 'message': message, 'method': method},
      ),
    );
    _throwIfError(res);
    return (await _decode(res)) as Map<String, dynamic>;
  }

  Future<Map<String, dynamic>> batchMessage(List<Map<String, String>> items) async {
    final res = await _client.post(
      _uri('/message/batch'),
      headers: _headers(json: true),
      body: jsonEncode({'items': items}),
    );
    _throwIfError(res);
    return (await _decode(res)) as Map<String, dynamic>;
  }

  Future<Map<String, dynamic>> batchUpload(
    String filename,
    List<int> bytes,
  ) async {
    final req = http.MultipartRequest('POST', _uri('/message/batch/upload'));
    req.headers.addAll(_headers());
    req.files.add(
      http.MultipartFile.fromBytes('file', bytes, filename: filename),
    );
    final streamed = await _client.send(req);
    final res = await http.Response.fromStream(streamed);
    _throwIfError(res);
    return (await _decode(res)) as Map<String, dynamic>;
  }

  // ---- logs ----
  Future<Map<String, dynamic>> logs({int lines = 100}) async {
    final res = await _client.get(
      _uri('/logs', {'lines': '$lines'}),
      headers: _headers(),
    );
    _throwIfError(res);
    return (await _decode(res)) as Map<String, dynamic>;
  }

  void _throwIfError(http.Response res) {
    if (res.statusCode >= 200 && res.statusCode < 300) return;
    String detail = res.body;
    try {
      final body = jsonDecode(res.body);
      if (body is Map && body['detail'] != null) detail = body['detail'].toString();
    } catch (_) {}
    throw ApiException(res.statusCode, detail);
  }
}

class ApiException implements Exception {
  ApiException(this.statusCode, this.message);
  final int statusCode;
  final String message;

  @override
  String toString() => 'API $statusCode: $message';
}
