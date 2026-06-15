// pgerl_nif.c
// requires C99

#include "erl_nif.h"
#include "libpq-fe.h"

typedef struct {
	PGconn* conn;
} Pgerl_connection;

static ErlNifResourceType* Pgerl_connection_resource = NULL;

static ERL_NIF_TERM pgerl_error(ErlNifEnv* env, const char* reason) {
	return enif_make_tuple2(env, enif_make_atom(env, "error"), enif_make_string(env,reason, ERL_NIF_LATIN1));
}

static void pgerl_conn_destructor(ErlNifEnv* env, void* obj) {
	Pgerl_connection* res = (Pgerl_connection*)obj;
	fprintf(stderr,"pgerl_conn_destructor on %p\n", obj);
	if (!res || !res->conn) return;
	PQfinish(res->conn);
	fprintf(stderr,"pgerl_conn_destructor finished %p\n",obj);
}

static int pgerl_load(ErlNifEnv* env, void** priv, ERL_NIF_TERM load_info) {
	ErlNifResourceFlags flags = (ErlNifResourceFlags)(ERL_NIF_RT_CREATE | ERL_NIF_RT_TAKEOVER);
	if (Pgerl_connection_resource = enif_open_resource_type(env,NULL,"PGconn",pgerl_conn_destructor,flags,NULL)) return 0;
	return -1;
}

static ERL_NIF_TERM pgerl_status(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[] ) {
	Pgerl_connection* res = NULL;

        if (!enif_get_resource(env,argv[0],Pgerl_connection_resource, (void**)&res)) return enif_make_badarg(env);

	if (!res || !res->conn || PQstatus(res->conn) != CONNECTION_OK) {
		const char* err = PQerrorMessage(res->conn);
		return pgerl_error(env,err);
	}

	return enif_make_atom(env,"ok");
}

static ERL_NIF_TERM pgerl_query(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[] ) {
	unsigned int nparams = 0;
	const ERL_NIF_TERM* terms;

	if (argc != 3)
		return enif_make_badarg(env);

	Pgerl_connection* pgconn = NULL;
        if (!enif_get_resource(env,argv[0],Pgerl_connection_resource, (void**)&pgconn))
		return enif_make_badarg(env);

	ErlNifBinary command;
	if (!enif_inspect_binary(env,argv[1],&command))
		return enif_make_badarg(env);

	if (!enif_get_tuple(env,argv[2], &nparams, &terms))
		return enif_make_badarg(env);

	const char* params[nparams];
	int lengths[nparams];
	int formats[nparams];	
	ErlNifBinary bins[nparams];
	int allocations[nparams];

	// TODO: take metrics on the tests and reorder in order of frequency
	for (int i = 0; i < nparams; ++i) {
		ERL_NIF_TERM param = terms[i];
		allocations[i] = 0;
		formats[i] = 0;
		if (enif_is_binary(env,param)) { // typically  text
			if (!enif_inspect_binary(env,param,&bins[i])) {
				fprintf(stderr,"Bad binary at %d\n",i);
				return enif_make_badarg(env);
			}
			params[i] = bins[i].data;
			lengths[i] = bins[i].size;
			continue;
		}
		if (enif_is_empty_list(env,param)) { // null, empty string needs to come before list test
			params[i] = NULL;
			lengths[i] = 0;
			continue;
		}

		if (enif_is_list(env,param)) { // typically text
			unsigned int bin_len = 0;
			if (!enif_get_list_length(env,param,&bin_len)) {
				fprintf(stderr,"bad list length at %d\n",i);
				return enif_make_badarg(env);
			}
			if (!enif_alloc_binary(bin_len,&bins[i])) {
				fprintf(stderr,"failed to allocate binary at %d\n",i);
				return enif_make_badarg(env);
			}
			if (!enif_get_string(env, argv[i], bins[i].data, bins[i].size, ERL_NIF_LATIN1)) {
				fprintf(stderr,"bad string at %d\n", i);
				return enif_make_badarg(env);
			}
			params[i] = bins[i].data;
			lengths[i] = bins[i].size;
			allocations[i] = 1;
			continue;
		}
		if (enif_is_number(env,param)) {
			ErlNifSInt64 inum;
			double dnum;
			if (!enif_alloc_binary(48,&bins[i])) {
				fprintf(stderr,"failed to allocate binary at %d\n",i);
				return enif_make_badarg(env);
			}
			if (enif_get_int64(env,param,&inum)){
				bins[i].size = enif_snprintf(bins[i].data,48,"%lld",inum);
			} else if (enif_get_double(env,param,&dnum)) {
				bins[i].size = enif_snprintf(bins[i].data,48,"%.17g",inum);
			} else {
				fprintf(stderr,"unsupported number %d\n",i);
				enif_release_binary(&bins[i]);
				return enif_make_badarg(env);
			}
			params[i] = bins[i].data;
			lengths[i] = bins[i].size;
			allocations[i] = 1;	
			continue;
		}
		if (enif_is_map(env,param)) {	// probably json
			fprintf(stderr,"todo implement maps\n");
			params[i] = NULL;
			lengths[i] = 0;
			continue;
		}
		if (enif_is_tuple(env,param)) { // usually a tagged blob
			fprintf(stderr,"todo implement blobs\n");
			params[i] = NULL;
			lengths[i] = 0;

			continue;
		}
		if (enif_is_atom(env,param)) { // null or text
			fprintf(stderr,"todo implement atoms\n");
			params[i] = NULL;
			lengths[i] = 0;
			continue;
		}
		// all others
		fprintf(stderr,"Invalid parameter at %d\n", i);
		return enif_make_badarg(env);
	}

	PGresult* res = PQexecParams(pgconn->conn,command.data,nparams,NULL,params,lengths,formats,0);

	int status = 0;
	switch(status = PQresultStatus(res)) {
		case PGRES_TUPLES_OK:
			fprintf(stderr,"got result %p\n",res);
			break;
		default:
			fprintf(stderr,"got status %p\n",status);
	}

	// free all the temporary allocations
	for (int i = 0; i < nparams; ++i) {
		if (allocations[i]) {
			fprintf(stderr,"releasing bin at %d\n",i);
			enif_release_binary(&bins[i]);
		}
	}

}

