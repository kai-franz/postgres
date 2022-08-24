"$HOME"/postgres/bin/pg_ctl -D $HOME/postgres/data -l logfile stop
make world-bin
make install-world-bin
rm -r ~/postgres/data
"$HOME"/postgres/bin/initdb -D $HOME/postgres/data
./start.sh
