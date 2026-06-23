// pgerl_nif.c
// requires C99

#include "erl_nif.h"
#include "libpq-fe.h"

// Erlang resource wrappers
typedef struct {
	PGconn* conn;
} Pgerl_connection;

typedef struct {
	PGresult* res;
} Pgerl_result;

// Erlang resource types
static ErlNifResourceType* Pgerl_connection_resource = NULL;
static ErlNifResourceType* Pgerl_result_resource = NULL;

// { error, "your error message" }
static ERL_NIF_TERM pgerl_error(ErlNifEnv* env, const char* reason) {
	return enif_make_tuple2(env, enif_make_atom(env, "error"), enif_make_string(env,reason, ERL_NIF_UTF8));
}

// Erlang resource destructors
static void pgerl_conn_destructor(ErlNifEnv* env, void* obj) {
	Pgerl_connection* res = (Pgerl_connection*)obj;
	if (!res || !res->conn) return;
	PQfinish(res->conn);
}

static void pgerl_result_destructor(ErlNifEnv* env, void* obj) {
	Pgerl_result* res = (Pgerl_result*)obj;
	if (!res || !res->res) return;
	PQclear(res->res);
}

// Erlang resource constructor
static int pgerl_load(ErlNifEnv* env, void** priv, ERL_NIF_TERM load_info) {
	ErlNifResourceFlags flags = (ErlNifResourceFlags)(ERL_NIF_RT_CREATE | ERL_NIF_RT_TAKEOVER);
	Pgerl_connection_resource = enif_open_resource_type(env,NULL,"PGconn",pgerl_conn_destructor,flags,NULL);
	Pgerl_result_resource = enif_open_resource_type(env,NULL,"PGresult",pgerl_result_destructor,flags,NULL);
	if (!Pgerl_connection_resource || !Pgerl_result_resource) return -1;
	return 0;
}

// PQstatus wrapper
static ERL_NIF_TERM pgerl_status(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[] ) {
	Pgerl_connection* res = NULL;

        if (!enif_get_resource(env,argv[0],Pgerl_connection_resource, (void**)&res)) return enif_make_badarg(env);

	if (!res || !res->conn || PQstatus(res->conn) != CONNECTION_OK) {
		fprintf(stderr,"not connected %s", PQerrorMessage(res->conn));
		const char* err = PQerrorMessage(res->conn);
		return pgerl_error(env,err);
	}
	return enif_make_atom(env,"ok");
}

