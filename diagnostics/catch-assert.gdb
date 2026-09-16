set pagination off
set confirm off
set remotetimeout 60
target remote 127.0.0.1:3333
break up_assert
break __assert
continue
printf "\n==== stopped after assert ====\n"
bt
info registers
printf "==== tasks ====\n"
thread apply all bt
detach
quit
