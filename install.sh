"$HOME"/my_postgres/bin/pg_ctl -D $HOME/my_postgres/data -l logfile stop
make world-bin
make install-world-bin
rm -r ~/my_postgres/data
"$HOME"/my_postgres/bin/initdb -D $HOME/my_postgres/data
cp postgresql.conf ~/my_postgres/data/postgresql.conf
./start.sh
"$HOME"/my_postgres/bin/psql -d postgres -c "create user admin with superuser password 'password';"
"$HOME"/my_postgres/bin/psql -d postgres -c "create database benchbase;"