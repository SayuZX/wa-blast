import 'package:flutter/material.dart';
import 'package:hugeicons/hugeicons.dart';
import 'package:provider/provider.dart';

import '../state.dart';

/// Settings screen: connection + appearance + behaviour toggles.
class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key});

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  late final TextEditingController _urlController;
  late final TextEditingController _keyController;
  bool _showKey = false;

  @override
  void initState() {
    super.initState();
    final state = context.read<HarnessState>();
    _urlController = TextEditingController(text: state.client.baseUrl);
    _keyController = TextEditingController(text: state.client.apiKey);
  }

  @override
  void dispose() {
    _urlController.dispose();
    _keyController.dispose();
    super.dispose();
  }

  Future<void> _save() async {
    final state = context.read<HarnessState>();
    await state.saveConfig(
      _urlController.text.trim(),
      _keyController.text.trim(),
    );
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text('Saved')),
    );
  }

  @override
  Widget build(BuildContext context) {
    final state = context.watch<HarnessState>();
    final theme = Theme.of(context);

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        // --- Connection ---
        _sectionTitle(theme, 'Connection'),
        const SizedBox(height: 12),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: Column(
              children: [
                TextField(
                  controller: _urlController,
                  decoration: const InputDecoration(
                    labelText: 'Backend URL',
                    hintText: 'http://127.0.0.1:8000',
                    prefixIcon: HugeIcon(icon: HugeIcons.strokeRoundedLink01),
                  ),
                ),
                const SizedBox(height: 16),
                TextField(
                  controller: _keyController,
                  obscureText: !_showKey,
                  decoration: InputDecoration(
                    labelText: 'API Key',
                    prefixIcon: const HugeIcon(icon: HugeIcons.strokeRoundedKey01),
                    suffixIcon: IconButton(
                      icon: HugeIcon(
                        icon: _showKey
                            ? HugeIcons.strokeRoundedView
                            : HugeIcons.strokeRoundedViewOff,
                      ),
                      onPressed: () => setState(() => _showKey = !_showKey),
                    ),
                  ),
                ),
                const SizedBox(height: 16),
                SizedBox(
                  width: double.infinity,
                  child: ElevatedButton.icon(
                    onPressed: _save,
                    icon: const HugeIcon(
                        icon: HugeIcons.strokeRoundedCheckmarkCircle01),
                    label: const Text('Save settings'),
                  ),
                ),
              ],
            ),
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
                subtitle: 'Use clipboard if `input text` fails (long text)',
                value: state.clipboardFallback,
                onChanged: state.setClipboardFallback,
              ),
              _toggle(
                icon: HugeIcons.strokeRoundedRefresh,
                title: 'Retry on fail',
                subtitle: 'Auto-retry up to 3× with backoff (5/10/20s)',
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

        const SizedBox(height: 24),

        // --- Active profile ---
        _sectionTitle(theme, 'Active profile'),
        const SizedBox(height: 12),
        Card(
          child: ListTile(
            leading: const HugeIcon(icon: HugeIcons.strokeRoundedUserCircle),
            title: Text(
              state.activeProfile.isEmpty ? 'None' : state.activeProfile,
              style: theme.textTheme.titleMedium?.copyWith(
                fontWeight: FontWeight.w600,
              ),
            ),
            subtitle: const Text('Current profile used for sends'),
          ),
        ),
      ],
    );
  }

  Widget _sectionTitle(ThemeData theme, String text) {
    return Text(
      text,
      style: theme.textTheme.titleMedium?.copyWith(
        fontWeight: FontWeight.w600,
      ),
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
