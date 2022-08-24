./stop.sh
(cd "$HOME"/postgres_main && make world-bin)
(cd "$HOME"/postgres_main && make install-world-bin)
rm -r ~/postgres/data
"$HOME"/postgres/bin/initdb -D "$HOME"/postgres/data
./start_pg.sh
