# macXtract

Decompiles the keyboard layouts installed on your Mac into editable human readable `.keylayout` files.

macOS stores its keyboard layouts in a compiled binary format. macXtract reads the layouts installed on your Mac and converts each one back into `.keylayout` XML (the human readable format), and writes one file per layout.

It's a single executable with no dependencies: no Xcode, no Apple developer tools.

## Requirements

- macOS 11 (Big Sur) or later
- Apple Silicon or Intel

## Install

1. Download `macxtract.zip` from the [Releases](../../releases) page and unzip it.
2. Allow it to run in one of two ways:
   - remove the quarantine label:

     ```sh
     xattr -d com.apple.quarantine macxtract
     ```

   - go to settings and allow it:
     1. Run `./macxtract`
     2. Open **System Settings -> Privacy & Security**, scroll to Security click **Open Anyway**
     3. Run `./macxtract` again

3. Move it somewhere on your PATH:

   ```sh
   sudo mkdir -p /usr/local/bin
   sudo mv macxtract /usr/local/bin/
   # or run it with './' prefix from wherever the executable is located
   ```

## Usage

```sh
macxtract # when on your path
./macxtract # when not on your path
```

Writes every installed layout to `./decompiled`.

### Flags

| Flag       | Description                                                                                 |
| ---------- | ------------------------------------------------------------------------------------------- |
| `-h`       | Lists all flags and how to use them.                                                        |
| `-v`       | Prints the version.                                                                         |
| `-o <dir>` | Writes to `<dir>` instead of `./decompiled`. Asks to create the folder if it doesn't exist. |
| `-s`       | Strict mode: writes XML 1.0 and strips the control characters it doesn't allow (see below). |
| `-c`       | Leaves out the header comment.                                                              |

Flags can be combined:

```sh
macxtract -sc -o layouts
```

## Output

- One file per layout, named by its Apple ID, fx. `com.apple.keylayout.Danish.keylayout`.
- Layouts that macOS lists more than once with identical data are written once.
- Files are **XML 1.1** by default. Some layouts output control characters (arrow keys, escape, function keys), which XML 1.0 doesn't allow.
- With `-s`, those keys lose their output. Use `-s` files for parsing and tooling, not for installing as a keyboard layout.
- The files match Apple's own decompiled output. Validating them against the DTD in `/System/Library/DTDs/KeyboardLayout.dtd` reports the same errors as Apple's files do: that DTD predates the `baseMapSet` and `baseIndex` attributes, and it doesn't accept the numeric table ids Apple uses.

## License

[MIT LICENSE.](LICENSE)
