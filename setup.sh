sudo apt update
sudo apt install make
sudo apt install llvm
sudo apt install clang
sudo apt install lib32readline-dev
sudo apt-get install zlib1g-dev
sudo apt-get install flex bison
./configure --with-llvm --prefix=$HOME/my_postgres --without-readline