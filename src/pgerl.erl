%% MIT License
%% Copyright 2026 David J Goehrig <dave@dloh.org>
%%
%% Permission is hereby granted, free of charge, to any person obtaining a
%% copy of this software and associated documentation files (the "Software"),
%% to deal in the Software without restriction, including without limitation
%% the rights to use, copy, modify, merge, publish, distribute, sublicense,
%% and/or sell copies of the Software, and to permit persons to whom the
%% Software is furnished to do so, subject to the following conditions:
%%
%% The above copyright notice and this permission notice shall be included in
%% all copies or substantial portions of the Software.
%%
%% THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
%% IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
%% FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
%% THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
%% LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
%% FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
%% DEALINGS IN THE SOFTWARE.

-module(pgerl).
-author({ "David J Goehrig", "dave@dloh.org" }).
-copyright(<<"© 2026 David J. Goehrig"/utf8>>).
-export([ start/0, init/2, status/1, query/3, ntuples/1, nfields/1, fname/2, value/3, is_null/3, procs/2, generate/2 ]).

%%%-----------------------------------------------------------------------------
%%% Public API
%%%-----------------------------------------------------------------------------

%% load the NIF shared library
start() ->
	erlang:load_nif("./pgerl_nif", 0).

%% connect to postgres; Schema is an atom matching the pg schema name
%% returns a connection resource or {error, Reason}
init(_ConnInfo, _Schema) ->
	error(nif_not_loaded).

%% returns ok or {error, Reason}
status(_Conn) ->
	error(nif_not_loaded).

%% execute a parameterized query; Params is a tuple of binaries/integers/[]
%% returns a PGresult resource
query(_Conn, _Command, _Params) ->
	error(nif_not_loaded).

%% number of rows in a PGresult resource
ntuples(_Result) ->
	error(nif_not_loaded).

%% number of columns in a PGresult resource
nfields(_Result) ->
	error(nif_not_loaded).

%% column name at index Col (0-based)
fname(_Result, _Col) ->
	error(nif_not_loaded).

%% cell value at Row, Col (0-based), returned as binary
value(_Result, _Row, _Col) ->
	error(nif_not_loaded).

%% true if the cell at Row, Col is SQL NULL
is_null(_Result, _Row, _Col) ->
	error(nif_not_loaded).

%% list all stored procedures in Schema (atom), returns [{ Name, Arity }]
procs(Conn, Schema) ->
	Result = query(Conn, <<"SELECT proc.proname, proc.pronargs FROM pg_proc proc JOIN pg_namespace namesp ON proc.pronamespace = namesp.oid WHERE namesp.nspname = $1 ORDER BY proc.proname">>, { atom_to_binary(Schema, utf8) }),
	decode_procs(Result, ntuples(Result), 0, []).

%% generate and load an Erlang module named Schema wrapping all procs in that schema
generate(Conn, Schema) ->
	Forms = module_forms(Schema, procs(Conn, Schema)),
	{ok, Schema, Binary} = compile:forms(Forms, []),
	{module, Schema} = code:load_binary(Schema, atom_to_list(Schema) ++ ".erl", Binary),
	ok.

%%%-----------------------------------------------------------------------------
%%% Private
%%%-----------------------------------------------------------------------------

decode_procs(_Result, N, N, Acc) ->
	lists:reverse(Acc);
decode_procs(Result, N, I, Acc) ->
	Name = binary_to_atom(value(Result, I, 0), utf8),
	Arity = binary_to_integer(value(Result, I, 1)),
	decode_procs(Result, N, I+1, [{ Name, Arity } | Acc]).

module_forms(Module, Procs) ->
	Exports = [ {Name, Arity+1} || {Name, Arity} <- Procs ],
	[
		{attribute, 1, module, Module},
		{attribute, 1, export, Exports}
		| [ proc_form(Name, Arity) || {Name, Arity} <- Procs ]
	].

proc_form(Name, Arity) ->
	ConnVar = {var, 1, 'Conn'},
	ArgVars = [ {var, 1, list_to_atom("Arg" ++ integer_to_list(I))} || I <- lists:seq(1, Arity) ],
	SQL = proc_sql(Name, Arity),
	Call = {call, 1,
		{remote, 1, {atom, 1, pgerl}, {atom, 1, query}},
		[ConnVar, SQL, {tuple, 1, ArgVars}]},
	{function, 1, Name, Arity+1, [{clause, 1, [ConnVar | ArgVars], [], [Call]}]}.

proc_sql(Name, Arity) ->
	Placeholders = string:join([ "$" ++ integer_to_list(I) || I <- lists:seq(1, Arity) ], ","),
	SQL = "SELECT " ++ atom_to_list(Name) ++ "(" ++ Placeholders ++ ")",
	{bin, 1, [{bin_element, 1, {string, 1, SQL}, default, default}]}.

-ifdef(TEST).
-include_lib("eunit/include/eunit.hrl").

conninfo() ->
	Host = os:getenv("PGHOST", "localhost"),
	Port = os:getenv("PGPORT", "5432"),
	User = os:getenv("PGUSER", "postgres"),
	DB = os:getenv("PGDATABASE", "test"),
	lists:flatten(io_lib:format("host=~s port=~s user=~s dbname=~s", [ Host, Port, User, DB ])).

setup() ->
	ok = pgerl:start(),
	Conn = pgerl:init(conninfo(), pgtest),
	pgerl:query(Conn, <<"CREATE SCHEMA IF NOT EXISTS pgtest">>, {}),
	Conn.

cleanup(Conn) ->
	pgerl:query(Conn, <<"DROP SCHEMA IF EXISTS pgtest CASCADE">>, {}),
	ok.

