#!/bin/bash

# See https://github.com/Debian/debhelper/blob/5d1bb29841043d8e47ebbdd043e6cd086cad508e/dh_strip#L362-L384
# for what dh_strip removes.

strip --remove-section=.comment --remove-section=.note "$@"
