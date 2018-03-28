#!/bin/sh
set -x
# WARNING: this script doesnt check for errors, so you have to enhance it in case any of the comands below fail.
lsmod
rmmod sys_xdedup
insmod sys_xdedup.ko
lsmod
