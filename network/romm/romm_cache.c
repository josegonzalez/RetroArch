/*  RetroArch - A frontend for libretro.
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <string/stdstring.h>
#include <compat/strl.h>
#include <file/file_path.h>
#include <lists/dir_list.h>
#include <streams/file_stream.h>

#include "romm_cache.h"
#include "../../configuration.h"

bool romm_cache_dir(char *s, size_t len)
{
   settings_t *settings = config_get_ptr();
   if (!s || len == 0 || !settings)
      return false;
   if (!string_is_empty(settings->paths.romm_cache_dir))
      strlcpy(s, settings->paths.romm_cache_dir, len);
   else
   {
      if (string_is_empty(settings->paths.directory_core_assets))
      {
         s[0] = '\0';
         return false;
      }
      fill_pathname_join_special(s,
            settings->paths.directory_core_assets, "romm", len);
   }
   if (!path_is_directory(s))
      path_mkdir(s);
   return true;
}

void romm_cache_platform_dir(char *s, size_t len, const char *platform_slug)
{
   char base[DIR_MAX_LENGTH];
   if (!s || len == 0)
      return;
   if (!romm_cache_dir(base, sizeof(base)))
   {
      s[0] = '\0';
      return;
   }
   fill_pathname_join_special(s, base,
         (platform_slug && *platform_slug) ? platform_slug : "_", len);
}

void romm_cache_rom_path(char *s, size_t len,
      const char *platform_slug, const char *fs_name)
{
   char dir[DIR_MAX_LENGTH];
   if (!s || len == 0)
      return;
   romm_cache_platform_dir(dir, sizeof(dir), platform_slug);
   if (string_is_empty(dir))
   {
      s[0] = '\0';
      return;
   }
   fill_pathname_join_special(s, dir, fs_name ? fs_name : "", len);
}

enum romm_cache_mode romm_cache_get_mode(void)
{
   settings_t *settings = config_get_ptr();
   if (!settings)
      return ROMM_CACHE_KEEP;
   return (enum romm_cache_mode)settings->uints.romm_cache_mode;
}

unsigned romm_cache_max_size_mb(void)
{
   settings_t *settings = config_get_ptr();
   return settings ? settings->uints.romm_cache_max_size_mb : 0;
}

static void romm_sidecar_path(char *s, size_t len, const char *content_path)
{
   char dir[DIR_MAX_LENGTH];
   char name[320];
   const char *bn = path_basename(content_path);
   if (!s || len == 0)
      return;
   fill_pathname_basedir(dir, content_path, sizeof(dir));
   snprintf(name, sizeof(name), ".romm.%s.txt", bn ? bn : "");
   fill_pathname_join_special(s, dir, name, len);
}

bool romm_cache_write_sidecar(const char *content_path, int rom_id,
      int platform_id)
{
   char sc[PATH_MAX_LENGTH];
   char buf[160];
   size_t l;
   if (string_is_empty(content_path))
      return false;
   romm_sidecar_path(sc, sizeof(sc), content_path);
   l = snprintf(buf, sizeof(buf),
         "rom_id=%d\nplatform_id=%d\nlast_access=%lld\n",
         rom_id, platform_id, (long long)time(NULL));
   return filestream_write_file(sc, buf, l);
}

static long long romm_sidecar_last_access(const char *content_path)
{
   char sc[PATH_MAX_LENGTH];
   char line[160];
   RFILE *f;
   long long ts = 0;
   romm_sidecar_path(sc, sizeof(sc), content_path);
   if (!(f = filestream_open(sc, RETRO_VFS_FILE_ACCESS_READ,
               RETRO_VFS_FILE_ACCESS_HINT_NONE)))
      return 0;
   while (filestream_gets(f, line, sizeof(line)))
   {
      if (!strncmp(line, "last_access=", 12))
         ts = strtoll(line + 12, NULL, 10);
   }
   filestream_close(f);
   return ts;
}

bool romm_cache_read_sidecar(const char *content_path, int *rom_id,
      int *platform_id)
{
   char sc[PATH_MAX_LENGTH];
   char line[160];
   RFILE *f;
   bool got = false;
   if (string_is_empty(content_path))
      return false;
   romm_sidecar_path(sc, sizeof(sc), content_path);
   if (!(f = filestream_open(sc, RETRO_VFS_FILE_ACCESS_READ,
               RETRO_VFS_FILE_ACCESS_HINT_NONE)))
      return false;
   while (filestream_gets(f, line, sizeof(line)))
   {
      if (!strncmp(line, "rom_id=", 7))
      {
         if (rom_id)
            *rom_id = (int)strtol(line + 7, NULL, 10);
         got = true;
      }
      else if (!strncmp(line, "platform_id=", 12))
      {
         if (platform_id)
            *platform_id = (int)strtol(line + 12, NULL, 10);
      }
   }
   filestream_close(f);
   return got;
}

bool romm_cache_is_cached_content(const char *content_path, int *rom_id)
{
   char base[DIR_MAX_LENGTH];
   if (string_is_empty(content_path))
      return false;
   if (!romm_cache_dir(base, sizeof(base)))
      return false;
   if (strncmp(content_path, base, strlen(base)) != 0)
      return false;
   return romm_cache_read_sidecar(content_path, rom_id, NULL);
}

void romm_cache_touch(const char *content_path)
{
   int rid = 0, pid = 0;
   if (romm_cache_read_sidecar(content_path, &rid, &pid))
      romm_cache_write_sidecar(content_path, rid, pid);
}

void romm_cache_remove(const char *content_path)
{
   char sc[PATH_MAX_LENGTH];
   if (string_is_empty(content_path))
      return;
   romm_sidecar_path(sc, sizeof(sc), content_path);
   if (path_is_valid(content_path))
      filestream_delete(content_path);
   if (path_is_valid(sc))
      filestream_delete(sc);
}

static int64_t romm_file_size(const char *path)
{
   int64_t sz;
   RFILE *f = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ,
         RETRO_VFS_FILE_ACCESS_HINT_NONE);
   if (!f)
      return 0;
   sz = filestream_get_size(f);
   filestream_close(f);
   return sz < 0 ? 0 : sz;
}

/* NOTE: eviction is per-file. A multi-file (multi-disc) game cached as a folder
 * of files could have one disc evicted; Phase 3 refines this to evict such games
 * as a unit. */
