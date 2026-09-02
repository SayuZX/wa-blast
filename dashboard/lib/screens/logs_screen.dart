import 'package:flutter/material.dart';
import 'package:hugeicons/hugeicons.dart';
import 'package:provider/provider.dart';

import '../state.dart';

/// Real-time activity log (polled from backend /api/logs).
/// Icons: File01 (menu), CheckmarkCircle01 (success), AlertCircle (failed).
class LogsScreen extends StatelessWidget {
  const LogsScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final state = context.watch<HarnessState>();

    if (state.logs.isEmpty) {
      return const Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            HugeIcon(icon: HugeIcons.strokeRoundedFile01, size: 48),
            SizedBox(height: 16),
            Text('No log activity yet'),
          ],
        ),
      );
    }

    return ListView.builder(
      padding: const EdgeInsets.all(16),
      itemCount: state.logs.length,
      itemBuilder: (context, i) {
        // Reverse so newest is at the top (real-time feel).
        final entry = state.logs[state.logs.length - 1 - i];
        return Padding(
          padding: const EdgeInsets.symmetric(vertical: 4),
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const HugeIcon(icon: HugeIcons.strokeRoundedArrowRight01, size: 14),
              const SizedBox(width: 8),
              Expanded(
                child: Text(
                  entry.line,
                  style: Theme.of(context).textTheme.bodySmall,
                ),
              ),
            ],
          ),
        );
      },
    );
  }
}
