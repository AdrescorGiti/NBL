# Changelog

## 0.4.0

- Add NBL Mentor syntax checks for POSIX sh, Bash, Python, C, and C++
  with local tool output, short explanations, and line-level indicators.
- Add Explorer root navigation and file and directory operations.
- Show the file, language, cursor position, and save state in the status line.
- Add user and system source installers and a manifest-based uninstaller.

## 0.3.0-2

- Create a missing user TOML configuration on first launch or with
  `nbl --init-config`, preserving existing user settings.

## 0.3.0

- Load and validate TOML settings, with F4 to reload.
- Track automatic brackets and quotes through typing and undo.
- Add Python and C/C++ syntax, indentation, and integrated execution.
- Route native compiler errors to Problems.

## 0.2.0

- Run the checked shell buffer in an interactive terminal split with F5.
- Check shell code on open and save. Show errors in the gutter and Problems panel.
- Add clickable coding controls and function-key shortcuts.
- Support buffer selection by dragging and copy it to the terminal clipboard.

## 0.1.0

- Fork GNU nano 9.3 with an independent executable, config and history.
- Add a blue terminal theme, current-line highlight and cursor position.
- Add a right-side filesystem explorer with multiple buffers, keyboard and
  mouse navigation, resize, hidden entries and refresh.
- Add POSIX shell highlighting, syntax checks, optional ShellCheck and
  explicit completion.
- Provide Arch x86-64 packaging and a user-local installation helper.
