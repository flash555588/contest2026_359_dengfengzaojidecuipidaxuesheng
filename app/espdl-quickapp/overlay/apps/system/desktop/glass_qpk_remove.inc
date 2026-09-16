/* SPDX-License-Identifier: Apache-2.0 */
static bool remove_component(const char *text)
{
  if (!text || !*text || *text == '.' || strlen(text) > 47) return false;
  for (const unsigned char *p = (const unsigned char *)text; *p; p++)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
          (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' || *p == '-')) return false;
  return true;
}

bool glass_qpk_removable(const char *dir, const char *package)
{
  const char *protected[] = {"music", "pinyin", "ouo", "com.example.hello", "com.example.game2048"};
  if (!remove_component(dir) || !remove_component(package)) return false;
  if (!strncmp(dir, "com.openvela.", 13) || !strncmp(package, "com.openvela.", 13)) return false;
  for (unsigned i = 0; i < sizeof(protected) / sizeof(protected[0]); i++)
    if (!strcmp(dir, protected[i]) || !strcmp(package, protected[i])) return false;
  return true;
}

static int remove_installed(const char *dir, const char *package)
{
  if (!glass_qpk_removable(dir, package)) return -EPERM;
  pthread_mutex_lock(&io_lock);
  char path[256], manifest[288];
  int ret = root_ready();
  snprintf(path, sizeof(path), QPK_BUILDER_ROOT "/%s", dir);
  if (!ret) ret = directory(path, false);
  snprintf(manifest, sizeof(manifest), "%s/manifest.json", path);
  char *text = NULL;
  if (!ret) ret = read_file(manifest, 1024, &text);
  cJSON *root = text ? parse(text) : NULL;
  cJSON *pkg = cJSON_GetObjectItemCaseSensitive(root, "package");
  if (!ret && (!cJSON_IsString(pkg) || strcmp(pkg->valuestring, package))) ret = -ESTALE;
  cJSON_Delete(root); free(text);
  if (!ret) ret = qpk_storage_remove_package(QPK_BUILDER_ROOT "/.data", package);
  /* Keep the manifest until all other files are gone: failed cleanup can be
   * retried from the same tile instead of silently leaving hidden files. */
  DIR *dp = !ret ? opendir(path) : NULL;
  if (!ret && !dp) ret = -errno;
  if (dp) {
    struct dirent *entry;
    while (!ret) {
      errno = 0; entry = readdir(dp);
      if (!entry) { if (errno) ret = -errno; break; }
      if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") || !strcmp(entry->d_name, "manifest.json")) continue;
      char child[512];
      if (snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) >= (int)sizeof(child)) ret = -ENAMETOOLONG;
      else ret = qpk_remove_tree(child);
    }
    closedir(dp);
  }
  if (!ret && unlink(manifest)) ret = -errno;
  if (!ret && rmdir(path)) ret = -errno;
  if (!ret) {
    pthread_mutex_lock(&state_lock);
    if (!strcmp(info.installed_package, package)) { info.saved_revision = 0; info.installed_package[0] = 0; }
    pthread_mutex_unlock(&state_lock);
  }
  pthread_mutex_unlock(&io_lock);
  return ret;
}
