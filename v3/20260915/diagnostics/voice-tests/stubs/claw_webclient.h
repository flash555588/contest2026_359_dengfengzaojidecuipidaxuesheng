#pragma once
#include <stddef.h>
#include <stdatomic.h>
int claw_webclient_post_binary(const char *,const char *,const char *,const void *,size_t,atomic_bool *,char **);