// PQexecParams wrapper -> PGresult resource
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

		// assume binaries will be the norm in the params tuples
		if (enif_is_binary(env,param)) { 
			ErlNifBinary src;
			if (!enif_inspect_binary(env,param,&src)) {
				fprintf(stderr,"Bad binary at %d\n",i);
				return enif_make_badarg(env);
			}
			if (!enif_alloc_binary(src.size + 1, &bins[i])) {
				fprintf(stderr,"failed to allocate binary at %d\n",i);
				return enif_make_badarg(env);
			}
			memcpy(bins[i].data, src.data, src.size);
			bins[i].data[src.size] = '\0';
			bins[i].size = src.size;
			params[i] = (char*)bins[i].data;
			lengths[i] = bins[i].size;
			allocations[i] = 1;
			continue;
		}

		// null, empty string needs to come before list test
		if (enif_is_empty_list(env,param)) { 
			params[i] = NULL;
			lengths[i] = 0;
			continue;
		}

		// list as text support, consider changing to support [ numbers... ]
		if (enif_is_list(env,param)) {
			unsigned int bin_len = 0;
			if (!enif_get_list_length(env,param,&bin_len)) {
				fprintf(stderr,"bad list length at %d\n",i);
				return enif_make_badarg(env);
			}
			if (!enif_alloc_binary(bin_len,&bins[i])) {
				fprintf(stderr,"failed to allocate binary at %d\n",i);
				return enif_make_badarg(env);
			}
			if (!enif_get_string(env, param, bins[i].data, bins[i].size, ERL_NIF_UTF8)) {
				fprintf(stderr,"bad string at %d\n", i);
				return enif_make_badarg(env);
			}
			params[i] = bins[i].data;
			lengths[i] = bins[i].size;
			allocations[i] = 1;
			continue;
		}

		// raw numbers that we didn't convert to binaries before calling
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

		// { blob, Binary } -> for raw binaries w/ no string interpolation
		if (enif_is_tuple(env,param)) { 
			const ERL_NIF_TERM* elems;
			int arity;
			if (!enif_get_tuple(env, param, &arity, &elems) || arity != 2) {
				fprintf(stderr,"bad tuple at %d\n",i);
				return enif_make_badarg(env);
			}
			if (!enif_inspect_binary(env, elems[1], &bins[i])) {
				fprintf(stderr,"bad tuple binary at %d\n",i);
				return enif_make_badarg(env);
			}
			params[i] = (char*)bins[i].data;
			lengths[i] = bins[i].size;
			formats[i] = 1;
			continue;
		}

		// null -> SQL NULL, atom -> string literal
		if (enif_is_atom(env,param)) { 
			char atom[256];
			int atom_len = 0;
			if (!(atom_len = enif_get_atom(env, param, atom, sizeof(atom), ERL_NIF_UTF8))) {
				fprintf(stderr,"bad atom at %d\n",i);
				return enif_make_badarg(env);
			}
			if (strcmp(atom, "null") == 0) {
				params[i] = NULL;
				lengths[i] = 0;
				continue;
			}
			if (!enif_alloc_binary(atom_len, &bins[i])) {
				fprintf(stderr,"failed to allocate binary at %d\n",i);
				return enif_make_badarg(env);
			}
			memcpy(bins[i].data, atom, atom_len);
			bins[i].size = atom_len;
			params[i] = (char*)bins[i].data;
			lengths[i] = bins[i].size;
			allocations[i] = 1;
			continue;
		}
		// all others
		fprintf(stderr,"Invalid parameter at %d\n", i);
		return enif_make_badarg(env);
	}

	// construct the SQL string
	char cmd[command.size + 1];
	memcpy(cmd, command.data, command.size);
	cmd[command.size] = '\0';

	Pgerl_result* result = (Pgerl_result*)enif_alloc_resource(Pgerl_result_resource,sizeof(Pgerl_result));
	result->res = PQexecParams(pgconn->conn,cmd,nparams,NULL,params,lengths,formats,0);

	// free all the temporary allocations
	for (int i = 0; i < nparams; ++i) {
		if (allocations[i])
			enif_release_binary(&bins[i]);
	}

	//  we're using a switch so we can cascade statuses
	switch(PQresultStatus(result->res)) {
		// happy path
		case PGRES_TUPLES_OK:		// SELECT and friends
		case PGRES_COMMAND_OK:		// INSERT, UPDATE, DELETE, DDL
		case PGRES_NONFATAL_ERROR: {	// NOTICE or WARNING
			ERL_NIF_TERM term = enif_make_resource(env, result);
			enif_release_resource(result);
			return term;
		}
		// oops path
		case PGRES_FATAL_ERROR:
		case PGRES_BAD_RESPONSE:
		default: {
			const char* err = PQresultErrorMessage(result->res);
			enif_release_resource(result);
			return pgerl_error(env, err);
		}
	}
}

