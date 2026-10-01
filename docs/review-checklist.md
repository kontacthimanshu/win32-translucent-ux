# Code review checklist

Checks a reviewer runs on every change, in addition to the Core Principles and the
Release-Readiness gate in `.specify/memory/constitution.md` (Compliance Review).

## Motion (T082; spec US5 scenario 4, research R-05)

- [ ] The app has no animations today: hover, pressed, selection and focus states change at
      once. A change that adds an animation or a timed transition (DirectComposition
      animations, `SetTimer`-driven fades, `IUIAnimationManager`, animated scrolling, ...)
      MUST check `RenderingCapabilities::animationsEnabled` (Windows *Settings >
      Accessibility > Visual effects > Animation effects*, `SPI_GETCLIENTAREAANIMATION`)
      and, while it is false, show the end state immediately.
- [ ] The setting is read again when it changes (`WM_SETTINGCHANGE` with
      `SPI_SETCLIENTAREAANIMATION` -> `MainWindow::RefreshAppearance`); the animation reads
      the current value, not one cached at startup.

## Live OS settings (T082, quickstart V-5e)

- [ ] New text uses the formats from `TextFormats` (rebuilt for the Windows text size), and
      new fixed heights for text rows are multiplied by the text scale, like
      `FileView::RowHeight()`.
- [ ] New colors come from `EffectiveAppearance` (re-resolved on light/dark, accent and high
      contrast changes), never from constants.
