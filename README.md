# GirottOS-ExploreR

An Arch Linux file manager written in C for my future distribution (GirottOS), featuring a familiar desktop file management experience inspired by Windows Explorer.
Even if I said before that was specifically for my future distro I want all GirottOS' software available for other systems rather than mine (arch based).

> **Beta software**
>
> This project is currently in **beta**. It is functional and actively developed, but bugs, missing features and unexpected behavior may still occur.
>
> Use it with caution, especially when performing file operations on important data.


## Disclaimer

*Wayland ONLY* running this on X11 or other desktop environments may be impossible or cause damage. 

This software is provided during its beta stage and may contain bugs.

File managers perform operations directly on the filesystem. Always keep backups of important data and use beta versions with appropriate caution.

The developer is not responsible for data loss, filesystem damage or other issues resulting from the use of this software.


## Bug Reports

Because this is a beta release, bug reports are especially useful.

When opening an issue, please include:

* Arch distribution
* Architecture
* File manager version/commit
* Steps to reproduce the problem
* Expected behavior
* Actual behavior
* Relevant terminal output or logs
* Screenshots, (I beg you)


## Build

*HIGHLY RECOMMENDED*

```bash
cd current
chmod +x build.sh
./build.sh

./ExploreR
```

`build.sh` checks for GTK 4 (>= 4.12), the Tela icon theme (installed through
`yay`/`paru` if missing) and, optionally, poppler-glib for PDF previews.
(strange codename, I know)


## Configuration

Settings (theme, icons, window size, view options) are saved in `settings.ini`
next to the `ExploreR` executable. If that folder is not writable (for example
when installed in `/usr/bin`), `~/.config/explorer/settings.ini` is used
instead. The file can be edited by hand or through the Settings window (Ctrl+,).


## Credits

* **[Tela icon theme](https://github.com/vinceliuice/Tela-icon-theme)** by
  [vinceliuice](https://github.com/vinceliuice), licensed under the GPL-3.0.
  ExploreR uses it for its file, folder and drive icons (only inside the file
  manager, your desktop icon theme is not changed). `build.sh` installs it with
  `yay` or `paru` (`tela-icon-theme` on the AUR) when it is missing. Without it,
  or with *Settings → Icons → Emoji*, emoji icons are shown instead.


## License

This project is licensed under the **Apache License 2.0**.

You may use, modify, distribute and contribute to the project according to the terms of the license.

See the [`LICENSE`](LICENSE) file for the complete license text.
