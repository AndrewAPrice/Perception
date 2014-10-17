 #!/bin/bash
 ./build.sh

qemu-system-x86_64 -cdrom Perception.iso -d int,cpu_reset
