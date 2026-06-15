#!/usr/bin/bash

DB_FILES=$(pwd)/db
DB_NAME=${PGUSER:-test}
DB_SCHEMA=${PGSCHEMA:-test}
DB_HOST=${PGHOST:-localhost}
DB_PORT=${PGPORT:-5432}
DB_USER=${PGUSER:-postgres}

PGROOT=/usr/local/pgsql

LOOP=$(sudo losetup -f --show db.img)
sudo mount $LOOP $DB_FILES
sudo -u postgres $PGROOT/bin/pg_ctl -D $DB_FILES -l logfile start