void romm_cache_evict_if_needed(int64_t incoming_bytes)
{
   typedef struct { char path[PATH_MAX_LENGTH]; int64_t size; long long atime; bool gone; } ent_t;
   char base[DIR_MAX_LENGTH];
   struct string_list *list;
   ent_t  *ents;
   size_t  i, n = 0;
   int64_t cap, total = 0;

   if (romm_cache_get_mode() != ROMM_CACHE_LRU)
      return;
   cap = (int64_t)romm_cache_max_size_mb() * 1024 * 1024;
   if (cap <= 0)
      return;
   if (!romm_cache_dir(base, sizeof(base)))
      return;

   /* Recursive listing; show_hidden=false excludes the .romm.*.txt sidecars. */
   if (!(list = dir_list_new(base, NULL, false, false, true, true)))
      return;
   if (list->size == 0)
   {
      string_list_free(list);
      return;
   }
   if (!(ents = (ent_t*)calloc(list->size, sizeof(ent_t))))
   {
      string_list_free(list);
      return;
   }

   for (i = 0; i < list->size; i++)
   {
      const char *p = list->elems[i].data;
      if (!p || list->elems[i].attr.i == RARCH_DIRECTORY)
         continue;
      strlcpy(ents[n].path, p, sizeof(ents[n].path));
      ents[n].size  = romm_file_size(p);
      ents[n].atime = romm_sidecar_last_access(p);
      total        += ents[n].size;
      n++;
   }
   string_list_free(list);

   while (total + incoming_bytes > cap)
   {
      size_t  oldest = (size_t)-1;
      long long best = 0;
      for (i = 0; i < n; i++)
      {
         if (ents[i].gone)
            continue;
         if (oldest == (size_t)-1 || ents[i].atime < best)
         {
            oldest = i;
            best   = ents[i].atime;
         }
      }
      if (oldest == (size_t)-1)
         break;
      romm_cache_remove(ents[oldest].path);
      total -= ents[oldest].size;
      ents[oldest].gone = true;
   }

   free(ents);
}

