yank
====

Yank terminal output to clipboard.

![yank](https://raw.githubusercontent.com/mptre/yank/gh-pages/screencast.gif)

Description
-----------

Read input from `stdin` and draw a selection interface where all fields in the
given input is recognized by using a given set of delimiters which defaults to
space (new line and tab characters is always treated as delimiters). The
delimiters is a sequence of characters represented as a string and can be
overwritten using the `-d` option.

Using the `Ctrl-N` and `Ctrl-P` keys will move the field selection forward and
backward. The interface support several Emacs like key bindings, consult the man
page for further reference. Pressing the return key will invoke the yank command
and write the selected field to its `stdin`. The yank command defaults to
xsel[1] but could be anything that accepts input on `stdin`. When invoking yank
everything supplied after the `--` option will be used as the yank command, see
examples below. The default yank command can also be defined at compile time,
see compilation below.

Motivation
----------

Others including myself consider it a cache miss when resort to using the mouse.
Copying output from the terminal is still one of the few cases where I still use
the mouse. Several terminal multiplexers solves this issue, however I don't want
to be required to use a multiplexer but instead use a terminal agnostic
solution.

Examples
--------

  - Yank a environment variable key or value:

    ```
    env | yank -d =
    ```

  - Yank a field from a CSV file:

    ```
    yank -d \", <file.csv
    ```

  - Yank a whole line using the `-l` option:

    ```
    make 2>&1 | yank -l
    ```

  - If `stdout` is not a terminal the selected field will be written to `stdout`
    and exit without invoking the yank command:

    ```
    yank | cat
    ```

  - Yank the selected field to the clipboard as opposed of the default primary
    clipboard:

    ```
    yank -- xsel -b
    ```

Compilation
-----------

Run the following command:

  ```
  make
  ```

The default yank command can be defined using the `YANKCMD` environment
variable. Example: OS X users would use `pbcopy` as the default yank command:

  ```
  YANKCMD=pbcopy make
  ```

Alternatively put the `YANKCMD` variable declaration in your local `config.mk`
file.

Installation
------------

The install directory defaults to `/usr/local`:

  ```
  make install
  ```

Change the install directory using the `PREFIX` environment variable:

  ```
  PREFIX=DIR make install
  ```

[1] http://www.vergenet.net/~conrad/software/xsel/
