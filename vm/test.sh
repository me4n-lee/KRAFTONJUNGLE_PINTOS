make clean
make
cd build/
clear

pintos -v -k -T 60 -m 20   --fs-disk=10 -p tests/userprog/args-multiple:args-multiple --swap-disk=4 -- -q   -f run 'args-multiple some arguments for you!'
cd ..