// Connection wrapper takes ConnInfo string and a schema atom -> PGconn
static ERL_NIF_TERM pgerl_init(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[] ) {
	unsigned int conninfo_size;

	if (argc != 2)  
		return enif_make_badarg(env);

	if (!enif_get_list_length(env, argv[0], &conninfo_size))
		return enif_make_badarg(env);

	if (conninfo_size == 0)
		return enif_make_badarg(env);

	char conninfo[conninfo_size+1];	
	memset(conninfo,0,conninfo_size+1);
	if (0 >= enif_get_string(env, argv[0], conninfo, sizeof(conninfo), ERL_NIF_UTF8))
		return enif_make_badarg(env);

	char schema[256];
	if (!enif_get_atom(env, argv[1], schema, sizeof(schema), ERL_NIF_UTF8))
		return enif_make_badarg(env);

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

// PQntuples wrapper
static ERL_NIF_TERM pgerl_ntuples(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[] ) {
	Pgerl_result* res;

	if (argc != 1)  
		return enif_make_badarg(env);

	if (!enif_get_resource(env,argv[0],Pgerl_result_resource,(void**)&res))
		return enif_make_badarg(env);

	if (!res || !res->res) 
		return enif_make_badarg(env);

	ERL_NIF_TERM term = enif_make_int(env,PQntuples(res->res));
	return term;
}

// PQnfields wrapper
static ERL_NIF_TERM pgerl_nfields(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[] ) {
	Pgerl_result* res;

	if (argc != 1)  
		return enif_make_badarg(env);

	if (!enif_get_resource(env,argv[0],Pgerl_result_resource,(void**)&res))
		return enif_make_badarg(env);

	if (!res || !res->res) 
		return enif_make_badarg(env);

	ERL_NIF_TERM term = enif_make_int(env,PQnfields(res->res));
	return term;
}

// PQfname wrapper
static ERL_NIF_TERM pgerl_fname(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[] ) {
	Pgerl_result* res;
	int field;

	if (argc != 2)  
		return enif_make_badarg(env);

	if (!enif_get_int(env,argv[1],&field))
		return enif_make_badarg(env);

	if (!enif_get_resource(env,argv[0],Pgerl_result_resource,(void**)&res))
		return enif_make_badarg(env);

	if (!res || !res->res) 
		return enif_make_badarg(env);

	char* data = PQfname(res->res,field);
	size_t length = strlen(data);

	ERL_NIF_TERM term = enif_make_resource_binary(env,res,data,length);
	return term;
}

// PQgetisnull wrapper
static ERL_NIF_TERM pgerl_is_null(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[] ) {
	Pgerl_result* res;
	int row,column;

	if (argc != 3)  
		return enif_make_badarg(env);
	
	if (!enif_get_int(env,argv[2],&column))
		return enif_make_badarg(env);

	if (!enif_get_int(env,argv[1],&row))
		return enif_make_badarg(env);

	if (!enif_get_resource(env,argv[0],Pgerl_result_resource,(void**)&res))
		return enif_make_badarg(env);

	if (!res || !res->res) 
		return enif_make_badarg(env);

	ERL_NIF_TERM term = enif_make_int(env,PQgetisnull(res->res,row,column));
	return term;
}

// PQgetvalue & PQgetlength wrapper
static ERL_NIF_TERM pgerl_value(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[] ) {
	Pgerl_result* res;
	int row,column;
	char* data;
	int length;

	if (argc != 3)  
		return enif_make_badarg(env);
	
	if (!enif_get_int(env,argv[2],&column))
		return enif_make_badarg(env);

	if (!enif_get_int(env,argv[1],&row))
		return enif_make_badarg(env);

	if (!enif_get_resource(env,argv[0],Pgerl_result_resource,(void**)&res))
		return enif_make_badarg(env);

	if (!res || !res->res) 
		return enif_make_badarg(env);

	ErlNifBinary value;
	value.data = PQgetvalue(res->res,row,column);
	value.size = PQgetlength(res->res,row,column);
//	fprintf(stderr,"Result: %s\n", value.data);
//	fprintf(stderr,"Length: %d\n", value.size);

	ERL_NIF_TERM term = enif_make_resource_binary(env,res,value.data,value.size);
	return term;
}

// NIF vtable
static ErlNifFunc nif_funcs[] = {
	{ "init", 2, pgerl_init },
	{ "status", 1, pgerl_status },
	{ "query", 3, pgerl_query },
	{ "ntuples", 1, pgerl_ntuples },
	{ "nfields", 1, pgerl_nfields },
	{ "fname", 2, pgerl_fname },
	{ "value", 3, pgerl_value },
	{ "is_null", 3, pgerl_is_null }
};

ERL_NIF_INIT(pgerl,nif_funcs,pgerl_load,NULL,NULL,NULL)
