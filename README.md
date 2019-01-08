# `wsudo`: Proof of concept sudo for Windows

## ⚠ Not production ready!
This project is in the very early stages. It may have bugs and security holes. Use at your own risk!

## What does it look like?
A terminal. Or two terminals currently. [See this short demo](https://raw.githubusercontent.com/parkovski/wsudo/assets/demo.mp4).

## How to build/run?
Currently the only dependencies are spdlog, fmt, and the Windows SDK. I use vcpkg to install these. The project builds with CMake - I use the Ninja generator but the VS one is probably fine. After making sure your spdlog and fmt work, do:

```powershell
...\wsudo> mkdir build
...\wsudo> cd build
...\wsudo> cmake -G Ninja ..
...\wsudo> cmake --build .
```

This will produce two binaries in `bin\Debug`. To try it, start `TokenServer.exe` in an admin console; then in a separate unelevated console run `wsudo.exe <program> <args>`. Currently you need to provide the full path to the program. It will ask for your password, but this is not yet implemented so the password is always `password`. To see the difference in elevation status, try `wsudo.exe C:\Windows\System32\whoami.exe /groups` and look for the `Mandatory Label` section.

## What makes this one different?
It uses a token server, which can be run as a system service, to remotely reassign the primary token for an interactive process. A process you create with the `wsudo.exe` command inherits the environment as if you just called the target command itself, but it starts elevated with no UAC involvement.

## How?
There are three ways to create an elevated process:
1. Request elevation with UAC.
2. Be an executable signed by the Windows Publisher.
3. Be an elevated process.

The system will automatically start services elevated, but they have their own environment, which is not very useful for command line purposes. However, there's a trick - you can start a regular restricted process suspended in your own session and notify the service, which uses `NtSetInformationProcess` to change the remote process token to an elevated one before it starts.

I originally created a remote process in the service, but setting up the environment is tricky and requires digging through undocumented parts of the PEB. With this method, the system sets up all the inheritance correctly, and we only need one undocumented call to elevate the process.

It may be possible to achieve this without any undocumented APIs by creating the process in the server and using `PROC_THREAD_ATTRIBUTE_PARENT_PROCESS`.

## What features are missing?
Most of them. Here are the big ones:
- Actually check the user's password. Surprisingly using "password" for everyone is not very secure.
- Create a token for the client user instead of just duplicating the server's token.
- Cache the users' tokens for a while after a successful authentication (note: should be per-session).
- Implement Windows service functionality for the server.
- Create some type of "sudoers" config file or registry key and enforce permissions.
- Improve the client's command line handling - shouldn't have to type the full path to an exe.
- Improve error handling and write tests.

### Other ideas
- Options to set user and privileges.
- PowerShell wrapper cmdlets.
- Integration with WSL sudo.
- COM elevation.
- Session selection.

## Additional resources
* [OpenSSH's usage of LSA](https://github.com/PowerShell/openssh-portable/blob/latestw_all/contrib/win32/win32compat/win32_usertoken_utils.c)