/* ============================================================
 * RESPONSE CACHE (raw API JSON, with an embedded wall-clock TTL)
 * ============================================================ */

static void romm_response_cache_dir(char *s, size_t len)
{
   char base[DIR_MAX_LENGTH];
   if (!s || len == 0)
      return;
   if (!romm_cache_dir(base, sizeof(base)))
   {
      s[0] = '\0';
      return;
   }
   fill_pathname_join_special(s, base, "responses", len);
   if (!path_is_directory(s))
      path_mkdir(s);
}

static void romm_response_cache_path(char *s, size_t len, const char *key)
{
   char   dir[DIR_MAX_LENGTH];
   char   safe[192];
   size_t i;
   if (!s || len == 0)
      return;
   romm_response_cache_dir(dir, sizeof(dir));
   if (string_is_empty(dir) || string_is_empty(key))
   {
      s[0] = '\0';
      return;
   }
   for (i = 0; key[i] && i < sizeof(safe) - 6; i++)
   {
      char c = key[i];
      safe[i] = (   (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                 || (c >= '0' && c <= '9') || c == '-' || c == '.')
            ? c : '_';
   }
   safe[i] = '\0';
   strlcat(safe, ".json", sizeof(safe));
   fill_pathname_join_special(s, dir, safe, len);
}

char *romm_cache_response_load(const char *key, size_t *out_len)
{
   settings_t *settings = config_get_ptr();
   char        path[PATH_MAX_LENGTH];
   void       *buf       = NULL;
   int64_t     sz        = 0;
   char       *body, *nl;
   unsigned    minutes;
   size_t      bodylen;

   if (out_len)
      *out_len = 0;
   if (!settings || !settings->bools.romm_response_cache_enable)
      return NULL;
   if ((minutes = settings->uints.romm_response_cache_minutes) == 0)
      return NULL;
   romm_response_cache_path(path, sizeof(path), key);
   if (string_is_empty(path) || !path_is_valid(path))
      return NULL;
   if (!filestream_read_file(path, &buf, &sz) || !buf || sz <= 0)
   {
      if (buf)
         free(buf);
      return NULL;
   }
   /* First line is the unix-seconds timestamp the file was written. */
   if (!(nl = (char*)memchr(buf, '\n', (size_t)sz)))
   {
      free(buf);
      return NULL;
   }
   if ((long long)time(NULL) - strtoll((const char*)buf, NULL, 10)
         >= (long long)minutes * 60)
   {
      free(buf); /* expired */
      return NULL;
   }
   bodylen = (size_t)sz - (size_t)((nl + 1) - (char*)buf);
   if (!(body = (char*)malloc(bodylen + 1)))
   {
      free(buf);
      return NULL;
   }
   memcpy(body, nl + 1, bodylen);
   body[bodylen] = '\0';
   free(buf);
   if (out_len)
      *out_len = bodylen;
   return body;
}

void romm_cache_response_store(const char *key, const char *data, size_t len)
{
   settings_t *settings = config_get_ptr();
   char        path[PATH_MAX_LENGTH];
   char        hdr[32];
   RFILE      *f;
   if (!settings || !settings->bools.romm_response_cache_enable)
      return;
   if (!data || len == 0 || string_is_empty(key))
      return;
   /* romm_response_cache_path() creates the responses directory if needed. */
   romm_response_cache_path(path, sizeof(path), key);
   if (string_is_empty(path))
      return;
   if (!(f = filestream_open(path, RETRO_VFS_FILE_ACCESS_WRITE,
               RETRO_VFS_FILE_ACCESS_HINT_NONE)))
      return;
   snprintf(hdr, sizeof(hdr), "%lld\n", (long long)time(NULL));
   filestream_write(f, hdr, strlen(hdr));
   filestream_write(f, data, len);
   filestream_close(f);
}
