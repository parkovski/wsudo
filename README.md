# `stdo`: Proof of concept sudo for Windows

## ⚠ Not production ready!
This is a very rough proof of concept, intended to show that it can be done. It is buggy, insecure, and missing many features you might want from a sudo equivalent. Use at your own risk!

## What does it look like?
A terminal. Or two terminals currently. [See this short demo](https://raw.githubusercontent.com/parkovski/stdo/assets/demo.mp4).

## How to build/run?
Currently the only dependencies are spdlog and the Windows SDK. Note that if you are using vcpkg, spdlog requires an external fmt install but is not yet compatible with fmt v5, so you'll have to downgrade the port file. For convenience, the last working version of fmt was added at commit `178517052f42d428bb2f304946e635d3c1f318e9`. The project builds with CMake - I use the Ninja generator but the VS one is probably fine. After making sure your spdlog and fmt work, do:

```powershell
...\stdo> mkdir build
...\stdo> cd build
...\stdo> cmake -G Ninja ..
...\stdo> cmake --build .
```

This will produce two binaries in `bin\debug`. To try it, start `TokenServer.exe` in an admin console; then in a separate unelevated console run `stdo.exe <program> <args>`. Currently you need to provide the full path to the program. It will ask for your password, but this is not yet implemented so the password is always `password`. To see the difference in elevation status, try `stdo.exe C:\Windows\System32\whoami.exe /groups` and look for the `Mandatory Label` section.

**Note:** This will probably only build with Visual C++, and probably only 2017 or later due to use of C++17. You can use CQuery for autocompletion, but you will need to add `-DDECLSPEC_IMPORT=` to the options - it seems Clang doesn't support constexpr imported symbols, which are used frequently for RAII Windows `HANDLE` objects. See `class Handle` in `include/stdo/winsupport.h`.

## Why is it called stdo?
Unix systems use a user/group based access model. Windows uses access tokens. `sudo` stands for "set user do"; `stdo` stands for "set token do." Also because if Microsoft ever releases a sudo command, I don't want them to conflict.

## What makes this one different?
It uses a token server, which can be run as a system service, to remotely reassign the primary token for an interactive process. A process you create with the `stdo.exe` command inherits the environment as if you just called the target command itself, but it starts elevated with no UAC involvement.

## How?
There are three ways to create an elevated process:
1. Request elevation with UAC.
2. Be an executable signed by the Windows Publisher.
3. Be an elevated process.

The system will automatically start services elevated, but they have their own environment, which is not very useful for command line purposes. However, there's a trick - you can start a regular restricted process suspended in your own session and notify the service, which uses `NtSetInformationProcess` to change the remote process token to an elevated one before it starts.

I originally created a remote process in the service, but setting up the environment is tricky and requires digging through undocumented parts of the PEB. With this method, the system sets up all the inheritance correctly, and we only need one undocumented call to elevate the process.

## What features are missing?
Most of them. Here are the big ones:
- Actually check the user's password. Surprisingly using "password" for everyone is not very secure.
- Create a token for the client user instead of just duplicating the server's token.
- Cache the users' tokens for a while after a successful authentication (note: should be per-session).
- Implement Windows service functionality for the server.
- Create some type of "stdoers" config file or registry key and enforce permissions.
- Improve the client's command line handling - shouldn't have to type the full path to an exe.
- Improve error handling and write tests.
- Make the server handle multiple clients at a time. It already works as an async event loop so this should be fairly easy.

### Other ideas
- Options to set user and privileges.
- PowerShell wrapper cmdlets.
- Integration with WSL sudo.

## Additional resources
* [OpenSSH's usage of LSA](https://github.com/PowerShell/openssh-portable/blob/latestw_all/contrib/win32/win32compat/win32_usertoken_utils.c)
