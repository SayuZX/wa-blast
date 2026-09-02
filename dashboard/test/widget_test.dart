// Basic widget smoke test for the QA Harness dashboard.

import 'package:flutter_test/flutter_test.dart';

import 'package:qa_harness_dashboard/main.dart';

void main() {
  testWidgets('Dashboard renders without crashing', (WidgetTester tester) async {
    await tester.pumpWidget(const HarnessApp());
    // Let the first frame + provider init settle.
    await tester.pump();

    // Bottom navigation has four sections.
    expect(find.text('Profiles'), findsWidgets);
    expect(find.text('Send'), findsWidgets);
    expect(find.text('Logs'), findsWidgets);
    expect(find.text('Settings'), findsWidgets);
  });
}
