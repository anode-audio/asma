# asma-scan protocol

`asma-scan` is the out-of-process scanner. The UI starts it, reads its stdout
line by line, and restarts it if it dies. Every line is one JSON object with an
`event` field.

## Invocation

    asma-scan --db PATH --root ID [--threads N] [--fail RELPATH]...

`--fail` marks a root-relative path as failed with the reason "crashed the
scanner" before scanning starts, so the scan skips it until the file changes.

## Events

| event           | fields                                                                      | meaning                             |
| --------------- | --------------------------------------------------------------------------- | ----------------------------------- |
| `marked_failed` | `path`                                                                      | a `--fail` path was recorded        |
| `start`         | `path`                                                                      | a worker began probing this file    |
| `progress`      | `done`, `total`, `path`                                                     | this file finished                  |
| `done`          | `added`, `updated`, `unchanged`, `relinked`, `missing`, `failed`, `skipped` | the scan completed                  |
| `error`         | `code` (`locked` or `failed`), optional `pid`, `message`                    | the scan did not run or did not end |

Paths are root-relative, UTF-8, with `/` separators.

`skipped` counts files that could not be read this time (permissions, a file
that vanished, a drive that went away mid-scan). Their rows are left as they
were and the next scan tries again. `failed` is only for files whose content
could not be parsed; they stay failed until they change.

## Exit codes

0 success, 1 error, 2 usage, 3 another process holds the writer lock.

## Crash recovery (supervisor contract)

Several files are probed in parallel, so a crash cannot be pinned on one file
from the last line alone. The supervisor keeps the set of paths that have a
`start` but no `progress`. When the worker exits without a `done` event, the
supervisor restarts it with `--threads 1`, and when a single-threaded worker
dies, the path it started last is passed back with `--fail`.
