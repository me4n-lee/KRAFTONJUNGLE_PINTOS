make clean
make
cd build/
clear

# pintos --fs-disk=10 -p tests/userprog/args-single:args-single -- -q -f run 'args-single onearg'
pintos -v -k -T 600 -m 20 -m 20   --fs-disk=10 -p tests/userprog/no-vm/multi-oom:multi-oom -- -q   -f run multi-oom
make tests/userprog/no-vm/multi-oom.result

cd ..