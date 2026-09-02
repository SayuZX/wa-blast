import 'package:flutter/foundation.dart';

/// A logical QA profile (WA_1 .. WA_N).
class Profile {
  Profile({
    required this.name,
    required this.androidUser,
    required this.status,
    this.snapshotPath,
    this.createdAt,
  });

  final String name;
  final int androidUser;
  final String status;
  final String? snapshotPath;
  final String? createdAt;

  bool get isActive => status == 'active';

  factory Profile.fromJson(Map<String, dynamic> json) => Profile(
        name: json['name'] as String? ?? '',
        androidUser: json['android_user'] as int? ?? 0,
        status: json['status'] as String? ?? 'inactive',
        snapshotPath: json['snapshot_path'] as String?,
        createdAt: json['created_at'] as String?,
      );
}

/// A single structured log line from the backend.
@immutable
class LogEntry {
  const LogEntry({required this.line});

  final String line;

  factory LogEntry.fromJson(String line) => LogEntry(line: line);
}

/// Result of a batch trigger operation.
class BatchOutcome {
  BatchOutcome({
    required this.total,
    required this.succeeded,
    required this.failed,
    this.message = '',
  });

  final int total;
  final int succeeded;
  final int failed;
  final String message;

  bool get ok => failed == 0;
}
