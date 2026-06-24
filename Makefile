all: install

pgerl_nif.so: c_src/pgerl_nif.c
	gcc -ggdb -fPIC -shared -o pgerl_nif.so -I/usr/local/pgsql/include -I/usr/local/lib/erlang/usr/include -L/usr/local/pgsql/lib -lpq c_src/pgerl_nif.c

.PHONY: test
test: 
	LD_LIBRARY_PATH=/usr/local/pgsql/lib beamer test src/pgerl.erl

install: pgerl_nif.so
	beamer compile src/pgerl.erl
	cp pgerl_nif.so ~/.beamer/

db: test/db.img
	cd test && sudo ./db.sh
	
start:
	cd test && sudo ./start.sh
