all: install

pgerl_nif.so: c_src/pgerl_nif.c
	gcc -ggdb -fPIC -shared -o pgerl_nif.so -I/usr/local/pgsql/include -I/usr/local/lib/erlang/usr/include -L/usr/local/pgsql/lib -lpq c_src/pgerl_nif.c

test: pgerl_nif.so
	LD_LIBRARY_PATH=/usr/local/pgsql/lib beamer test src/pgerl.erl

install: pgerl_nif.so
	beamer compile src/pgerl.erl
	mv pgerl_nif.so ~/.beamer/
