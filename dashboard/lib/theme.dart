import 'package:flutter/material.dart';

/// Design system: neutral monochromatic palette (grayscale/black/white).
///
/// Constraints (from spec):
///  * Accent colour is permitted ONLY for critical status indicators.
///  * No vivid colours (red/yellow/green/blue neon) — accent is dark gray
///    for "active" and light gray for "inactive".
///  * No transparency / glassmorphism — every surface is fully opaque
///    (opacity = 1.0).
///  * Material 3 typography hierarchy (Headline, Title, Body, Label).
///  * 8dp grid system.
class HarnessPalette {
  HarnessPalette._();

  // Grayscale ramp (Material-neutral inspired).
  static const Color black = Color(0xFF000000);
  static const Color gray900 = Color(0xFF111111);
  static const Color gray800 = Color(0xFF1C1C1E);
  static const Color gray700 = Color(0xFF2C2C2E);
  static const Color gray600 = Color(0xFF3A3A3C);
  static const Color gray500 = Color(0xFF48484A);
  static const Color gray400 = Color(0xFF636366);
  static const Color gray300 = Color(0xFF8E8E93);
  static const Color gray200 = Color(0xFFC7C7CC);
  static const Color gray100 = Color(0xFFE5E5EA);
  static const Color gray50 = Color(0xFFF2F2F7);
  static const Color white = Color(0xFFFFFFFF);

  // Status accents — the ONLY place a non-neutral tone may appear.
  // Kept intentionally dark gray (active) vs light gray (inactive) per spec.
  static const Color statusActive = gray800;    // active profile
  static const Color statusInactive = gray300;  // inactive profile
  static const Color statusCritical = gray900;  // error/attention (near-black)
}

/// Light theme — bright neutral surfaces, solid.
ThemeData buildLightTheme() {
  final scheme = ColorScheme(
    brightness: Brightness.light,
    primary: HarnessPalette.gray900,
    onPrimary: HarnessPalette.white,
    secondary: HarnessPalette.gray600,
    onSecondary: HarnessPalette.white,
    error: HarnessPalette.gray900,
    onError: HarnessPalette.white,
    surface: HarnessPalette.white,
    onSurface: HarnessPalette.gray900,
    // Surface containers (M3 tonal surfaces) mapped to neutral ramp.
    surfaceContainerHighest: HarnessPalette.gray50,
    surfaceContainerHigh: HarnessPalette.gray100,
    surfaceContainer: HarnessPalette.gray100,
    surfaceContainerLow: HarnessPalette.gray50,
    surfaceContainerLowest: HarnessPalette.white,
    outline: HarnessPalette.gray300,
    outlineVariant: HarnessPalette.gray200,
    shadow: HarnessPalette.black,
    scrim: HarnessPalette.black,
    inverseSurface: HarnessPalette.gray900,
    onInverseSurface: HarnessPalette.white,
    inversePrimary: HarnessPalette.gray200,
  );

  final base = ThemeData(
    useMaterial3: true,
    colorScheme: scheme,
    scaffoldBackgroundColor: HarnessPalette.white,
    // No transparency anywhere: force full opacity on scaffold & surfaces.
    canvasColor: HarnessPalette.white,
    splashFactory: NoSplash.splashFactory, // remove ripple transparency
    visualDensity: VisualDensity.standard,
  );

  return base.copyWith(
    appBarTheme: const AppBarTheme(
      backgroundColor: HarnessPalette.white,
      foregroundColor: HarnessPalette.gray900,
      elevation: 0,
      scrolledUnderElevation: 0,
      surfaceTintColor: HarnessPalette.white,
      centerTitle: false,
      titleTextStyle: TextStyle(
        fontFamily: 'Roboto',
        fontSize: 22,
        fontWeight: FontWeight.w600,
        color: HarnessPalette.gray900,
      ),
    ),
    cardTheme: const CardThemeData(
      color: HarnessPalette.white,
      surfaceTintColor: HarnessPalette.white,
      elevation: 0,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.all(Radius.circular(12)),
        side: BorderSide(color: HarnessPalette.gray200, width: 1),
      ),
      margin: EdgeInsets.zero,
    ),
    elevatedButtonTheme: ElevatedButtonThemeData(
      style: ElevatedButton.styleFrom(
        backgroundColor: HarnessPalette.gray900,
        foregroundColor: HarnessPalette.white,
        elevation: 0,
        minimumSize: const Size(64, 48),
        padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 12),
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(8),
        ),
        textStyle: const TextStyle(
          fontSize: 14,
          fontWeight: FontWeight.w600,
          letterSpacing: 0.1,
        ),
      ),
    ),
    filledButtonTheme: FilledButtonThemeData(
      style: FilledButton.styleFrom(
        backgroundColor: HarnessPalette.gray900,
        foregroundColor: HarnessPalette.white,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(8),
        ),
      ),
    ),
    outlinedButtonTheme: OutlinedButtonThemeData(
      style: OutlinedButton.styleFrom(
        foregroundColor: HarnessPalette.gray900,
        side: const BorderSide(color: HarnessPalette.gray300, width: 1),
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(8),
        ),
      ),
    ),
    inputDecorationTheme: InputDecorationTheme(
      filled: true,
      fillColor: HarnessPalette.gray50, // solid, no opacity
      contentPadding: const EdgeInsets.symmetric(horizontal: 16, vertical: 14),
      border: OutlineInputBorder(
        borderRadius: BorderRadius.circular(8),
        borderSide: const BorderSide(color: HarnessPalette.gray300),
      ),
      enabledBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(8),
        borderSide: const BorderSide(color: HarnessPalette.gray300),
      ),
      focusedBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(8),
        borderSide: const BorderSide(color: HarnessPalette.gray900, width: 2),
      ),
      labelStyle: const TextStyle(color: HarnessPalette.gray500),
      hintStyle: const TextStyle(color: HarnessPalette.gray400),
    ),
    bottomNavigationBarTheme: const BottomNavigationBarThemeData(
      backgroundColor: HarnessPalette.white,
      selectedItemColor: HarnessPalette.gray900,
      unselectedItemColor: HarnessPalette.gray300,
      elevation: 0,
      type: BottomNavigationBarType.fixed,
      selectedLabelStyle: TextStyle(fontSize: 12, fontWeight: FontWeight.w600),
      unselectedLabelStyle: TextStyle(fontSize: 12),
    ),
    dividerTheme: const DividerThemeData(
      color: HarnessPalette.gray100,
      thickness: 1,
      space: 1,
    ),
    dialogTheme: const DialogThemeData(
      backgroundColor: HarnessPalette.white,
      surfaceTintColor: HarnessPalette.white,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.all(Radius.circular(12)),
      ),
    ),
    snackBarTheme: SnackBarThemeData(
      backgroundColor: HarnessPalette.gray900,
      contentTextStyle: const TextStyle(color: HarnessPalette.white),
      behavior: SnackBarBehavior.floating,
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
    ),
  );
}

