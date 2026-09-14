CSRCS = core/claw_agent_mgr.c \
  core/claw_cap.c \
  core/claw_core.c \
  core/claw_core_agent_loop.c \
  core/claw_core_context.c \
  core/claw_core_context_persist.c \
  core/claw_core_control.c \
  core/claw_core_events.c \
  core/claw_core_llm.c \
  core/claw_core_messages.c \
  core/claw_core_utils.c \
  core/claw_event.c \
  core/claw_event_router.c \
  core/claw_session_mgr.c \
  core/claw_version.c \
  core/llm/backends/claw_llm_backend_anthropic.c \
  core/llm/backends/claw_llm_backend_custom.c \
  core/llm/backends/claw_llm_backend_openai_compatible.c \
  core/llm/claw_llm_runtime.c \
  core/llm/media/claw_media_pipeline.c \
  port/claw_task_posix.c \
  port/connect.c \
  port/espclaw_main.c \
  port/http_webclient.c \
  port/mutex.c \
  port/queue.c \
  port/task.c \
  port/tls_mbedtls.c \
  utils/claw_utils_string.c \
  utils/claw_utils_time.c

CSRCS += port/esp_err_to_name.c
