#!/usr/bin/bash

DB_FILES=$(pwd)/db
DB_NAME=${PGUSER:-test}
DB_SCHEMA=${PGSCHEMA:-test}
DB_HOST=${PGHOST:-localhost}
DB_PORT=${PGPORT:-5432}
DB_USER=${PGUSER:-postgres}
PGROOT=/usr/local/pgsql

truncate -s 1G db.img
mkfs.ext4 -F -L test db.img

mkdir -p $DB_FILES
LOOP=$(sudo losetup -f --show db.img)
sudo mount $LOOP $DB_FILES

sudo rm -rf "$DB_FILES/lost+found"
sudo chown -R postgres:postgres $DB_FILES
sudo chmod 700 $DB_FILES

sudo -u postgres $PGROOT/bin/initdb --auth-local=trust --auth-host=trust -D $DB_FILES --encoding=UTF8 --locale=C


sudo -u postgres sed -i "s/#ssl = off/ssl = off/" $DB_FILES/postgresql.conf
sleep 1
sudo -u postgres $PGROOT/bin/pg_ctl -D $DB_FILES  start
sleep 1

$PGROOT/bin/createdb -p $DB_PORT -h $DB_HOST -U $DB_USER $DB_NAME

