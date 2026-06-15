-module(pgerl).
-export([start/0,init/1 ]).


start() ->
	erlang:load_nif("./pgerl_nif",0).

init(ConnInfo) ->
	io:format("nif not loaded~n").
