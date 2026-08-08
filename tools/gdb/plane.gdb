# Plane GDB helper.
#
# Usage:
#   make qemu-gdb
#   gdb -q -x tools/gdb/plane.gdb
#
# Build with `make debug` or `make debug-iso` to get source lines, local
# variables, and type information.

set pagination off
set confirm off
set architecture i386:x86-64

file plane.elf
target remote :1234

# Common starting points:
#   break kmain
#   break panic
#   continue
