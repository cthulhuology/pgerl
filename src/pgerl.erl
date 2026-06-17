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
-export([ start/0, init/2, status/1, query/3 ]).

%%%-----------------------------------------------------------------------------
%%% Public API
%%%-----------------------------------------------------------------------------

%% load the NIF shared library
start() ->
	erlang:load_nif("./pgerl_nif", 0).

%% connect to postgres, returns a connection resource or {error, Reason}
%% NB: nif_funcs must register this as arity 2, not 1
init(_ConnInfo, _Schema) ->
	error(nif_not_loaded).

%% returns ok or {error, Reason}
status(_Conn) ->
	error(nif_not_loaded).

%% execute a parameterized query; Params is a tuple of binaries/integers/[]
%% returns {ok, Rows} or {error, Reason}
query(_Conn, _Command, _Params) ->
	error(nif_not_loaded).

-ifdef(TEST).
-include_lib("eunit/include/eunit.hrl").

conninfo() ->
	Host = os:getenv("PGHOST", "localhost"),
	Port = os:getenv("PGPORT", "5432"),
	User = os:getenv("PGUSER", "postgres"),
	DB = os:getenv("PGDATABASE", "test"),
	lists:flatten(io_lib:format("host=~s port=~s user=~s dbname=~s", [ Host, Port, User, DB ])).

setup() ->
	ok = pgerl:start().

cleanup(_) ->
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
		Conn = pgerl:init(conninfo(), "public"),
		?assertNotMatch({ error, _ }, Conn),
		?assertEqual(ok, pgerl:status(Conn))
	 end),

	 %% bad conninfo returns an error tuple
	 ?_test(begin
		{ error, _ } = pgerl:init("host=localhost port=0 user=nobody dbname=noexist", "public")
	 end),

	 %% simple query with no parameters returns a list of rows
	 ?_test(begin
		Conn = pgerl:init(conninfo(), "public"),
		{ ok, Rows } = pgerl:query(Conn, <<"SELECT 1 AS n">>, {}),
		?assert(is_list(Rows))
	 end),

	 %% binary parameter passed as $1
	 ?_test(begin
		Conn = pgerl:init(conninfo(), "public"),
		{ ok, Rows } = pgerl:query(Conn, <<"SELECT $1::text AS val">>, { <<"hello">> }),
		?assert(is_list(Rows))
	 end),

	 %% integer parameter passed as $1
	 ?_test(begin
		Conn = pgerl:init(conninfo(), "public"),
		{ ok, Rows } = pgerl:query(Conn, <<"SELECT $1::integer AS n">>, { 42 }),
		?assert(is_list(Rows))
	 end),

	 %% empty list [] is treated as SQL NULL
	 ?_test(begin
		Conn = pgerl:init(conninfo(), "public"),
		{ ok, Rows } = pgerl:query(Conn, <<"SELECT $1::text AS val">>, { [] }),
		?assert(is_list(Rows))
	 end),

	 %% multiple mixed parameters
	 ?_test(begin
		Conn = pgerl:init(conninfo(), "public"),
		{ ok, Rows } = pgerl:query(Conn, <<"SELECT $1::text AS name, $2::integer AS age">>, { <<"alice">>, 30 }),
		?assert(is_list(Rows))
	 end)
	].

-endif.
