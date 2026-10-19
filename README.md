# `wsudo`: UAC-less sudo for Windows

## Why another sudo for Windows?
Every other sudo project I've seen requires at least one UAC prompt. I haven't
tried the official one from Microsoft, but it appears to be abandoned. This
sudo runs a system service to assign new tokens to a process and therefore you
never see a UAC prompt. Seriously. Never.

## ⚠ Not production ready!
This project is in the very early stages. It may have bugs and security holes.
Use at your own risk!

## How to build/run?
This project relies on another of my projects,
[wscoro](https://github.com/parkovski/wscoro). To build, you need to clone that
project in a sibling directory to this one. After that, you can install the
dependencies with vcpkg and build with CMake/Ninja.

Make sure your `VCPKG_ROOT` points to a valid location, and then do:

```powershell
...\wsudo> cmake --preset=x64 # Or arm64 if that's your platform.
...\wsudo> cmake --build build
```

This will produce three binaries in `build\bin`. From an elevated shell, run
`./TokenServer.exe install && net localgroup wsudoers <username> /add`. Then
log out and log in to refresh your groups.

Now you can run `wsudo whatever-you-want`. It will ask for your password, after
which you have a 5 minute window of no repeat password prompts. This window is
reset each time you run `wsudo`. After 5 minutes of inactivity your session
times out and you will have to enter your password again.

You can symlink to `wsudo.exe`, but it must live in the same directory as
`wsudo-shim.exe`. Some programs work without the shim, but some don't, so the
default is to use it always. Note that most of the command line options for
`wsudo.exe` are not implemented yet.

## How does it work?
There are three ways to create an elevated process:
1. Request elevation with UAC.
2. Be an executable signed by the Windows Publisher.
3. Be an elevated process.

The system will automatically start services elevated, but they have their own
environment, which is not very useful for command line purposes. However,
there's a trick - you can start a regular restricted process suspended in your
own session and notify the service, which uses `NtSetInformationProcess` to
change the remote process token to an elevated one before it starts.

I originally created a remote process in the service, but setting up the
environment is tricky and requires digging through undocumented parts of the
PEB. With this method, the system sets up all the inheritance correctly, and
we only need one undocumented call to elevate the process.

## What features are missing?
wsudo is coming along quickly, but some features are still missing. Here are
the big ones:
- Create some type of "sudoers" config file or registry key and enforce
    permissions.
- Improve error handling and write tests.

### Other ideas
- Options to set user and privileges. Logging in as another user without their
    password requires using `NtCreateToken` which is some dark magic, so this
    is a big maybe.
- PowerShell wrapper cmdlets.
- Integration with WSL sudo.

## Why the GPL?
I believe that security software should be open and auditable. If I went with
something like MIT, people could clone this and keep their changes to
themselves, which I don't want. That said, if you want to use part of this
project with GPL-incompatible software, that's probably ok with me - just ask.
