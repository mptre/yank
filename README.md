# yank

Yank terminal output to clipboard.

![yank](https://raw.githubusercontent.com/mptre/yank/gh-pages/screencast.gif)

## Description

The
[yank(1)][yank]
utility reads input from `stdin` and display a selection interface that allows a
field to be selected and copied to the clipboard.
Fields are either recognized by a regular expression using the `-g` option or by
splitting the input on a delimiter sequence using the `-d` option.

Using the arrow keys will move the selected field.
The interface supports several Emacs and Vi like key bindings,
consult the man page for further reference.
Pressing the return key will invoke the yank command and write the selected
field to its `stdin`.
The yank command defaults to
[xsel(1)][xsel]
but could be anything that accepts input on `stdin`.
When invoking yank,
everything supplied after the `--` option will be used as the yank command,
see examples below.

## Motivation

Others including myself consider it a cache miss when resort to using the mouse.
Copying output from the terminal is still one of the few cases where I still use
the mouse.
Several terminal multiplexers solves this issue,
however I don't want to be required to use a multiplexer but instead use a
terminal agnostic solution.

## Examples

- Yank an environment variable key or value:

  ```sh
  env | yank -d =
  ```

- Yank a field from a CSV file:

  ```sh
  yank -d \", <file.csv
  ```

- Yank a whole line using the `-l` option:

  ```sh
  make 2>&1 | yank -l
  ```

- If `stdout` is not a terminal the selected field will be written to `stdout`
  and exit without invoking the yank command.
  Kill the selected PID:

  ```sh
  ps ux | yank -g [0-9]+ | xargs kill
  ```

- Yank the selected field to the clipboard as opposed of the default primary
  clipboard:

  ```sh
  yank -- xsel -b
  ```

## Installation

### Arch Linux

On AUR:

```sh
yaourt -S yank
```

### Debian

On testing and unstable:

```sh
sudo apt-get install yank
```

The binary is installed at `/usr/bin/yank-cli` due to a naming conflict.

### Fedora

Versions 24/25/26/Rawhide:

```sh
sudo dnf install yank
```

The binary is installed at `/usr/bin/yank-cli` due to a naming conflict.
Man-pages are available as both `yank` and `yank-cli`.

### FreeBSD

```sh
pkg install yank
```

### Nix/NixOS

```sh
nix-env -i yank
```

### macOS

```sh
brew install yank
```

### OpenBSD

```sh
pkg_add yank
```

### From source

The install directory defaults to `/usr/local`:

```sh
make install
```

Change the install directory using the `PREFIX` variable:

```sh
make PREFIX=DIR install
```

The default yank command can be defined using the `YANKCMD` variable.
For instance,
macOS users would prefer `pbcopy(1)`:

```sh
make YANKCMD=pbcopy
```

## License

Copyright (c) 2018 Anton Lindqvist.
Distributed under the MIT license.

[xsel]: http://www.vergenet.net/~conrad/software/xsel/
[yank]: https://mptre.github.io/yank/
