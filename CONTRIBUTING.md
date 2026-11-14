# Contributing

Use the pinned upstream and build procedure in [BUILDING.md](BUILDING.md).
Keep nano behavior changes narrow and explain any intentional compatibility
changes. Put independent fork components in their own C modules and keep
upstream copyright notices. New code uses GPL-3.0-or-later.

For a behavior change, include steps to reproduce it, the terminal and
TERM value, and relevant PTY coverage. Run the regression suite in a
UTF-8 locale. Changes to buffer, resize or directory code should also
run under AddressSanitizer and UndefinedBehaviorSanitizer.
