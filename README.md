yank
====

Yank terminal output to clipboard.

![yank](https://raw.githubusercontent.com/mptre/yank/gh-pages/screencast.gif)

Description
-----------

The yank command reads from `stdin` and displays a selection interface that
allows the user to select a field and copy it to the clipboard.  Fields are
determined by splitting the input on a delimiter sequence, optionally 
specified using the `-d` option.  New line, carriage return, and tab characters
are always treated as delimiters.

Using the `Ctrl-N` and `Ctrl-P` keys will move the field selection forward and
backward. The interface supports several Emacs like key bindings, consult the
man page for more information. Pressing the return key will invoke the yank 
command and write the selected field to its `stdin`. The yank command defaults 
to xsel[1] but can use anything that accepts input on `stdin`. When invoking 
yank everything supplied after the `--` option will be used as the yank 
command, as shown in the examples below. The default yank command can also be
specified at compile time, as explained in the compilation section below.

Motivation
----------

Some people consider it a cache miss when they need to use their mouse.  Copying
output from the terminal is one situation where many people still need to use
the mouse.  Several terminal multiplexers solve this issue, but a terminal 
agnostic solution is preferable.

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
