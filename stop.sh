echo "Stopping new postgres:"
$HOME/my_postgres/bin/pg_ctl -D $HOME/my_postgres/data -l $HOME/CLionProjects/postgres_jit_project/logfile_pcq stop
echo "Stopping old postgres:"
$HOME/postgres/bin/pg_ctl -D $HOME/postgres/data -l $HOME/CLionProjects/postgres_jit_project/logfile_pcq stop
