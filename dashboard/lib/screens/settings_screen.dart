import 'package:flutter/material.dart';
import 'package:hugeicons/hugeicons.dart';
import 'package:provider/provider.dart';

import '../state.dart';

/// Server URL + API key configuration, plus theme toggle and health status.
class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key});

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  late final TextEditingController _urlController;
  late final TextEditingController _keyController;

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

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        Text('Connection', style: Theme.of(context).textTheme.titleLarge),
        const SizedBox(height: 16),
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
          obscureText: true,
          decoration: const InputDecoration(
            labelText: 'API Key',
            prefixIcon: HugeIcon(icon: HugeIcons.strokeRoundedKey01),
          ),
        ),
        const SizedBox(height: 16),
        ElevatedButton.icon(
          onPressed: _save,
          icon: const HugeIcon(icon: HugeIcons.strokeRoundedCheckmarkCircle01),
          label: const Text('Save settings'),
        ),
        const SizedBox(height: 32),
        const Divider(),
        const SizedBox(height: 16),
        Text('Appearance', style: Theme.of(context).textTheme.titleLarge),
        SwitchListTile(
          contentPadding: EdgeInsets.zero,
          title: const Text('Dark mode'),
          subtitle: const Text('Neutral dark palette'),
          secondary: const HugeIcon(icon: HugeIcons.strokeRoundedMoon02),
          value: state.darkMode,
          onChanged: (v) => state.setDarkMode(v),
        ),
        const SizedBox(height: 16),
        Text('Active profile', style: Theme.of(context).textTheme.titleLarge),
        const SizedBox(height: 8),
        Text(
          state.activeProfile.isEmpty ? 'None' : state.activeProfile,
          style: Theme.of(context).textTheme.bodyMedium,
        ),
      ],
    );
  }
}
