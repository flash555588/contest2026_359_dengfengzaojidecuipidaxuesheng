/* SPDX-License-Identifier: Apache-2.0 */
#include "camera_uvc_protocol.h"
#include <errno.h>
#include <string.h>

static uint16_t le16(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p)
{ return le16(p) | ((uint32_t)le16(p + 2) << 16); }

bool camera_uvc_interval_valid(const struct uvc_camera_mode *m, uint32_t v)
{
  if (!v) return false;
  if (m->continuous)
    return v >= m->intervals[0] && v <= m->intervals[1] &&
           (!m->intervals[2] || (v - m->intervals[0]) % m->intervals[2] == 0);
  for (unsigned i = 0; i < m->interval_count; i++)
    if (m->intervals[i] == v) return true;
  return false;
}

int camera_uvc_parse(const uint8_t *p, size_t size, uint8_t stream,
                     struct uvc_descriptor_info *o)
{
  uint8_t current = 255, subclass = 0, alt = 0, format = 0;
  bool mjpeg = false;
  if (!p || !o || size < 9 || p[0] != 9 || p[1] != 2 || le16(p + 2) != size)
    return -EINVAL;
  memset(o, 0, sizeof(*o));
  o->stream_interface = stream;
  o->control_interface = 255;
  for (size_t offset = 0; offset < size;)
    {
      const uint8_t *d = p + offset;
      size_t n = d[0];
      if (size - offset < 2 || n < 2 || n > size - offset) return -EINVAL;
      if (d[1] == 4)
        {
          if (n < 9) return -EINVAL;
          current = d[2]; alt = d[3]; subclass = d[5] == 14 ? d[6] : 0;
          mjpeg = false; format = 0;
        }
      else if (d[1] == 0x24 && subclass && alt == 0)
        {
          if (n < 3) return -EINVAL;
          if (subclass == 1 && d[2] == 1)
            {
              if (n < 12 || n < 12u + d[11]) return -EINVAL;
              for (unsigned i = 0; i < d[11]; i++)
                if (d[12 + i] == stream)
                  { o->control_interface = current; o->caps.uvc_version = le16(d + 3); }
            }
          if (subclass == 2 && current == stream)
            {
              if (d[2] == 1)
                { if (n < 13) return -EINVAL; o->endpoint = d[6]; }
              else if (d[2] == 6)
                { if (n < 11 || !d[3]) return -EINVAL; mjpeg = true; format = d[3]; }
              else if (d[2] == 4 || d[2] == 10 || d[2] == 16)
                { mjpeg = false; format = 0; }
              else if (d[2] == 7 && mjpeg)
                {
                  if (n < 26 || !d[3]) return -EINVAL;
                  unsigned intervals = d[25];
                  if (n < (intervals ? 26u + 4u * intervals : 38u)) return -EINVAL;
                  if (o->caps.count == UVC_CAMERA_MAX_MODES)
                    { o->caps.truncated = 1; offset += n; continue; }
                  struct uvc_camera_mode *m = &o->caps.modes[o->caps.count];
                  m->width = le16(d + 5); m->height = le16(d + 7);
                  m->max_frame_size = le32(d + 17);
                  m->default_interval = le32(d + 21);
                  m->format_index = format; m->frame_index = d[3];
                  m->continuous = intervals == 0;
                  m->interval_count = intervals ? intervals : 3;
                  if (m->interval_count > UVC_CAMERA_MAX_INTERVALS)
                    { m->interval_count = UVC_CAMERA_MAX_INTERVALS; o->caps.truncated = 1; }
                  for (unsigned i = 0; i < m->interval_count; i++) m->intervals[i] = le32(d + 26 + 4 * i);
                  if (!m->width || !m->height || !m->default_interval) return -EINVAL;
                  if (m->continuous && (!m->intervals[0] || m->intervals[1] < m->intervals[0])) return -EINVAL;
                  for (unsigned i = 0; !m->continuous && i < m->interval_count; i++)
                    if (!m->intervals[i]) return -EINVAL;
                  if (!camera_uvc_interval_valid(m, m->default_interval)) m->default_interval = m->intervals[0];
                  o->caps.count++;
                }
            }
        }
      offset += n;
    }
  return o->control_interface == 255 || !(o->endpoint & 0x80) ? -ENODEV : 0;
}

int camera_uvc_payload(struct uvc_payload_state *s, const uint8_t *p,
                       size_t n, uint8_t *frame, size_t cap)
{
  if (n < 2 || p[0] < 2 || p[0] > n)
    { s->damaged = true; return -EINVAL; }
  uint8_t header = p[0], flags = p[1];
  if (header < 2u + ((flags & 4) ? 4u : 0u) + ((flags & 8) ? 6u : 0u))
    { s->damaged = true; return -EINVAL; }
  int fid = flags & 1;
  if (s->fid != fid)
    { s->fid = fid; s->used = 0; s->damaged = false; s->ended = false; }
  if (flags & 0x40) s->damaged = true;
  if (s->ended || s->damaged) return 0;
  size_t bytes = n - header;
  if (bytes > cap - s->used) { s->damaged = true; return -EOVERFLOW; }
  memcpy(frame + s->used, p + header, bytes);
  s->used += bytes;
  if (!(flags & 2)) return 0;
  s->ended = true;
  if (s->used < 4 || frame[0] != 0xff || frame[1] != 0xd8 ||
      frame[s->used - 2] != 0xff || frame[s->used - 1] != 0xd9)
    { s->damaged = true; return -EBADMSG; }
  return (int)s->used;
}