pgerl_test_() ->
	{setup,
	 fun setup/0,
	 fun cleanup/1,
	 fun tests/1}.

tests(_) ->
	[
	 %% connect and verify status
	 ?_test(begin
		Conn = pgerl:init(conninfo(), public),
		?assertNotMatch({ error, _ }, Conn),
		?assertEqual(ok, pgerl:status(Conn))
	 end),

	 %% bad conninfo returns an error tuple
	 ?_test(begin
		{ error, _ } = pgerl:init("host=localhost port=0 user=nobody dbname=noexist", public)
	 end),

	 %% simple query with no params returns a result resource
	 ?_test(begin
		Conn = pgerl:init(conninfo(), public),
		Result = pgerl:query(Conn, <<"SELECT 1 AS n">>, {}),
		?assertNotMatch({ error, _ }, Result),
		?assertEqual(1, pgerl:ntuples(Result)),
		?assertEqual(1, pgerl:nfields(Result)),
		?assertEqual(<<"n">>, pgerl:fname(Result, 0)),
		?assertEqual(<<"1">>, pgerl:value(Result, 0, 0)),
		?assertEqual(0, pgerl:is_null(Result, 0, 0))
	 end),

	 %% binary parameter passed as $1
	 ?_test(begin
		Conn = pgerl:init(conninfo(), public),
		Result = pgerl:query(Conn, <<"SELECT $1::text AS val">>, { <<"hello">> }),
		?assertNotMatch({ error, _ }, Result),
		?assertEqual(<<"hello">>, pgerl:value(Result, 0, 0))
	 end),

	 %% integer parameter passed as $1 comes back as text
	 ?_test(begin
		Conn = pgerl:init(conninfo(), public),
		Result = pgerl:query(Conn, <<"SELECT $1::integer AS n">>, { 42 }),
		?assertNotMatch({ error, _ }, Result),
		?assertEqual(<<"42">>, pgerl:value(Result, 0, 0))
	 end),

	 %% empty list [] is treated as SQL NULL
	 ?_test(begin
		Conn = pgerl:init(conninfo(), public),
		Result = pgerl:query(Conn, <<"SELECT $1::text AS val">>, { [] }),
		?assertNotMatch({ error, _ }, Result),
		?assertEqual(1, pgerl:is_null(Result, 0, 0))
	 end),

	 %% multiple mixed parameters; check column count and names
	 ?_test(begin
		Conn = pgerl:init(conninfo(), public),
		Result = pgerl:query(Conn, <<"SELECT $1::text AS name, $2::integer AS age">>, { <<"alice">>, 30 }),
		?assertNotMatch({ error, _ }, Result),
		?assertEqual(2, pgerl:nfields(Result)),
		?assertEqual(<<"name">>, pgerl:fname(Result, 0)),
		?assertEqual(<<"age">>, pgerl:fname(Result, 1)),
		?assertEqual(<<"alice">>, pgerl:value(Result, 0, 0)),
		?assertEqual(<<"30">>, pgerl:value(Result, 0, 1))
	 end),

	 %% atom null maps to SQL NULL
	 ?_test(begin
		Conn = pgerl:init(conninfo(), public),
		Result = pgerl:query(Conn, <<"SELECT $1::text AS val">>, { null }),
		?assertNotMatch({ error, _ }, Result),
		?assertEqual(1, pgerl:is_null(Result, 0, 0))
	 end),

	 %% atom other than null passed as string literal
	 ?_test(begin
		Conn = pgerl:init(conninfo(), public),
		Result = pgerl:query(Conn, <<"SELECT $1::text AS val">>, { hello }),
		?assertNotMatch({ error, _ }, Result),
		?assertEqual(<<"hello">>, pgerl:value(Result, 0, 0))
	 end),

	 %% { blob, Binary } passed as binary format
	 ?_test(begin
		Conn = pgerl:init(conninfo(), public),
		Result = pgerl:query(Conn, <<"SELECT $1::bytea AS val">>, { { blob, <<1,2,3,4>> } }),
		?assertNotMatch({ error, _ }, Result),
		?assertEqual(0, pgerl:is_null(Result, 0, 0))
	 end),

	 %% procs/2 returns a list of {Name, Arity} for the schema
	 ?_test(begin
		Conn = pgerl:init(conninfo(), pgtest),
		pgerl:query(Conn, <<"CREATE OR REPLACE FUNCTION pgtest.add(a integer, b integer) RETURNS integer AS $$ BEGIN RETURN a + b; END; $$ LANGUAGE plpgsql">>, {}),
		Ps = pgerl:procs(Conn, pgtest),
		?assert(is_list(Ps)),
		?assert(lists:keymember(add, 1, Ps))
	 end),

	 %% generate/2 loads a module named after the schema with one wrapper per proc
	 ?_test(begin
		Conn = pgerl:init(conninfo(), pgtest),
		pgerl:query(Conn, <<"CREATE OR REPLACE FUNCTION pgtest.hello() RETURNS text AS $$ BEGIN RETURN 'hello'; END; $$ LANGUAGE plpgsql">>, {}),
		ok = pgerl:generate(Conn, pgtest),
		Result = pgtest:hello(Conn),
		?assertEqual(<<"hello">>, pgerl:value(Result, 0, 0))
	 end),

	 %% generated wrapper passes arguments correctly
	 ?_test(begin
		Conn = pgerl:init(conninfo(), pgtest),
		Result = pgtest:add(Conn, <<"3">>, <<"5">>),
		?assertEqual(<<"8">>, pgerl:value(Result, 0, 0))
	 end)
	].

-endif.
