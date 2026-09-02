import 'package:flutter/material.dart';
import 'package:hugeicons/hugeicons.dart';
import 'package:provider/provider.dart';

import '../models.dart';
import '../state.dart';
import '../theme.dart';

/// Profile list with search/filter + per-item actions (restore/edit/delete/export).
class ProfilesScreen extends StatefulWidget {
  const ProfilesScreen({super.key});

  @override
  State<ProfilesScreen> createState() => _ProfilesScreenState();
}

class _ProfilesScreenState extends State<ProfilesScreen> {
  final _searchController = TextEditingController();
  String _query = '';

  @override
  void dispose() {
    _searchController.dispose();
    super.dispose();
  }

  List<Profile> _filter(List<Profile> all) {
    final q = _query.toLowerCase().trim();
    if (q.isEmpty) return all;
    return all.where((p) => p.name.toLowerCase().contains(q)).toList();
  }

  @override
  Widget build(BuildContext context) {
    final state = context.watch<HarnessState>();

    return Column(
      children: [
        // Search bar.
        Padding(
          padding: const EdgeInsets.fromLTRB(16, 12, 16, 8),
          child: TextField(
            controller: _searchController,
            onChanged: (v) => setState(() => _query = v),
            decoration: InputDecoration(
              hintText: 'Search profile…',
              prefixIcon: const HugeIcon(icon: HugeIcons.strokeRoundedSearch01),
              suffixIcon: _query.isNotEmpty
                  ? IconButton(
                      icon: const HugeIcon(icon: HugeIcons.strokeRoundedCancel01),
                      onPressed: () {
                        _searchController.clear();
                        setState(() => _query = '');
                      },
                    )
                  : null,
              filled: true,
            ),
          ),
        ),

        Expanded(
          child: state.loading && state.profiles.isEmpty
              ? const Center(child: CircularProgressIndicator())
              : state.profiles.isEmpty
                  ? _EmptyProfiles(onCreate: () => _createProfile(context))
                  : ListView.separated(
                      padding: const EdgeInsets.fromLTRB(16, 8, 16, 16),
                      itemCount: _filter(state.profiles).length + 1,
                      separatorBuilder: (_, __) => const SizedBox(height: 8),
                      itemBuilder: (context, i) {
                        final filtered = _filter(state.profiles);
                        if (i == filtered.length) {
                          return _CreateProfileCard(
                              onCreate: () => _createProfile(context));
                        }
                        final profile = filtered[i];
                        return _ProfileCard(
                          profile: profile,
                          onSwitch: () => _confirmSwitch(context, profile),
                          onActions: () => _showActions(context, profile),
                        );
                      },
                    ),
        ),
      ],
    );
  }

