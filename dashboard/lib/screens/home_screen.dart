import 'package:flutter/material.dart';
import 'package:hugeicons/hugeicons.dart';
import 'package:provider/provider.dart';

import '../state.dart';
import 'logs_screen.dart';
import 'profiles_screen.dart';
import 'send_screen.dart';
import 'settings_screen.dart';

/// Root scaffold with a Material 3 bottom navigation bar.
/// Four sections: Profiles, Send, Logs, Settings.
class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  int _index = 0;

  static const _titles = ['Profiles', 'Send', 'Logs', 'Settings'];

  final _screens = const [
    ProfilesScreen(),
    SendScreen(),
    LogsScreen(),
    SettingsScreen(),
  ];

  @override
  Widget build(BuildContext context) {
    final state = context.watch<HarnessState>();

    return Scaffold(
      appBar: AppBar(
        title: Text(_titles[_index]),
        actions: [
          IconButton(
            tooltip: 'Toggle theme',
            icon: HugeIcon(
              icon: state.darkMode
                  ? HugeIcons.strokeRoundedSun02
                  : HugeIcons.strokeRoundedMoon02,
            ),
            onPressed: () => state.setDarkMode(!state.darkMode),
          ),
          IconButton(
            tooltip: 'Refresh',
            icon: const HugeIcon(icon: HugeIcons.strokeRoundedRefresh),
            onPressed: () => state.refresh(),
          ),
        ],
      ),
      body: _screens[_index],
      bottomNavigationBar: NavigationBar(
        selectedIndex: _index,
        onDestinationSelected: (i) => setState(() => _index = i),
        destinations: const [
          NavigationDestination(
            icon: HugeIcon(icon: HugeIcons.strokeRoundedUserGroup),
            label: 'Profiles',
          ),
          NavigationDestination(
            icon: HugeIcon(icon: HugeIcons.strokeRoundedSent),
            label: 'Send',
          ),
          NavigationDestination(
            icon: HugeIcon(icon: HugeIcons.strokeRoundedAlignLeft),
            label: 'Logs',
          ),
          NavigationDestination(
            icon: HugeIcon(icon: HugeIcons.strokeRoundedSettings01),
            label: 'Settings',
          ),
        ],
      ),
    );
  }
}
