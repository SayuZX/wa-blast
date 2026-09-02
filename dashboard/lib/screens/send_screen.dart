import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:hugeicons/hugeicons.dart';
import 'package:provider/provider.dart';

import '../state.dart';

/// Manual trigger form + CSV/TXT batch upload, with anti-ban toggle.
class SendScreen extends StatefulWidget {
  const SendScreen({super.key});

  @override
  State<SendScreen> createState() => _SendScreenState();
}

class _SendScreenState extends State<SendScreen> {
  final _numberController = TextEditingController();
  final _messageController = TextEditingController();
  bool _sending = false;
  bool _antiBan = true;

  @override
  void dispose() {
    _numberController.dispose();
    _messageController.dispose();
    super.dispose();
  }

  Future<void> _sendManual() async {
    final number = _numberController.text.trim();
    final message = _messageController.text.trim();
    if (number.isEmpty || message.isEmpty) {
      _toast('Number and message are required');
      return;
    }
    setState(() => _sending = true);
    final ok = await context.read<HarnessState>().triggerMessage(number, message);
    if (!mounted) return;
    setState(() => _sending = false);
    _toast(ok ? 'Message sent to $number' : 'Send failed');
    if (ok) _messageController.clear();
  }

  Future<void> _pickAndUpload() async {
    final state = context.read<HarnessState>();
    final files = await FilePicker.pickFiles(
      type: FileType.custom,
      allowedExtensions: ['csv', 'txt'],
    );
    if (files.isEmpty) return;
    final file = files.single;
    final bytes = await file.readAsBytes();
    if (bytes.isEmpty) {
      _toast('Could not read file');
      return;
    }
    setState(() => _sending = true);
    final outcome = await state.batchUpload(file.name, bytes);
    if (!mounted) return;
    setState(() => _sending = false);
    if (outcome == null) {
      _toast('Batch upload failed');
    } else {
      _toast(
        'Batch: ${outcome.succeeded}/${outcome.total} sent, '
        '${outcome.failed} failed',
      );
    }
  }

  void _toast(String msg) {
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(msg)));
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        // --- Manual trigger ---
        Text('Manual trigger', style: theme.textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w600)),
        const SizedBox(height: 12),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: Column(
              children: [
                TextField(
                  controller: _numberController,
                  keyboardType: TextInputType.phone,
                  decoration: const InputDecoration(
                    labelText: 'Destination number',
                    hintText: '+62 812 3456 7890',
                    prefixIcon: HugeIcon(icon: HugeIcons.strokeRoundedCall),
                    helperText: 'Include country code, e.g. +62',
                  ),
                ),
                const SizedBox(height: 16),
                TextField(
                  controller: _messageController,
                  maxLines: 4,
                  maxLength: 2000,
                  decoration: const InputDecoration(
                    labelText: 'Message',
                    hintText: 'Message body',
                    prefixIcon: HugeIcon(icon: HugeIcons.strokeRoundedChatting01),
                    alignLabelWithHint: true,
                  ),
                ),
                const SizedBox(height: 8),
                SizedBox(
                  width: double.infinity,
                  child: ElevatedButton.icon(
                    onPressed: _sending ? null : _sendManual,
                    icon: _sending
                        ? const _SpinningIcon(icon: HugeIcons.strokeRoundedRefresh, size: 18)
                        : const HugeIcon(icon: HugeIcons.strokeRoundedSent, size: 18),
                    label: Text(_sending ? 'Sending…' : 'Send message'),
                  ),
                ),
              ],
            ),
          ),
        ),

        const SizedBox(height: 24),

        // --- Anti-ban protection ---
        Text('Anti-ban protection', style: theme.textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w600)),
        const SizedBox(height: 12),
        Card(
          child: Column(
            children: [
              SwitchListTile(
                contentPadding: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
                secondary: const HugeIcon(icon: HugeIcons.strokeRoundedAlertCircle),
                title: const Text('Avoid number blocking'),
                subtitle: const Text(
                  'Rate limit, jitter, and cooldown to reduce ban risk',
                ),
                value: _antiBan,
                onChanged: (v) => setState(() => _antiBan = v),
              ),
              const Divider(height: 1),
              const ListTile(
                leading: HugeIcon(icon: HugeIcons.strokeRoundedQueue01),
                title: Text('Max 60 messages / hour'),
                subtitle: Text('Cooldown 5 min every 20 messages'),
                dense: true,
              ),
            ],
          ),
        ),

        const SizedBox(height: 24),

        // --- Batch trigger ---
        Text('Batch trigger (CSV/TXT)', style: theme.textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w600)),
        const SizedBox(height: 8),
        Text(
          'CSV: "number,message" per line. TXT: "number<TAB>message". '
          'Runs sequentially with jittered delay between messages.',
          style: theme.textTheme.bodySmall,
        ),
        const SizedBox(height: 12),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: SizedBox(
              width: double.infinity,
              child: OutlinedButton.icon(
                onPressed: _sending ? null : _pickAndUpload,
                icon: const HugeIcon(icon: HugeIcons.strokeRoundedUpload01),
                label: const Text('Upload file & run batch'),
              ),
            ),
          ),
        ),
      ],
    );
  }
}

/// A HugeIcon that rotates continuously (used for the "loading" state).
class _SpinningIcon extends StatefulWidget {
  const _SpinningIcon({required this.icon, required this.size});

  final List<List<dynamic>> icon;
  final double size;

  @override
  State<_SpinningIcon> createState() => _SpinningIconState();
}

class _SpinningIconState extends State<_SpinningIcon>
    with SingleTickerProviderStateMixin {
  late final AnimationController _controller;

  @override
  void initState() {
    super.initState();
    _controller = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 900),
    )..repeat();
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return RotationTransition(
      turns: _controller,
      child: HugeIcon(icon: widget.icon, size: widget.size),
    );
  }
}
