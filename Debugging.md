# Debugging Tips, Tricks, and Notes

## Decompiling a file

We can use the followig command to decompile an compiled file. It works much better if built with `--dbg`.

`objdump -M intel -C -D -l  <.app> > dump.txt`