static ERL_NIF_TERM pgerl_init(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[] ) {
	unsigned int conninfo_size;
	unsigned int schema_size;

	if (argc != 2)  
		return enif_make_badarg(env);

	if (!enif_get_list_length(env, argv[0], &conninfo_size))
		return enif_make_badarg(env);

	if (conninfo_size == 0)
		return enif_make_badarg(env);

	char conninfo[conninfo_size+1];	
	memset(conninfo,0,schema_size+1);
	if (0 >= enif_get_string(env, argv[0], conninfo, sizeof(conninfo), ERL_NIF_LATIN1))
		return enif_make_badarg(env);

	if (!enif_get_list_length(env, argv[1], &schema_size))
		return enif_make_badarg(env);

	if (schema_size == 0)
		return enif_make_badarg(env);
	
	char schema[schema_size+1];
	memset(schema,0,schema_size+1);

	if (0 >= enif_get_string(env, argv[1], schema, sizeof(schema), ERL_NIF_LATIN1))
		return enif_make_badarg(env);

	fprintf(stderr,"Connecting to %s\n", conninfo);
	fprintf(stderr,"Loading schema %s\n", schema);

	Pgerl_connection* res = (Pgerl_connection*)enif_alloc_resource(Pgerl_connection_resource,sizeof(Pgerl_connection));
	res->conn = PQconnectdb(conninfo);

	if (PQstatus(res->conn) != CONNECTION_OK) {
		const char* err = PQerrorMessage(res->conn);
		fprintf(stderr,"Failed to connect %s\n", err);
		return pgerl_error(env,err);
	}

	ERL_NIF_TERM term = enif_make_resource(env,res);	
	enif_release_resource(res);
	return term;
}

static ErlNifFunc nif_funcs[] = {
	{ "init", 1, pgerl_init },
	{ "status", 1, pgerl_status },
	{ "query", 3, pgerl_query }
};

ERL_NIF_INIT(pgerl,nif_funcs,pgerl_load,NULL,NULL,NULL)