/// Dark theme — near-black neutral surfaces, solid.
ThemeData buildDarkTheme() {
  final scheme = ColorScheme(
    brightness: Brightness.dark,
    primary: HarnessPalette.white,
    onPrimary: HarnessPalette.gray900,
    secondary: HarnessPalette.gray300,
    onSecondary: HarnessPalette.gray900,
    error: HarnessPalette.gray300,
    onError: HarnessPalette.gray900,
    surface: HarnessPalette.gray900,
    onSurface: HarnessPalette.white,
    surfaceContainerHighest: HarnessPalette.gray700,
    surfaceContainerHigh: HarnessPalette.gray700,
    surfaceContainer: HarnessPalette.gray800,
    surfaceContainerLow: HarnessPalette.gray800,
    surfaceContainerLowest: HarnessPalette.gray900,
    outline: HarnessPalette.gray500,
    outlineVariant: HarnessPalette.gray600,
    shadow: HarnessPalette.black,
    scrim: HarnessPalette.black,
    inverseSurface: HarnessPalette.white,
    onInverseSurface: HarnessPalette.gray900,
    inversePrimary: HarnessPalette.gray700,
  );

  final base = ThemeData(
    useMaterial3: true,
    colorScheme: scheme,
    scaffoldBackgroundColor: HarnessPalette.gray900,
    canvasColor: HarnessPalette.gray900,
    splashFactory: NoSplash.splashFactory,
    visualDensity: VisualDensity.standard,
  );

  return base.copyWith(
    appBarTheme: const AppBarTheme(
      backgroundColor: HarnessPalette.gray900,
      foregroundColor: HarnessPalette.white,
      elevation: 0,
      scrolledUnderElevation: 0,
      surfaceTintColor: HarnessPalette.gray900,
      centerTitle: false,
      titleTextStyle: TextStyle(
        fontFamily: 'Roboto',
        fontSize: 22,
        fontWeight: FontWeight.w600,
        color: HarnessPalette.white,
      ),
    ),
    cardTheme: const CardThemeData(
      color: HarnessPalette.gray800,
      surfaceTintColor: HarnessPalette.gray800,
      elevation: 0,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.all(Radius.circular(12)),
        side: BorderSide(color: HarnessPalette.gray700, width: 1),
      ),
      margin: EdgeInsets.zero,
    ),
    elevatedButtonTheme: ElevatedButtonThemeData(
      style: ElevatedButton.styleFrom(
        backgroundColor: HarnessPalette.white,
        foregroundColor: HarnessPalette.gray900,
        elevation: 0,
        minimumSize: const Size(64, 48),
        padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 12),
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
        textStyle: const TextStyle(fontSize: 14, fontWeight: FontWeight.w600),
      ),
    ),
    inputDecorationTheme: InputDecorationTheme(
      filled: true,
      fillColor: HarnessPalette.gray800,
      contentPadding: const EdgeInsets.symmetric(horizontal: 16, vertical: 14),
      border: OutlineInputBorder(
        borderRadius: BorderRadius.circular(8),
        borderSide: const BorderSide(color: HarnessPalette.gray600),
      ),
      enabledBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(8),
        borderSide: const BorderSide(color: HarnessPalette.gray600),
      ),
      focusedBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(8),
        borderSide: const BorderSide(color: HarnessPalette.white, width: 2),
      ),
      labelStyle: const TextStyle(color: HarnessPalette.gray400),
      hintStyle: const TextStyle(color: HarnessPalette.gray500),
    ),
    bottomNavigationBarTheme: const BottomNavigationBarThemeData(
      backgroundColor: HarnessPalette.gray900,
      selectedItemColor: HarnessPalette.white,
      unselectedItemColor: HarnessPalette.gray500,
      elevation: 0,
      type: BottomNavigationBarType.fixed,
      selectedLabelStyle: TextStyle(fontSize: 12, fontWeight: FontWeight.w600),
      unselectedLabelStyle: TextStyle(fontSize: 12),
    ),
    dividerTheme: const DividerThemeData(color: HarnessPalette.gray700, thickness: 1),
    dialogTheme: const DialogThemeData(
      backgroundColor: HarnessPalette.gray800,
      surfaceTintColor: HarnessPalette.gray800,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.all(Radius.circular(12)),
      ),
    ),
    snackBarTheme: SnackBarThemeData(
      backgroundColor: HarnessPalette.gray300,
      contentTextStyle: const TextStyle(color: HarnessPalette.gray900),
      behavior: SnackBarBehavior.floating,
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
    ),
  );
}
