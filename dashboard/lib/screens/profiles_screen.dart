import 'package:flutter/material.dart';
import 'package:hugeicons/hugeicons.dart';
import 'package:provider/provider.dart';

import '../models.dart';
import '../state.dart';
import '../theme.dart';

/// Profile list + switch (with confirmation dialog) + create new profile.
class ProfilesScreen extends StatelessWidget {
  const ProfilesScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final state = context.watch<HarnessState>();

    if (state.loading && state.profiles.isEmpty) {
      return const Center(child: CircularProgressIndicator());
    }

    if (state.profiles.isEmpty) {
      return _EmptyProfiles(onCreate: () => _createProfile(context));
    }

    return ListView.separated(
      padding: const EdgeInsets.all(16),
      itemCount: state.profiles.length + 1, // +1 for "create" card
      separatorBuilder: (_, __) => const SizedBox(height: 8),
      itemBuilder: (context, i) {
        if (i == state.profiles.length) {
          return _CreateProfileCard(onCreate: () => _createProfile(context));
        }
        final profile = state.profiles[i];
        return _ProfileCard(
          profile: profile,
          onSwitch: () => _confirmSwitch(context, profile),
        );
      },
    );
  }

  void _confirmSwitch(BuildContext context, Profile profile) {
    final state = context.read<HarnessState>();
    showDialog<void>(
      context: context,
      builder: (dialogContext) => AlertDialog(
        title: const Text('Switch profile?'),
        content: Text(
          'Switch active profile to "${profile.name}"?\n\n'
          'The current profile will be snapshotted before switching.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(dialogContext),
            child: const Text('Cancel'),
          ),
          ElevatedButton(
            onPressed: () async {
              Navigator.pop(dialogContext);
              final ok = await state.switchProfile(profile.name);
              if (!context.mounted) return;
              ScaffoldMessenger.of(context).showSnackBar(
                SnackBar(
                  content: Text(
                    ok
                        ? 'Switched to ${profile.name}'
                        : 'Switch failed: ${state.error}',
                  ),
                ),
              );
            },
            child: const Text('Switch'),
          ),
        ],
      ),
    );
  }

  Future<void> _createProfile(BuildContext context) async {
    final state = context.read<HarnessState>();
    final controller = TextEditingController();
    final name = await showDialog<String>(
      context: context,
      builder: (dialogContext) => AlertDialog(
        title: const Text('Create profile'),
        content: TextField(
          controller: controller,
          autofocus: true,
          decoration: const InputDecoration(
            labelText: 'Profile name',
            hintText: 'e.g. WA_3',
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(dialogContext),
            child: const Text('Cancel'),
          ),
          ElevatedButton(
            onPressed: () => Navigator.pop(dialogContext, controller.text.trim()),
            child: const Text('Create'),
          ),
        ],
      ),
    );

    if (name == null || name.isEmpty) return;
    final ok = await state.createProfile(name);
    if (!context.mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(content: Text(ok ? 'Created $name' : 'Create failed: ${state.error}')),
    );
  }
}

class _ProfileCard extends StatelessWidget {
  const _ProfileCard({required this.profile, required this.onSwitch});

  final Profile profile;
  final VoidCallback onSwitch;

  @override
  Widget build(BuildContext context) {
    final isActive = profile.isActive;
    final statusColor =
        isActive ? HarnessPalette.statusActive : HarnessPalette.statusInactive;

    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Row(
          children: [
            // UserCircle icon: thick stroke + solid = active; low opacity = inactive.
            HugeIcon(
              icon: HugeIcons.strokeRoundedUserCircle,
              size: 28,
              strokeWidth: isActive ? 2.5 : 1.5,
              color: isActive
                  ? statusColor
                  : HarnessPalette.statusInactive,
            ),
            const SizedBox(width: 16),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    profile.name,
                    style: Theme.of(context).textTheme.titleMedium?.copyWith(
                          fontWeight: FontWeight.w600,
                        ),
                  ),
                  const SizedBox(height: 4),
                  Text(
                    'Android user ${profile.androidUser} · '
                    '${isActive ? 'active' : 'inactive'}',
                    style: Theme.of(context).textTheme.bodySmall,
                  ),
                ],
              ),
            ),
            if (isActive)
              const HugeIcon(icon: HugeIcons.strokeRoundedCheckmarkCircle01, size: 20)
            else
              ElevatedButton.icon(
                onPressed: onSwitch,
                icon: const HugeIcon(icon: HugeIcons.strokeRoundedUserSwitch, size: 16),
                label: const Text('Switch'),
              ),
          ],
        ),
      ),
    );
  }
}

class _CreateProfileCard extends StatelessWidget {
  const _CreateProfileCard({required this.onCreate});

  final VoidCallback onCreate;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: ListTile(
        leading: const HugeIcon(icon: HugeIcons.strokeRoundedAddCircle),
        title: const Text('Create new profile'),
        subtitle: const Text('Provision a new Android user + identity'),
        onTap: onCreate,
      ),
    );
  }
}

class _EmptyProfiles extends StatelessWidget {
  const _EmptyProfiles({required this.onCreate});

  final VoidCallback onCreate;

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          const HugeIcon(icon: HugeIcons.strokeRoundedUserGroup, size: 48),
          const SizedBox(height: 16),
          Text(
            'No profiles yet',
            style: Theme.of(context).textTheme.titleMedium,
          ),
          const SizedBox(height: 16),
          ElevatedButton(
            onPressed: onCreate,
            child: const Text('Create WA_1'),
          ),
        ],
      ),
    );
  }
}
