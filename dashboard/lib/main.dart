import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import 'screens/home_screen.dart';
import 'screens/root_check_screen.dart';
import 'state.dart';
import 'theme.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const HarnessApp());
}

class HarnessApp extends StatelessWidget {
  const HarnessApp({super.key});

  @override
  Widget build(BuildContext context) {
    return ChangeNotifierProvider(
      create: (_) => HarnessState()..init(),
      child: Consumer<HarnessState>(
        builder: (context, state, _) {
          return MaterialApp(
            title: 'QA Harness',
            debugShowCheckedModeBanner: false,
            theme: buildLightTheme(),
            darkTheme: buildDarkTheme(),
            themeMode: state.darkMode ? ThemeMode.dark : ThemeMode.light,
            home: const _RootGate(),
          );
        },
      ),
    );
  }
}

/// Show the root-check screen first, then the dashboard once passed.
class _RootGate extends StatefulWidget {
  const _RootGate();

  @override
  State<_RootGate> createState() => _RootGateState();
}

class _RootGateState extends State<_RootGate> {
  bool _passed = false;

  @override
  Widget build(BuildContext context) {
    if (_passed) return const HomeScreen();
    return RootCheckScreen(
      onContinue: () => setState(() => _passed = true),
    );
  }
}
