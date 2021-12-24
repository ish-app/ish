#!/bin/sh

# Try to figure out the user's PATH to pick up their installed utilities.
export PATH="$PATH:$($(sudo -u "$USER" -i echo '$SHELL') -l -c 'echo "$PATH"')"

ninja "$@"
