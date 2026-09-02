import 'package:flutter/material.dart';
import 'package:hugeicons/hugeicons.dart';
import 'package:provider/provider.dart';

import '../root_service.dart';
import '../state.dart';

/// Settings screen: root status, appearance, and behaviour toggles.
///
/// Note: there is NO backend URL / API key anymore — the C++ binary ships
/// inside the APK and is called directly via the platform channel.
class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key});

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  bool? _rooted;

  @override
  void initState() {
    super.initState();
    _checkRoot();
  }

  Future<void> _checkRoot() async {
    final r = await RootService.checkRoot();
    if (!mounted) return;
    setState(() => _rooted = r);
  }

  @override
  Widget build(BuildContext context) {
    final state = context.watch<HarnessState>();
    final theme = Theme.of(context);

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        // --- Device / backend status ---
        _sectionTitle(theme, 'Device'),
        const SizedBox(height: 12),
        Card(
          child: Column(
            children: [
              ListTile(
                leading: HugeIcon(
                  icon: _rooted == true
                      ? HugeIcons.strokeRoundedCheckmarkCircle01
                      : HugeIcons.strokeRoundedAlertCircle,
                ),
                title: Text(_rooted == true ? 'Root access' : 'No root access'),
                subtitle: const Text('C++ binary runs on-device via su'),
              ),
              SwitchListTile(
                contentPadding: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
                secondary: const HugeIcon(icon: HugeIcons.strokeRoundedCube),
                title: const Text('Non-root (simulation) mode'),
                subtitle: const Text(
                  'Simulasikan kirim & profil tanpa root. Matikan untuk pakai binary C++ (butuh root).',
                ),
                value: state.simulateMode,
                onChanged: state.setSimulateMode,
              ),
              const ListTile(
                leading: HugeIcon(icon: HugeIcons.strokeRoundedCube),
                title: Text('Backend'),
                subtitle: Text('wa_apid (bundled C++ binary)'),
                trailing: HugeIcon(icon: HugeIcons.strokeRoundedLink01),
              ),
            ],
          ),
        ),

        const SizedBox(height: 24),

        // --- Appearance ---
        _sectionTitle(theme, 'Appearance'),
        const SizedBox(height: 12),
        Card(
          child: Column(
            children: [
              _toggle(
                icon: HugeIcons.strokeRoundedMoon02,
                title: 'Dark mode',
                subtitle: 'Neutral dark palette (solid, no transparency)',
                value: state.darkMode,
                onChanged: state.setDarkMode,
              ),
            ],
          ),
        ),

        const SizedBox(height: 24),

        // --- Sending behaviour ---
        _sectionTitle(theme, 'Sending behaviour'),
        const SizedBox(height: 12),
        Card(
          child: Column(
            children: [
              _toggle(
                icon: HugeIcons.strokeRoundedCheckUnread01,
                title: 'Preflight check',
                subtitle: 'Verify WhatsApp is focused before sending',
                value: state.preflightCheck,
                onChanged: state.setPreflightCheck,
              ),
              _toggle(
                icon: HugeIcons.strokeRoundedBubbleChatDownload01,
                title: 'Clipboard fallback',
                subtitle: 'Use clipboard if `input text` fails',
                value: state.clipboardFallback,
                onChanged: state.setClipboardFallback,
              ),
              _toggle(
                icon: HugeIcons.strokeRoundedRefresh,
                title: 'Retry on fail',
                subtitle: 'Auto-retry up to 3× with backoff',
                value: state.retryOnFail,
                onChanged: state.setRetryOnFail,
              ),
            ],
          ),
        ),

        const SizedBox(height: 24),

        // --- Logs ---
        _sectionTitle(theme, 'Logs'),
        const SizedBox(height: 12),
        Card(
          child: Column(
            children: [
              _toggle(
                icon: HugeIcons.strokeRoundedArrowReloadVertical,
                title: 'Auto-refresh logs',
                subtitle: 'Poll activity every 4 seconds',
                value: state.autoRefreshLogs,
                onChanged: state.setAutoRefreshLogs,
              ),
              _toggle(
                icon: HugeIcons.strokeRoundedFile01,
                title: 'Structured JSON logging',
                subtitle: 'Write JSON-lines log files',
                value: state.jsonLogging,
                onChanged: state.setJsonLogging,
              ),
            ],
          ),
        ),
      ],
    );
  }

  Widget _sectionTitle(ThemeData theme, String text) {
    return Text(
      text,
      style: theme.textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w600),
    );
  }

  Widget _toggle({
    required dynamic icon,
    required String title,
    required String subtitle,
    required bool value,
    required ValueChanged<bool> onChanged,
  }) {
    return SwitchListTile(
      contentPadding: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
      secondary: HugeIcon(icon: icon),
      title: Text(title),
      subtitle: Text(subtitle),
      value: value,
      onChanged: onChanged,
    );
  }
}
