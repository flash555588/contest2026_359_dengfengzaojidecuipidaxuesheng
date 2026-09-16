set pagination off
set confirm off
set remotetimeout 20
target remote 127.0.0.1:3333
monitor halt
python
for expr in ["g_iob_count", "g_iob_freelist", "g_active_tcp_connections", "g_netlock", "'glass_music_service.c'::player_active", "'glass_music_service.c'::want_play", "'glass_music_service.c'::want_pause", "'glass_music_service.c'::state.state", "'glass_music_service.c'::state.search_error"]:
    try: print(expr, gdb.parse_and_eval(expr))
    except gdb.error as error: print('UNAVAILABLE', expr, error)
try:
    node = gdb.parse_and_eval('g_active_tcp_connections.head')
    seen = set()
    while int(node) and int(node) not in seen and len(seen) < 64:
        seen.add(int(node))
        conn = node.cast(gdb.lookup_type('struct tcp_conn_s').pointer()).dereference()
        print('TCP', hex(int(node)), 'state', int(conn['tcpstateflags']),
              'refs', int(conn['crefs']), 'readahead', hex(int(conn['readahead'])))
        chain = conn['readahead']
        count = 0
        while int(chain) and count < 256:
            count += 1
            chain = chain['io_flink']
        print('IOB chain length', count)
        node = node['flink']
except gdb.error as error: print(error)
end
monitor resume
detach
quit
