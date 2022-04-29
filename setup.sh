sudo apt update
sudo apt install make llvm clang lib32readline-dev zlib1g-dev flex bison
./configure --with-llvm --prefix=$HOME/my_postgres2 --without-readline