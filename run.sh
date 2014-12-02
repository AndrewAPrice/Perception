 #!/bin/bash
 ./build.sh

qemu-system-x86_64 -cdrom Perception.iso -serial stdio # -d cpu_reset,int
