/* SPDX-License-Identifier: Apache-2.0 */
/* Preserve the negotiated image resolution and avoid writing an expanded
 * screen-sized bitmap to slow flash. The decoder has validated this JPEG
 * and inserted standard Huffman tables where the camera omitted them. */
static void qpk_camera_save_jpeg(const uint8_t *image, size_t size)
{
  pthread_mutex_lock(&g_camera_lock);
  bool pending = g_photo_pending;
  g_photo_pending = false;
  pthread_mutex_unlock(&g_camera_lock);
  if (!pending) return;
  char path[128];
  FILE *file = NULL;
  int error = 0;
  if (mkdir(QPK_PHOTO_DIR, 0755) < 0 && errno != EEXIST) error = errno;
  if (!error)
    {
      do
        {
          snprintf(path, sizeof(path), QPK_PHOTO_DIR "/camera-%08u.jpg", ++g_photo_sequence);
          file = fopen(path, "wbx");
        }
      while (!file && errno == EEXIST && g_photo_sequence < 100000);
      if (!file) error = errno ? errno : EIO;
    }
  if (file)
    {
      for (size_t offset = 0; !error && offset < size;)
        {
          if (qpk_camera_cancelled()) { error = ECANCELED; break; }
          size_t count = size - offset;
          if (count > 4096) count = 4096;
          if (fwrite(image + offset, 1, count, file) != count) error = EIO;
          offset += count;
        }
      if (!error && (fflush(file) || fsync(fileno(file)))) error = EIO;
      if (fclose(file) && !error) error = EIO;
      if (error) unlink(path);
    }
  pthread_mutex_lock(&g_camera_lock);
  g_photo_status = error ? -error : 2;
  if (!error) snprintf(g_photo_path, sizeof(g_photo_path), "%s", path);
  pthread_mutex_unlock(&g_camera_lock);
  printf("[qpk] JPEG photo status=%d bytes=%lu\n", error ? -error : 2, (unsigned long)size);
}

/* P4 manages PSRAM cache through Espressif's HAL: CONFIG_ARCH_DCACHE is
 * unset, so generic up_clean_dcache() expands to nothing on this build.
 * Use the same real C2M writeback as the LCD driver, for all three pages. */
extern int esp_mipi_dsi_flush_framebuffer(void *addr, size_t len);

static int qpk_camera_clean_frame(uint16_t *pixels, size_t bytes)
{
  uintptr_t address = (uintptr_t)pixels;
  while (bytes)
    {
      size_t chunk = bytes > 16384 ? 16384 : bytes;
      int ret = esp_mipi_dsi_flush_framebuffer((void *)address, chunk);
      if (ret < 0) return ret;
      address += chunk;
      bytes -= chunk;
    }
  return 0;
}

/* Runs entirely outside LVGL. Decoder and input buffers belong to this
 * worker and are released before cleanup joins and resumes the desktop. */
static void *qpk_camera_usb_thread(pthread_addr_t arg)
{
  struct qpk_camera_s *camera = arg;
  uint8_t *input = NULL;
  void *decoder = NULL;
  int error = 0;
  struct uvc_camera_start mode = camera->usb_mode;
  if (qpk_camera_cancelled()) goto done;
  if (ioctl(camera->fd, UVCIOC_START, (unsigned long)&mode) < 0)
    { error = -errno; goto done; }
  pthread_mutex_lock(&g_camera_lock);
  camera->usb_mode = mode;
  pthread_mutex_unlock(&g_camera_lock);
  if (qpk_camera_cancelled()) goto done;
  input = malloc(mode.max_frame_size + 512);
  decoder = qpk_mjpeg_create();
  if (!input || !decoder) { error = -ENOMEM; goto done; }
  uint64_t last_frame = qpk_now_ms();
  unsigned decode_failures = 0;
  int bar_status[QPK_CAMERA_FRAMEBUFFER_COUNT];
  bool bar_valid[QPK_CAMERA_FRAMEBUFFER_COUNT] = { false };
  struct pollfd pfd = { .fd = camera->fd, .events = POLLIN };
  while (!qpk_camera_cancelled())
    {
      int ret = poll(&pfd, 1, 100);
      if (qpk_camera_cancelled()) break;
      if (ret < 0) { if (errno == EINTR) continue; error = -errno; break; }
      if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) { error = -ENODEV; break; }
      if (!ret)
        { if (qpk_now_ms() - last_frame > 6000) { error = -ETIMEDOUT; break; } continue; }
      ssize_t size = read(camera->fd, input, mode.max_frame_size);
      if (size < 0) { if (errno == EAGAIN || errno == EINTR) continue; error = -errno; break; }
      if (!size) continue;
      unsigned next = (camera->fb_page + 1) % QPK_CAMERA_FRAMEBUFFER_COUNT;
      uint16_t *target = (uint16_t *)((uint8_t *)camera->fb_plane.fbmem + next * QPK_CAMERA_FRAME_SIZE);
      ret = qpk_mjpeg_decode(decoder, input, size, mode.max_frame_size + 512,
                             target, QPK_CAMERA_RAW_WIDTH, QPK_CAMERA_BAR_TOP,
                             mode.width, mode.height);
      if (ret < 0)
        { if (++decode_failures == 10) { error = ret; break; } continue; }
      decode_failures = 0;
      last_frame = qpk_now_ms();
      camera->captured_frames++;
      if (qpk_camera_cancelled()) break;
      qpk_camera_save_jpeg(input, qpk_mjpeg_frame_size(decoder));
      pthread_mutex_lock(&g_camera_lock);
      int photo_status = g_photo_status;
      pthread_mutex_unlock(&g_camera_lock);
      size_t dirty_bytes = QPK_CAMERA_RAW_WIDTH * QPK_CAMERA_BAR_TOP * sizeof(uint16_t);
      /* The decoder owns only the image area. Preserve each off-screen
       * page's controls instead of clearing/rebuilding them for every frame. */
      if (!bar_valid[next] || bar_status[next] != photo_status)
        {
          bar_status[next] = qpk_camera_bar_blit(target);
          bar_valid[next] = true;
          dirty_bytes = QPK_CAMERA_FRAME_SIZE;
        }
      ret = qpk_camera_clean_frame(target, dirty_bytes);
      if (ret < 0) { error = ret; break; }
      camera->fb_plane.yoffset = next * QPK_CAMERA_RAW_HEIGHT;
      if (ioctl(camera->fb_fd, FBIOPAN_DISPLAY, (uintptr_t)&camera->fb_plane) < 0)
        { camera->pan_errors++; error = -errno; break; }
      /* The older framebuffer upper half drops pandisplay's return value.
       * Confirm scanout changed before allowing another page to be reused. */
      struct fb_planeinfo_s displayed = { .display = 0 };
      if (ioctl(camera->fb_fd, FBIOGET_PLANEINFO, (uintptr_t)&displayed) < 0 ||
          displayed.yoffset != camera->fb_plane.yoffset)
        { camera->pan_errors++; error = -EIO; break; }
      camera->fb_page = next;
      pthread_mutex_lock(&g_camera_lock);
      camera->frames++;
      pthread_mutex_unlock(&g_camera_lock);
      if (camera->frames == 1) printf("[qpk] USB MJPEG first frame %ux%u\n", mode.width, mode.height);
    }
done:
  ioctl(camera->fd, UVCIOC_STOP, 0);
  qpk_mjpeg_destroy(decoder);
  free(input);
  pthread_mutex_lock(&g_camera_lock);
  camera->thread_error = error;
  camera->thread_alive = false;
  pthread_mutex_unlock(&g_camera_lock);
  return NULL;
}
