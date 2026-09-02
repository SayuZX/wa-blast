import 'package:flutter/material.dart';
import 'package:hugeicons/hugeicons.dart';

import '../root_service.dart';
import '../theme.dart';

/// Root-check gate shown before entering the dashboard.
/// Verifies `su` access; if rooted, offers "Continue"; otherwise shows a
/// warning and a retry button.
class RootCheckScreen extends StatefulWidget {
  const RootCheckScreen({super.key, required this.onContinue});

  final VoidCallback onContinue;

  @override
  State<RootCheckScreen> createState() => _RootCheckScreenState();
}

class _RootCheckScreenState extends State<RootCheckScreen> {
  bool _checking = true;
  bool? _rooted;

  @override
  void initState() {
    super.initState();
    _check();
  }

  Future<void> _check() async {
    setState(() => _checking = true);
    final rooted = await RootService.checkRoot();
    if (!mounted) return;
    setState(() {
      _rooted = rooted;
      _checking = false;
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Center(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              if (_checking)
                const HugeIcon(icon: HugeIcons.strokeRoundedRefresh, size: 48)
              else if (_rooted == true)
                const HugeIcon(
                    icon: HugeIcons.strokeRoundedCheckmarkCircle01, size: 48)
              else
                const HugeIcon(icon: HugeIcons.strokeRoundedAlertCircle, size: 48),
              const SizedBox(height: 24),
              Text(
                _checking
                    ? 'Checking root access…'
                    : (_rooted == true
                        ? 'Root access detected'
                        : 'Root access not available'),
                style: Theme.of(context).textTheme.titleLarge,
              ),
              const SizedBox(height: 12),
              Text(
                _checking
                    ? 'Verifying `su` binary…'
                    : (_rooted == true
                        ? 'The device is rooted. You can run wa-cli commands.'
                        : 'This app requires a rooted device (Magisk). '
                          'The HTTP server & ADB control will not work.'),
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                      color: HarnessPalette.gray500,
                    ),
              ),
              const SizedBox(height: 32),
              if (!_checking && _rooted == true)
                ElevatedButton.icon(
                  onPressed: widget.onContinue,
                  icon: const HugeIcon(icon: HugeIcons.strokeRoundedArrowRight01),
                  label: const Text('Continue to dashboard'),
                )
              else if (!_checking && _rooted == false)
                Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    OutlinedButton.icon(
                      onPressed: _check,
                      icon: const HugeIcon(icon: HugeIcons.strokeRoundedRefresh),
                      label: const Text('Retry'),
                    ),
                    const SizedBox(width: 12),
                    OutlinedButton(
                      onPressed: widget.onContinue,
                      child: const Text('Continue anyway'),
                    ),
                  ],
                ),
            ],
          ),
        ),
      ),
    );
  }
}
