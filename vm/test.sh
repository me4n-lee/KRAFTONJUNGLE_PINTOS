make clean
make
cd build/
clear

# pintos -v -k -T 60 -m 20   --fs-disk=10 -p tests/userprog/read-boundary:read-boundary -p ../../tests/userprog/sample.txt:sample.txt --swap-disk=4 -- -q   -f run read-boundary

# pintos tests/userprog/fork-read.output
# pintos -- -q -f run tests/userprog/fork-read.result

# make tests/userprog/fork-read.output
pintos -v -k -T 60 -m 20   --fs-disk=10 -p tests/userprog/fork-read:fork-read -p ../../tests/userprog/sample.txt:sample.txt --swap-disk=4 -- -q   -f run fork-read
make tests/userprog/fork-read.result

cd ..