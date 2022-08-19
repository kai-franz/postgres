$HOME/my_postgres/bin/pg_ctl -D $HOME/my_postgres/data -l logfile stop
make world-bin
make install-world-bin
rm -r ~/my_postgres/data
$HOME/my_postgres/bin/initdb -D $HOME/my_postgres/data
./start.sh
