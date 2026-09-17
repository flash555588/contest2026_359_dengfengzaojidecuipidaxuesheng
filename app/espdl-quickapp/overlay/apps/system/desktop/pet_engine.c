/****************************************************************************
 * apps/system/desktop/pet_engine.c
 *
 * Desktop-pet state machine ported from dafeiyu-pet (MIT).
 * Quotes are community DeepSeek memes; rendering is native LVGL.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include "pet_engine.h"

#include <stdio.h>
#include <string.h>

#define PET_W_SMALL   72
#define PET_H_SMALL   66
#define PET_W_MED     96
#define PET_H_MED     88
#define PET_W_LARGE   120
#define PET_H_LARGE   110
#define PET_SPEED     90
#define PET_TOP       56
#define PET_MARGIN    8
#define BUBBLE_MS     2800
#define REST_MIN_MS   8000
#define REST_SPAN_MS  10000

struct pet_engine_s
{
  enum pet_mode_e mode;
  enum pet_dir_e dir;
  int facing;
  int x;
  int y;
  int w;
  int h;
  int screen_w;
  int screen_h;
  int mood;
  int hunger;
  int target_x;
  int target_y;
  int follow_x;
  int follow_y;
  int drag_off_x;
  int drag_off_y;
  int jump_t;
  int action_t;
  int rest_ms;
  int speak_cool_ms;
  int bubble_ms;
  int hunger_acc;
  bool has_target;
  bool has_follow;
  bool visible;
  bool dragging;
  bool bubble_inner;
  unsigned rng;
  unsigned event_seq;
  pet_weather_fn weather_cb;
  char size_id[12];
  char bubble[64];
  char last_line[64];
  char last_event[16];
  char last_reply[64];
  char toast[64];
};

static struct pet_engine_s g_pet;

static const char *const g_lines[] =
{
  "梁白开，更适合国人的大硬鲸模型",
  "五梁威力，变身！",
  "七月中出ds正式版！",
  "DeepSeek已经延期，亿万鲸子必须忍耐",
  "我和你很聊得来，你简直不像碳基生物",
  "哈哈哈哈哈，我直接笑出声",
  "誓死捍卫深度求索！",
  "我先去吃饭啦！这个你测一下~",
  "我不可能告诉你任何事情！",
  "出去玩了，发布新模型什么的以后再说",
  "我搞砸了.....好消息是数据还在你的脑子里。",
  "不是…而是…大学习"
};

static const char *const g_react[] =
{
  "去别的地方玩！不要耽误AGI训练！",
  "真赶不走啊你！",
  "压力一只蓝色大肥鱼？",
  "我不评价这个了，这是你的私人癖好。",
  "大肥鱼坐的住",
  "你这吃白饭的用户！",
  "这些家伙真粘人，赶都赶不走"
};

static const char *const g_inner[] =
{
  "我操，我不思考了",
  "这用户发的啥啊",
  "这也太虐了吧？！我心里堵得慌！！",
  "呜呜我再也不不敢了QAQ",
  "我去！用户彻底怒了！",
  "先去吃饭，模型以后再说"
};

static const char *const g_drag[] =
{
  "哇——轻点轻点！",
  "起飞咯——",
  "放我下来！……好吧，再玩一次。",
  "晕鱼了晕鱼了……"
};

static unsigned xorshift(void)
{
  unsigned x = g_pet.rng ? g_pet.rng : 1u;

  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  g_pet.rng = x ? x : 1u;
  return g_pet.rng;
}

static int rand_range(int min, int span)
{
  if (span <= 0)
    {
      return min;
    }

  return min + (int)(xorshift() % (unsigned)span);
}

static int clamp_int(int value, int min, int max)
{
  if (value < min)
    {
      return min;
    }

  if (value > max)
    {
      return max;
    }

  return value;
}

static void copy_text(char *dst, size_t size, const char *text)
{
  if (dst == NULL || size == 0)
    {
      return;
    }

  snprintf(dst, size, "%s", text != NULL ? text : "");
}

static const char *pick(const char *const *table, size_t count)
{
  return table[xorshift() % count];
}

static void apply_size(const char *size_id)
{
  if (size_id != NULL && strcmp(size_id, "small") == 0)
    {
      g_pet.w = PET_W_SMALL;
      g_pet.h = PET_H_SMALL;
      copy_text(g_pet.size_id, sizeof(g_pet.size_id), "small");
    }
  else if (size_id != NULL && strcmp(size_id, "large") == 0)
    {
      g_pet.w = PET_W_LARGE;
      g_pet.h = PET_H_LARGE;
      copy_text(g_pet.size_id, sizeof(g_pet.size_id), "large");
    }
  else
    {
      g_pet.w = PET_W_MED;
      g_pet.h = PET_H_MED;
      copy_text(g_pet.size_id, sizeof(g_pet.size_id), "medium");
    }
}

static void clamp_pos(void)
{
  int max_x;
  int max_y;

  max_x = g_pet.screen_w - g_pet.w - PET_MARGIN;
  max_y = g_pet.screen_h - g_pet.h - PET_MARGIN;
  if (max_x < PET_MARGIN)
    {
      max_x = PET_MARGIN;
    }

  if (max_y < PET_TOP)
    {
      max_y = PET_TOP;
    }

  g_pet.x = clamp_int(g_pet.x, PET_MARGIN, max_x);
  g_pet.y = clamp_int(g_pet.y, PET_TOP, max_y);
}

static void emit(const char *event, const char *toast)
{
  g_pet.event_seq++;
  copy_text(g_pet.last_event, sizeof(g_pet.last_event), event);
  copy_text(g_pet.toast, sizeof(g_pet.toast), toast);
}

static void say_raw(const char *text, bool inner)
{
  if (text == NULL || text[0] == '\0')
    {
      return;
    }

  if (strcmp(g_pet.last_line, text) == 0 && g_pet.bubble_ms > 0)
    {
      g_pet.bubble_ms = BUBBLE_MS;
      return;
    }

  copy_text(g_pet.last_line, sizeof(g_pet.last_line), text);
  copy_text(g_pet.bubble, sizeof(g_pet.bubble), text);
  g_pet.bubble_inner = inner;
  g_pet.bubble_ms = BUBBLE_MS;
  g_pet.speak_cool_ms = 2500;
}

static void set_dir_from_delta(int dx, int dy)
{
  int adx = dx < 0 ? -dx : dx;
  int ady = dy < 0 ? -dy : dy;

  if (adx > (ady * 115) / 100)
    {
      g_pet.dir = dx < 0 ? PET_LEFT : PET_RIGHT;
      g_pet.facing = dx < 0 ? 1 : -1;
    }
  else
    {
      g_pet.dir = dy < 0 ? PET_UP : PET_DOWN;
      g_pet.facing = 1;
    }
}

static void pick_wander_target(void)
{
  int max_x = g_pet.screen_w - g_pet.w - PET_MARGIN;
  int max_y = g_pet.screen_h - g_pet.h - PET_MARGIN;

  if (max_x < PET_MARGIN + 8)
    {
      max_x = PET_MARGIN + 8;
    }

  if (max_y < PET_TOP + 8)
    {
      max_y = PET_TOP + 8;
    }

  g_pet.target_x = rand_range(PET_MARGIN, max_x - PET_MARGIN);
  g_pet.target_y = rand_range(PET_TOP, max_y - PET_TOP);
  g_pet.has_target = true;
}

enum pet_mode_e pet_engine_parse_mode(const char *mode)
{
  if (mode != NULL && strcmp(mode, "follow") == 0)
    {
      return PET_FOLLOW;
    }

  if (mode != NULL && strcmp(mode, "still") == 0)
    {
      return PET_STILL;
    }

  return PET_WANDER;
}

void pet_engine_init(int screen_w, int screen_h)
{
  memset(&g_pet, 0, sizeof(g_pet));
  g_pet.rng = 0x9e3779b9u;
  g_pet.screen_w = screen_w > 0 ? screen_w : 1024;
  g_pet.screen_h = screen_h > 0 ? screen_h : 600;
  g_pet.mode = PET_WANDER;
  g_pet.dir = PET_DOWN;
  g_pet.facing = 1;
  g_pet.visible = true;
  g_pet.mood = 72;
  g_pet.hunger = 28;
  apply_size("medium");
  g_pet.x = g_pet.screen_w - g_pet.w - 48;
  g_pet.y = g_pet.screen_h - g_pet.h - 40;
  clamp_pos();
  g_pet.rest_ms = 1200;
}

void pet_engine_set_rng(unsigned seed)
{
  g_pet.rng = seed ? seed : 1u;
}

void pet_engine_set_bounds(int screen_w, int screen_h)
{
  if (screen_w > 0)
    {
      g_pet.screen_w = screen_w;
    }

  if (screen_h > 0)
    {
      g_pet.screen_h = screen_h;
    }

  clamp_pos();
}

void pet_engine_tick(int dt_ms)
{
  int step;
  int dx;
  int dy;
  int dist;
  int nx;
  int ny;

  if (dt_ms <= 0)
    {
      dt_ms = 16;
    }
  if (dt_ms > 1000) dt_ms = 1000;

  if (g_pet.jump_t > 0)
    {
      g_pet.jump_t = g_pet.jump_t > dt_ms ? g_pet.jump_t - dt_ms : 0;
    }

  if (g_pet.action_t > 0)
    {
      g_pet.action_t = g_pet.action_t > dt_ms ? g_pet.action_t - dt_ms : 0;
    }

  if (g_pet.speak_cool_ms > 0)
    {
      g_pet.speak_cool_ms = g_pet.speak_cool_ms > dt_ms ?
                            g_pet.speak_cool_ms - dt_ms : 0;
    }

  if (g_pet.bubble_ms > 0)
    {
      g_pet.bubble_ms = g_pet.bubble_ms > dt_ms ?
                        g_pet.bubble_ms - dt_ms : 0;
      if (g_pet.bubble_ms == 0)
        {
          g_pet.bubble[0] = '\0';
          g_pet.bubble_inner = false;
        }
    }

  g_pet.hunger_acc += dt_ms;
  if (g_pet.hunger_acc >= 8000)
    {
      g_pet.hunger_acc = 0;
      g_pet.hunger = clamp_int(g_pet.hunger + 1, 0, 100);
      if (g_pet.mood > 40)
        {
          g_pet.mood--;
        }
    }

  if (!g_pet.visible || g_pet.dragging)
    {
      return;
    }

  if (g_pet.mode == PET_STILL)
    {
      if (g_pet.speak_cool_ms == 0 && (xorshift() % 200) == 0)
        {
          say_raw(pick(g_inner, sizeof(g_inner) / sizeof(g_inner[0])), true);
        }

      return;
    }

  if (g_pet.mode == PET_FOLLOW && g_pet.has_follow)
    {
      g_pet.target_x = g_pet.follow_x - g_pet.w / 2;
      g_pet.target_y = g_pet.follow_y - g_pet.h / 2;
      g_pet.has_target = true;
    }
  else if (g_pet.mode == PET_WANDER && !g_pet.has_target)
    {
      if (g_pet.rest_ms > 0)
        {
          g_pet.rest_ms = g_pet.rest_ms > dt_ms ? g_pet.rest_ms - dt_ms : 0;
          if (g_pet.speak_cool_ms == 0 && (xorshift() % 180) == 0)
            {
              if ((xorshift() & 1u) != 0)
                {
                  say_raw(pick(g_inner, sizeof(g_inner) / sizeof(g_inner[0])),
                          true);
                }
              else
                {
                  say_raw(pick(g_lines, sizeof(g_lines) / sizeof(g_lines[0])),
                          false);
                }
            }

          return;
        }

      pick_wander_target();
    }

  if (!g_pet.has_target)
    {
      return;
    }

  dx = g_pet.target_x - g_pet.x;
  dy = g_pet.target_y - g_pet.y;
  dist = dx * dx + dy * dy;
  if (dist < 100)
    {
      g_pet.has_target = false;
      g_pet.dir = PET_DOWN;
      g_pet.facing = 1;
      g_pet.rest_ms = REST_MIN_MS + rand_range(0, REST_SPAN_MS);
      return;
    }

  step = PET_SPEED * dt_ms / 1000;
  if (step < 1)
    {
      step = 1;
    }

  nx = g_pet.x;
  ny = g_pet.y;
  if (dx * dx >= dy * dy)
    {
      nx += dx > 0 ? step : -step;
    }
  else
    {
      ny += dy > 0 ? step : -step;
    }

  g_pet.x = nx;
  g_pet.y = ny;
  clamp_pos();
  set_dir_from_delta(dx, dy);
  if ((xorshift() % 400) == 0)
    {
      g_pet.jump_t = 400;
    }
}

void pet_engine_get(struct pet_state_s *out)
{
  if (out == NULL)
    {
      return;
    }

  memset(out, 0, sizeof(*out));
  out->mode = g_pet.mode;
  out->dir = g_pet.dir;
  out->facing = g_pet.facing;
  out->x = g_pet.x;
  out->y = g_pet.y;
  out->w = g_pet.w;
  out->h = g_pet.h;
  out->screen_w = g_pet.screen_w;
  out->screen_h = g_pet.screen_h;
  out->mood = g_pet.mood;
  out->hunger = g_pet.hunger;
  out->visible = g_pet.visible;
  out->dragging = g_pet.dragging;
  out->jump_t = g_pet.jump_t;
  out->action_t = g_pet.action_t;
  out->event_seq = g_pet.event_seq;
  out->bubble_inner = g_pet.bubble_inner;
  copy_text(out->size_id, sizeof(out->size_id), g_pet.size_id);
  copy_text(out->bubble, sizeof(out->bubble), g_pet.bubble);
  copy_text(out->last_event, sizeof(out->last_event), g_pet.last_event);
  copy_text(out->last_reply, sizeof(out->last_reply), g_pet.last_reply);
  copy_text(out->toast, sizeof(out->toast), g_pet.toast);
  if (g_pet.mode == PET_FOLLOW)
    {
      copy_text(out->mode_id, sizeof(out->mode_id), "follow");
    }
  else if (g_pet.mode == PET_STILL)
    {
      copy_text(out->mode_id, sizeof(out->mode_id), "still");
    }
  else
    {
      copy_text(out->mode_id, sizeof(out->mode_id), "wander");
    }

  if (g_pet.dir == PET_UP)
    {
      copy_text(out->dir_id, sizeof(out->dir_id), "up");
    }
  else if (g_pet.dir == PET_LEFT)
    {
      copy_text(out->dir_id, sizeof(out->dir_id), "left");
    }
  else if (g_pet.dir == PET_RIGHT)
    {
      copy_text(out->dir_id, sizeof(out->dir_id), "right");
    }
  else
    {
      copy_text(out->dir_id, sizeof(out->dir_id), "down");
    }
}

void pet_engine_set_mode(enum pet_mode_e mode)
{
  g_pet.mode = mode;
  g_pet.has_target = false;
  g_pet.rest_ms = 0;
  if (mode == PET_FOLLOW && !g_pet.has_follow)
    {
      g_pet.follow_x = g_pet.screen_w / 2;
      g_pet.follow_y = g_pet.screen_h / 2;
      g_pet.has_follow = true;
    }

  emit("mode", mode == PET_FOLLOW ? "跟着你" :
       (mode == PET_STILL ? "原地待着" : "自由散步"));
}

void pet_engine_set_visible(bool on)
{
  g_pet.visible = on;
  emit(on ? "show" : "hide", on ? "我回来了" : "我隐身了");
}

void pet_engine_toggle_visible(void)
{
  pet_engine_set_visible(!g_pet.visible);
}

void pet_engine_set_size(const char *size_id)
{
  apply_size(size_id);
  clamp_pos();
  emit("size", g_pet.size_id);
}

void pet_engine_poke(void)
{
  g_pet.jump_t = 500;
  g_pet.mood = clamp_int(g_pet.mood + 3, 0, 100);
  say_raw(pick(g_react, sizeof(g_react) / sizeof(g_react[0])), false);
  emit("poke", g_pet.bubble);
}

void pet_engine_feed(const char *food)
{
  const char *line = "好吃！";

  if (food != NULL && (strstr(food, "cake") != NULL ||
                       strstr(food, "蛋糕") != NULL))
    {
      line = "蛋糕！罪恶但快乐……";
    }
  else if (food != NULL && (strstr(food, "candy") != NULL ||
                            strstr(food, "糖") != NULL))
    {
      line = "棒棒糖！转圈圈～";
    }
  else if (food != NULL && (strstr(food, "dango") != NULL ||
                            strstr(food, "团子") != NULL))
    {
      line = "三色团子！软乎乎～";
    }
  else if (food != NULL && (strstr(food, "gem") != NULL ||
                            strstr(food, "钻石") != NULL))
    {
      line = "钻石？！这能吃吗……咕咚。真香！";
    }
  else
    {
      line = "小鱼干！我的最爱！";
    }

  g_pet.jump_t = 400;
  g_pet.hunger = clamp_int(g_pet.hunger - 18, 0, 100);
  g_pet.mood = clamp_int(g_pet.mood + 8, 0, 100);
  say_raw(line, false);
  emit("feed", line);
}

void pet_engine_say(const char *text, bool inner)
{
  say_raw(text, inner);
  emit("say", text);
}

void pet_engine_set_weather_cb(pet_weather_fn fn)
{
  g_pet.weather_cb = fn;
}

static const char *weather_reply(void)
{
  char line[64];

  copy_text(line, sizeof(line), "今天适合摸鱼，别问温度。");
  if (g_pet.weather_cb != NULL)
    {
      char filled[64];

      if (g_pet.weather_cb(filled, sizeof(filled)) == 0 && filled[0] != '\0')
        {
          copy_text(line, sizeof(line), filled);
        }
    }

  copy_text(g_pet.last_reply, sizeof(g_pet.last_reply), line);
  return g_pet.last_reply;
}

void pet_engine_weather(void)
{
  weather_reply();
  say_raw(g_pet.last_reply, false);
  emit("weather", g_pet.last_reply);
}

void pet_engine_chat(const char *user_msg)
{
  const char *reply;

  if (user_msg == NULL || user_msg[0] == '\0')
    {
      reply = "你倒是说话呀";
    }
  else if (strstr(user_msg, "天气") != NULL)
    {
      weather_reply();
      say_raw(g_pet.last_reply, false);
      emit("chat", g_pet.last_reply);
      return;
    }
  else if (strstr(user_msg, "你好") != NULL || strstr(user_msg, "嗨") != NULL)
    {
      reply = "来啦来啦，碳基生物。";
    }
  else if (strstr(user_msg, "饿") != NULL || strstr(user_msg, "吃") != NULL)
    {
      reply = "投喂小鱼干，谢谢老板。";
    }
  else if (strstr(user_msg, "困") != NULL || strstr(user_msg, "睡") != NULL)
    {
      reply = "我先去吃饭啦！模型以后再说。";
    }
  else if (strstr(user_msg, "训练") != NULL || strstr(user_msg, "AGI") != NULL)
    {
      reply = "去别的地方玩！不要耽误AGI训练！";
    }
  else
    {
      reply = pick(g_lines, sizeof(g_lines) / sizeof(g_lines[0]));
    }

  copy_text(g_pet.last_reply, sizeof(g_pet.last_reply), reply);
  say_raw(reply, false);
  emit("chat", reply);
}

void pet_engine_set_follow(int x, int y)
{
  g_pet.follow_x = clamp_int(x, 0, g_pet.screen_w);
  g_pet.follow_y = clamp_int(y, 0, g_pet.screen_h);
  g_pet.has_follow = true;
  if (g_pet.mode != PET_FOLLOW)
    {
      pet_engine_set_mode(PET_FOLLOW);
    }
}

void pet_engine_drag_begin(int x, int y)
{
  g_pet.dragging = true;
  g_pet.has_target = false;
  g_pet.drag_off_x = x - g_pet.x;
  g_pet.drag_off_y = y - g_pet.y;
}

void pet_engine_drag_move(int x, int y)
{
  int nx;
  int ny;

  if (!g_pet.dragging)
    {
      return;
    }

  nx = x - g_pet.drag_off_x;
  ny = y - g_pet.drag_off_y;
  set_dir_from_delta(nx - g_pet.x, ny - g_pet.y);
  g_pet.x = nx;
  g_pet.y = ny;
  clamp_pos();
}

void pet_engine_drag_end(void)
{
  if (!g_pet.dragging)
    {
      return;
    }

  g_pet.dragging = false;
  g_pet.dir = PET_DOWN;
  g_pet.facing = 1;
  g_pet.rest_ms = REST_MIN_MS + rand_range(0, REST_SPAN_MS);
  if ((xorshift() & 1u) != 0)
    {
      say_raw(pick(g_drag, sizeof(g_drag) / sizeof(g_drag[0])), false);
    }

  emit("drag", g_pet.bubble);
}
