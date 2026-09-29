# asma-scan protocol

`asma-scan` is the out-of-process scanner. The UI starts it, reads its stdout
line by line, and restarts it if it dies. Every line is one JSON object with an
`event` field.

A run has two phases. First it indexes the root (probe, hash, file-name
metadata), then it analyses every file in that root that has not been analysed
by the current analyser version (decode, loudness, tempo, key, descriptors).

## Invocation

    asma-scan --db PATH --root ID [--threads N] [--no-analysis]
              [--fail RELPATH]... [--fail-analysis RELPATH]...

`--fail` marks a root-relative path as failed with the reason "crashed the
scanner" before indexing starts, so indexing skips it until the file changes.

`--fail-analysis` marks a path as analysed with the error "crashed the
analyser", so analysis skips it until its content changes. The file stays in
search results.

`--no-analysis` stops after indexing.

## Events

| event                    | fields                                                                      | meaning                               |
| ------------------------ | --------------------------------------------------------------------------- | ------------------------------------- |
| `marked_failed`          | `path`                                                                      | a `--fail` path was recorded          |
| `marked_analysis_failed` | `path`                                                                      | a `--fail-analysis` path was recorded |
| `start`                  | `path`                                                                      | indexing began on this file           |
| `progress`               | `done`, `total`, `path`                                                     | indexing finished this file           |
| `done`                   | `added`, `updated`, `unchanged`, `relinked`, `missing`, `failed`, `skipped` | indexing completed                    |
| `analyse_start`          | `path`                                                                      | analysis began on this file           |
| `analyse_progress`       | `done`, `total`, `path`                                                     | analysis finished this file           |
| `analyse_done`           | `analysed`, `failed`, `skipped`                                             | analysis completed                    |
| `error`                  | `code` (`locked` or `failed`), optional `pid`, `message`                    | the run did not start or did not end  |

Paths are root-relative, UTF-8, with `/` separators.

`skipped` counts files that could not be read this time (permissions, a file
that vanished, a drive that went away mid-scan). Their rows are left as they
were and the next run tries again. `failed` is only for files whose content
could not be parsed or decoded; they are not retried until they change.

## Exit codes

0 success, 1 error, 2 usage, 3 another process holds the writer lock.

## Crash recovery (supervisor contract)

Several files are processed in parallel, so a crash cannot be pinned on one file
from the last line alone. The supervisor keeps the set of paths that have a
`start` (or `analyse_start`) but no matching `progress` (or `analyse_progress`).
When the worker exits without finishing, the supervisor restarts it with
`--threads 1`, and when a single-threaded worker dies, the path it started last
is passed back with `--fail` if it died while indexing, or `--fail-analysis` if
it died while analysing.

`asma::ScanSupervisor` (`core/include/asma/core/ScanSupervisor.h`) implements
this contract; the recovery decisions live in `asma::ScanRecovery`, which is
tested without processes. After a single-threaded run pins a file, the next run
goes back to the requested thread count. A crash that no file explains ends the
scan after two single-threaded tries, and so does a file that crashes the worker
again after it was marked. `error` events end the scan without a retry. A worker
that prints nothing for `ScanRequest::stallTimeout` (120 s by default) is killed
and handled as a crash, so a file that hangs the decoder is marked like one that
crashes it.

On Windows `asma-scan` turns off the crash dialog (`SetErrorMode`) and abort's
report and message box (`_set_abort_behavior`), so a crash or an `abort()` ends
the process at once instead of waiting for someone to dismiss a window.
