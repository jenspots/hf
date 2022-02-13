# Hostsfile

A command line interface to interact with the hosts file. Easily inspect, add and remove entries individually or in bulk.

### Compatibility

The main target for this program is macOS, but as far as I know, pretty much any UNIX based system should be compatible.

### A work in progress

Please note that this program is not finished and might contain bugs. Always backup your hosts file manually since some operating systems rely on the mapping of domains such as `localhost`.

### Usage

```
HOSTFILE: command line interface for editing hosts files easily.
Copyright (c) by Jens Pots
Licensed under AGPL-3.0-only

IMPORTANT
        Writing to /etc/hosts requires root privileges.

FLAGS
        --verbose               Turn up verbosity.
        --raw                   Don't humanize output.
        --dry-run               Send changes to stdout.

OPTIONS
        -a --add <domain>@<ip>  Add a new entry.
        -l --list               List all current entries.
        -r --remove <domain>    Remove an entry.
        -i --import <path>      Take union with using file.
        -d --delete <path>      Minus set operation using file.
```
