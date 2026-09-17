set pagination off
set confirm off
set remotetimeout 5
target remote 127.0.0.1:3339
monitor halt
python
expressions = [
    'esp_i2s0_priv',
    'esp_i2s0_config',
    '*esp_i2s0_config.ctx->dev',
    '*(struct es8311_dev_s *)((struct esp_buffer_s *)esp_i2s0_priv.rx.act.head)->arg',
    '*(struct esp_buffer_s *)esp_i2s0_priv.rx.act.head',
    '*((struct esp_buffer_s *)esp_i2s0_priv.rx.act.head)->dma_link[0]',
    '*(struct esp_buffer_s *)esp_i2s0_priv.rx.pend.head',
]
for expression in expressions:
    try: print(expression, gdb.parse_and_eval(expression))
    except gdb.error as error: print(expression, str(error))
for i in range(int(gdb.parse_and_eval('g_npidhash'))):
    task = gdb.parse_and_eval('g_pidhash[%d]' % i)
    if int(task) and int(task['pid']) > 11:
        name = task['name'].string()
        if name in ('desktop', 'es8311'):
            print('AUDIO TASK', task['pid'], name, 'state', task['task_state'], 'xcp', task['xcp'])
end
monitor resume
detach
quit