  // ------------------------------------------------------------------ //
  // Actions
  // ------------------------------------------------------------------ //
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
              _snack(ok ? 'Switched to ${profile.name}' : 'Switch failed: ${state.error}');
            },
            child: const Text('Switch'),
          ),
        ],
      ),
    );
  }

  void _showActions(BuildContext context, Profile profile) {
    final state = context.read<HarnessState>();
    showModalBottomSheet<void>(
      context: context,
      builder: (sheetContext) => SafeArea(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            ListTile(
              leading: const HugeIcon(icon: HugeIcons.strokeRoundedUserSwitch),
              title: Text('Switch to ${profile.name}'),
              onTap: () {
                Navigator.pop(sheetContext);
                _confirmSwitch(context, profile);
              },
            ),
            ListTile(
              leading: const HugeIcon(icon: HugeIcons.strokeRoundedDatabaseRestore),
              title: const Text('Restore'),
              subtitle: const Text('Restore snapshot data'),
              onTap: () async {
                Navigator.pop(sheetContext);
                final ok = await state.restoreProfile(profile.name);
                _snack(ok ? 'Restored ${profile.name}' : 'Restore failed: ${state.error}');
              },
            ),
            ListTile(
              leading: const HugeIcon(icon: HugeIcons.strokeRoundedEdit01),
              title: const Text('Edit'),
              subtitle: const Text('Edit identity / props'),
              onTap: () {
                Navigator.pop(sheetContext);
                _editProfile(context, profile);
              },
            ),
            ListTile(
              leading: const HugeIcon(icon: HugeIcons.strokeRoundedFileExport),
              title: const Text('Export'),
              subtitle: const Text('Export snapshot to storage'),
              onTap: () async {
                Navigator.pop(sheetContext);
                final ok = await state.exportProfile(profile.name);
                _snack(ok ? 'Exported ${profile.name}' : 'Export failed: ${state.error}');
              },
            ),
            ListTile(
              leading: const HugeIcon(icon: HugeIcons.strokeRoundedDelete01),
              title: const Text('Delete'),
              subtitle: const Text('Remove profile permanently'),
              textColor: Theme.of(context).colorScheme.error,
              onTap: () {
                Navigator.pop(sheetContext);
                _confirmDelete(context, profile);
              },
            ),
          ],
        ),
      ),
    );
  }

  void _confirmDelete(BuildContext context, Profile profile) {
    final state = context.read<HarnessState>();
    showDialog<void>(
      context: context,
      builder: (dialogContext) => AlertDialog(
        title: const Text('Delete profile?'),
        content: Text('Delete "${profile.name}" permanently? This cannot be undone.'),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(dialogContext),
            child: const Text('Cancel'),
          ),
          ElevatedButton(
            onPressed: () async {
              Navigator.pop(dialogContext);
              final ok = await state.deleteProfile(profile.name);
              if (!context.mounted) return;
              _snack(ok ? 'Deleted ${profile.name}' : 'Delete failed: ${state.error}');
            },
            child: const Text('Delete'),
          ),
        ],
      ),
    );
  }

  Future<void> _editProfile(BuildContext context, Profile profile) async {
    final state = context.read<HarnessState>();
    final controller = TextEditingController(text: profile.name);
    final newName = await showDialog<String>(
      context: context,
      builder: (dialogContext) => AlertDialog(
        title: const Text('Edit profile'),
        content: TextField(
          controller: controller,
          autofocus: true,
          decoration: const InputDecoration(labelText: 'Profile name'),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(dialogContext),
            child: const Text('Cancel'),
          ),
          ElevatedButton(
            onPressed: () => Navigator.pop(dialogContext, controller.text.trim()),
            child: const Text('Save'),
          ),
        ],
      ),
    );
    if (newName == null || newName.isEmpty || newName == profile.name) return;
    await state.backend.editProfile(profile.name, {'name': newName});
    if (!mounted) return;
    _snack('Renamed to $newName');
    await state.refresh();
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
    _snack(ok ? 'Created $name' : 'Create failed: ${state.error}');
  }

  void _snack(String msg) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(msg)));
  }
}

class _ProfileCard extends StatelessWidget {
  const _ProfileCard({
    required this.profile,
    required this.onSwitch,
    required this.onActions,
  });

  final Profile profile;
  final VoidCallback onSwitch;
  final VoidCallback onActions;

  @override
  Widget build(BuildContext context) {
    final isActive = profile.isActive;
    final statusColor =
        isActive ? HarnessPalette.statusActive : HarnessPalette.statusInactive;

    return Card(
      child: InkWell(
        onTap: onActions,
        borderRadius: BorderRadius.circular(12),
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Row(
            children: [
              HugeIcon(
                icon: HugeIcons.strokeRoundedUserCircle,
                size: 28,
                strokeWidth: isActive ? 2.5 : 1.5,
                color: isActive ? statusColor : HarnessPalette.statusInactive,
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
                      isActive ? 'active' : 'inactive',
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
              IconButton(
                tooltip: 'Actions',
                icon: const HugeIcon(icon: HugeIcons.strokeRoundedMoreVertical),
                onPressed: onActions,
              ),
            ],
          ),
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
        subtitle: const Text('Add a new WhatsApp profile'),
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
          Text('No profiles yet', style: Theme.of(context).textTheme.titleMedium),
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
