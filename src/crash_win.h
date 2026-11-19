// eatlead - crash reports on Windows.
#pragma once

namespace eatlead::crash {

// On an unhandled exception (or abort), writes logs\crash-<pid>.txt (exception, faulting
// address, host stack as module+offset) and logs\crash-<pid>.dmp (minidump) next to
// eatlead.exe. The launcher's "Save crash report" collects them.
void Install();

}  // namespace eatlead::crash
