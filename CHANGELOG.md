v0.8.2 - 2017-02-19
===================

- Ensure `make clean` always exits with success (47a0ee9) (Sébastien Delafond)

v0.8.1 - 2017-02-18
===================

- Add Fedora install instructions to README (#33, #34, 8d90aae) (Nemanja
  Milosevic)

- Allow the name of the compiled binary to be changed using the `PROG` variable
  (#35, #36, 1c43c46) (Nemanja Milosevic)

v0.8.0 - 2017-01-17
===================

- Update man page and README (9eac46f, 252a419) (Anton Lindqvist)

- Add Arch Linux install instructions to README (#32, db259d8) (Javier Tiá)

- Use `install(1)` in Makefile (11a2dc1, 42604e9) (Anton Lindqvist)

- Add support for `DESTDIR` to Makefile (77cf987) (Anton Lindqvist)

v0.7.1 - 2016-10-08
===================

- Update man page (2e78c28, a5cdba9) (Anton Lindqvist)

- Consolidate key bindings (5b4a840) (Anton Lindqvist)

- Fix `snprintf(3)` return value check (9fbf248) (Anton Lindqvist)

- Improve error messages (89be934) (Anton Lindqvist)

- Fix segfault when no fields are found (f3ef1ea) (Anton Lindqvist)

- Add Vi goto `g` and `G` key bindings (859f27b) (Anton Lindqvist)

- Refactoring and cleanup (2017de6) (Anton Lindqvist)

v0.7.0 - 2016-05-18
===================

- Add MANPREFIX support to installation task (Anton Lindqvist)

- Pledge on OpenBSD (Anton Lindqvist)

v0.6.4 - 2016-03-13
===================

- Add Vi key bindings (Samson Yeung)

- Fix wrong exit code on yank command failure (Anton Lindqvist)

v0.6.3 - 2016-02-20
===================

- Exit with correct code on invocation failure (Anton Lindqvist)

- Don't allow yanking if the input doesn't contain any fields (Anton Lindqvist)

- Fix endless loop while executing the supplied regex (Anton Lindqvist)

v0.6.2 - 2015-12-15
===================

- Fix non string literal compilation warning (Anton Lindqvist)

v0.6.1 - 2015-12-15
===================

- Use more common save/restore cursor capabilities (Eli Young)

v0.6.0 - 2015-12-06
===================

- Add regex delimiter support using the `-g` option and `-i` to make the pattern
  case-insensitive (Anton Lindqvist)

v0.5.0 - 2015-11-12
===================

- Add arrow key bindings (Anton Lindqvist)

- Add Debian installation instructions (Sébastien Delafond)

- Add OS X installation instructions (Carl Dong)

v0.4.1 - 2015-10-19
===================

- Fix number of lines in input bug (Anton Lindqvist)

- Fix typo in man page (FND)

- Fix description in man page (Jeremiah LaRocco)

v0.4.0 - 2015-10-10
===================

- Always recognize carriage return as a delimiter (Anton Lindqvist)

- Exit with the same exit code as the yank command on failure (Anton Lindqvist)

- Ensure all data is written to the terminal (Anton Lindqvist)

- Fix number of lines in input bug (Anton Lindqvist)

v0.3.1 - 2015-09-24
===================

- Fix moving beyond input bug (Anton Lindqvist)

v0.3.0 - 2015-09-21
===================

- Add `-x` option used to enable usage of the alternate screen (Anton Lindqvist)

- Fix `style(9)` violations (Anton Lindqvist)

v0.2.0 - 2015-09-10
===================

- Add `-l` option used to yank a whole line (Anton Lindqvist)

- Add `Ctrl-D` key binding which will exit without invoking the yank command
  (Anton Lindqvist)

- Require presence of the `--` option used to separate options and the yank
  command (Anton Lindqvist)

- Fix number of lines in input bug (Anton Lindqvist)

v0.1.1 - 2015-08-28
===================

- Add support for linking against musl (Anton Lindqvist)

v0.1.0 - 2015-08-24
===================

- Initial commit (Anton Lindqvist)
