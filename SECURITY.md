# iSH is not a security boundary!

The goal of this project is to support a Linux shell on iOS. As such, its security model assumes that the app is running in another sandbox and is used by a single user. The project is focused on compatibility, and very little thought has been put into internal security. Permissions are only loosely checked. Memory corruption in edge cases is common. Please do not use iSH for any sort of secure containerization or production use case.

As such, most types of bugs that are security issues in most projects are not security issues in iSH. Insufficient permission checks, memory corruption, and thread safety issues are generally considered correctness bugs and would be best filed as GitHub issues. We will prioritize bugs encountered by real programs in typical use.

In our security model, we expect real security bugs to be very rare. It's not completely impossible, e.g. a bug allowing remote code execution without user consent would be a security bug. If you think you found one, you can send it to security@ish.app. We'll work with you to resolve it appropriately.
