# GirottOS-ExploreR

A Linux file manager written in C for my future Arch based Linux distribution (GirottOS), featuring a familiar desktop file management experience inspired by Windows Explorer.
Even if I said before that was specifically for my future distro I want all GirottOS' software available for other systems rather than mine.

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

* Linux distribution
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
chmod +x build.sh
./build.sh

./build/myfm
```
(strange codename, I know)


## License

This project is licensed under the **Apache License 2.0**.

You may use, modify, distribute and contribute to the project according to the terms of the license.

See the [`LICENSE`](LICENSE) file for the complete license text.
