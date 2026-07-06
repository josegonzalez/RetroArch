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
#include <sys/stat.h>

#include <string/stdstring.h>
#include <compat/strl.h>
#include <file/file_path.h>
#include <streams/file_stream.h>
#include <lists/string_list.h>
#include <net/net_http.h>
#include <queues/message_queue.h>

#include "tasks_internal.h"
#include "../configuration.h"
#include "../paths.h"
#include "../runloop.h"
#include "../command.h"
#include "../verbosity.h"
#include "../msg_hash.h"

#include "../network/romm/romm_api.h"
#include "../network/romm/romm_cache.h"

#define ROMM_SYNC_MAX_FILES 24
#define ROMM_SYNC_BOUNDARY  "----RetroArchRomMSyncBoundaryK2p9"

enum romm_sync_phase
{
   ROMM_PHASE_SAVES = 0,
   ROMM_PHASE_STATES
};

/* A locally-computed save-state transfer (states are diffed client-side, not
 * negotiated). */
typedef struct
{
   bool is_upload;
   char local_path[PATH_MAX_LENGTH];
   char url[2048];
   char file_name[256];
} romm_state_op_t;

typedef struct
{
   int    rom_id;
   int    direction;
   bool   is_current;                  /* gather from runloop + report playtime */
   char   content_path[PATH_MAX_LENGTH];
   char   device_id[160];
   char   session_id[192];
   char   emulator[128];
   char   headers[1024];

   /* Battery saves (.srm) go through negotiate; save states are diffed. */
   char   saves[ROMM_SYNC_MAX_FILES][PATH_MAX_LENGTH];
   size_t save_count;
   char   states[ROMM_SYNC_MAX_FILES][PATH_MAX_LENGTH];
   size_t state_count;

   romm_sync_op_t *ops;
   size_t          op_count;
   size_t          op_index;

   romm_state_op_t state_ops[ROMM_SYNC_MAX_FILES * 2];
   size_t          state_op_count;
   size_t          state_op_index;

   int    phase;
   char   cur_dest[PATH_MAX_LENGTH];   /* in-flight download target */

   int    completed;
   int    failed;
   long long play_usec;                /* current-session runtime for playtime */

   void (*on_done)(void*);             /* fired on every terminal path */
   void  *on_done_data;
} romm_sync_state_t;

/* Forward declarations (callbacks reference each other through the chain). */
static void romm_sync_negotiate(romm_sync_state_t *st);
static void romm_sync_run_op(romm_sync_state_t *st);
static void romm_sync_states_begin(romm_sync_state_t *st);
static void romm_sync_run_state_op(romm_sync_state_t *st);
static void romm_sync_finalize(romm_sync_state_t *st);
static void romm_sync_device_cb(retro_task_t *task, void *task_data,
      void *user_data, const char *err);
static void romm_sync_start(romm_sync_state_t *st);

static void romm_sync_msg(const char *msg)
{
   runloop_msg_queue_push(msg, strlen(msg), 1, 150, true, NULL,
         MESSAGE_QUEUE_ICON_DEFAULT, MESSAGE_QUEUE_CATEGORY_INFO);
}

/* Terminal: fire the optional completion callback (pull-before-load uses it to
 * resume the deferred launch) then free. Always call this instead of freeing
 * directly, so the callback runs on every success/failure path. */
static void romm_sync_done(romm_sync_state_t *st)
{
   void (*cb)(void*);
   void  *cbdata;
   if (!st)
      return;
   cb     = st->on_done;
   cbdata = st->on_done_data;
   free(st->ops);
   free(st);
   if (cb)
      cb(cbdata);
}

/* ---------- time helpers ---------- */

/* Days since the unix epoch for a civil (proleptic Gregorian) date.
 * Howard Hinnant's algorithm; avoids the non-portable timegm(). */
static long long romm_days_from_civil(long long y, unsigned m, unsigned d)
{
   long long era;
   unsigned  yoe, doy, doe;
   y  -= (m <= 2);
   era = (y >= 0 ? y : y - 399) / 400;
   yoe = (unsigned)(y - era * 400);
   doy = (153 * (m + (m > 2 ? -3u : 9u)) + 2) / 5 + d - 1;
   doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
   return era * 146097 + (long long)doe - 719468;
}

/* Parse an ISO-8601 "YYYY-MM-DDThh:mm:ss" (UTC) into a unix timestamp. */
static long long romm_iso_to_unix(const char *s)
{
   int Y, M, D, h, mi, se;
   if (string_is_empty(s))
      return 0;
   h = mi = se = 0;
   if (sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &mi, &se) < 3)
      return 0;
   return romm_days_from_civil(Y, (unsigned)M, (unsigned)D) * 86400LL
        + (long long)h * 3600 + (long long)mi * 60 + se;
}

static void romm_unix_to_iso(long long t, char *s, size_t len)
{
   time_t    tt = (time_t)t;
   struct tm tmv;
#if defined(_WIN32)
   gmtime_s(&tmv, &tt);
#else
   gmtime_r(&tt, &tmv);
#endif
   strftime(s, len, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}

/* ---------- save/state file gathering ---------- */

static void romm_sync_add_file(romm_sync_state_t *st, const char *path)
{
   const char *ext;
   size_t      i;
   if (string_is_empty(path) || !path_is_valid(path))
      return;
   ext = path_get_extension(path);
   if (ext && string_is_equal_noncase(ext, "srm"))
   {
      for (i = 0; i < st->save_count; i++)
         if (string_is_equal(st->saves[i], path))
            return;
      if (st->save_count < ROMM_SYNC_MAX_FILES)
         strlcpy(st->saves[st->save_count++], path, PATH_MAX_LENGTH);
   }
   else if (ext && !strncmp(ext, "state", 5))
   {
      for (i = 0; i < st->state_count; i++)
         if (string_is_equal(st->states[i], path))
            return;
      if (st->state_count < ROMM_SYNC_MAX_FILES)
         strlcpy(st->states[st->state_count++], path, PATH_MAX_LENGTH);
   }
}

static void romm_sync_gather_current(romm_sync_state_t *st)
{
   runloop_state_t *runloop_st = runloop_state_get_ptr();
   int slot;
   romm_sync_add_file(st, runloop_st->name.savefile);
   romm_sync_add_file(st, runloop_st->name.savestate);
   for (slot = 0; slot <= 8; slot++)
   {
      char p[PATH_MAX_LENGTH];
      if (runloop_get_savestate_path(p, sizeof(p), slot))
         romm_sync_add_file(st, p);
   }
}

/* Derive the save/state paths for content that is NOT loaded, from the global
 * save/state dirs + the content base name. Default-config approximation (does
 * not honor per-core/per-content sort subdirs). */
static void romm_sync_gather_path(romm_sync_state_t *st, const char *content_path)
{
   const char *savedir  = dir_get_ptr(RARCH_DIR_SAVEFILE);
   const char *statedir = dir_get_ptr(RARCH_DIR_SAVESTATE);
   char base[PATH_MAX_LENGTH];
   char p[PATH_MAX_LENGTH];
   int  slot;

   fill_pathname_base(base, content_path, sizeof(base));
   path_remove_extension(base);
   if (string_is_empty(base))
      return;

   if (!string_is_empty(savedir) && !string_is_equal(savedir, "default"))
   {
      fill_pathname_join_special(p, savedir, base, sizeof(p));
      strlcat(p, ".srm", sizeof(p));
      romm_sync_add_file(st, p);
   }
   if (!string_is_empty(statedir) && !string_is_equal(statedir, "default"))
   {
      char stem[PATH_MAX_LENGTH];
      fill_pathname_join_special(stem, statedir, base, sizeof(stem));
      strlcpy(p, stem, sizeof(p));
      strlcat(p, ".state", sizeof(p));
      romm_sync_add_file(st, p);
      for (slot = 1; slot <= 8; slot++)
      {
         snprintf(p, sizeof(p), "%s.state%d", stem, slot);
         romm_sync_add_file(st, p);
      }
   }
}

/* ---------- request body builders ---------- */

static char *romm_sync_build_device_body(void)
{
   settings_t *settings = config_get_ptr();
   char       *buf;
   const char *mode;
   if (!(buf = (char*)malloc(1024)))
      return NULL;
   mode = (settings && settings->uints.cloud_sync_sync_mode
            == 0 /* AUTOMATIC */) ? "automatic" : "manual";
   snprintf(buf, 1024,
         "{\"name\":\"RetroArch\",\"platform\":\"retroarch\","
         "\"hostname\":\"retroarch\",\"mac\":\"\",\"sync_mode\":\"%s\","
         "\"paths\":{\"roms\":\"\",\"saves\":\"\",\"states\":\"\"}}",
         mode);
   return buf;
}

/* Flat {device_id, saves:[ClientSaveState,...]} - .srm files only.
 * content_hash is omitted (optional); the server falls back to updated_at. */
static char *romm_sync_build_negotiate_body(romm_sync_state_t *st)
{
   size_t i;
   size_t cap = 512 + st->save_count * 512;
   size_t len = 0;
   char  *buf = (char*)malloc(cap);
   if (!buf)
      return NULL;

   len += snprintf(buf + len, cap - len,
         "{\"device_id\":\"%s\",\"saves\":[", st->device_id);

   for (i = 0; i < st->save_count; i++)
   {
      const char *base = path_basename(st->saves[i]);
      struct stat sb;
      long long   size = 0;
      char        iso[40];

      iso[0] = '\0';
      if (stat(st->saves[i], &sb) == 0)
      {
         size = (long long)sb.st_size;
         romm_unix_to_iso((long long)sb.st_mtime, iso, sizeof(iso));
      }

      len += snprintf(buf + len, cap - len,
            "%s{\"rom_id\":%d,\"file_name\":\"%s\",\"slot\":null,"
            "\"emulator\":\"%s\",\"updated_at\":\"%s\",\"file_size_bytes\":%lld}",
            (i == 0) ? "" : ",", st->rom_id, base ? base : "",
            st->emulator, iso, size);
   }

   strlcpy(buf + len, "]}", cap - len);
   return buf;
}

/* ---------- multipart helpers ---------- */

/* Append one "<field>=@<path>" file part to *body (realloc-grown). */
static bool romm_mp_append_file(char **body, size_t *len,
      const char *field, const char *filename, const char *ctype,
      const char *path)
{
   void   *fb = NULL;
   int64_t fl = 0;
   char    head[1024];
   size_t  hl;
   char   *nb;

   if (!filestream_read_file(path, &fb, &fl) || !fb || fl <= 0)
   {
      if (fb)
         free(fb);
      return false;
   }

   hl = snprintf(head, sizeof(head),
         "--" ROMM_SYNC_BOUNDARY "\r\n"
         "Content-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\n"
         "Content-Type: %s\r\n\r\n",
         field, filename, ctype);

   if (!(nb = (char*)realloc(*body, *len + hl + (size_t)fl + 2)))
   {
      free(fb);
      return false;
   }
   *body = nb;
   memcpy(*body + *len, head, hl);         *len += hl;
   memcpy(*body + *len, fb, (size_t)fl);   *len += (size_t)fl;
   memcpy(*body + *len, "\r\n", 2);        *len += 2;
   free(fb);
   return true;
}

static bool romm_mp_finish(char **body, size_t *len)
{
   const char *tail = "--" ROMM_SYNC_BOUNDARY "--\r\n";
   size_t      tl   = strlen(tail);
   char       *nb   = (char*)realloc(*body, *len + tl);
   if (!nb)
      return false;
   *body = nb;
   memcpy(*body + *len, tail, tl);
   *len += tl;
   return true;
}

/* ---------- transfer completion ---------- */

static void romm_sync_advance(romm_sync_state_t *st)
{
   if (st->phase == ROMM_PHASE_SAVES)
   {
      st->op_index++;
      romm_sync_run_op(st);
   }
   else
   {
      st->state_op_index++;
      romm_sync_run_state_op(st);
   }
}

static void romm_sync_download_cb(retro_task_t *task, void *task_data,
      void *user_data, const char *err)
{
   http_transfer_data_t *data = (http_transfer_data_t*)task_data;
   romm_sync_state_t    *st   = (romm_sync_state_t*)user_data;

   if (!st)
      return;

   if (    !err && data && data->data && data->len
         && data->status >= 200 && data->status < 300
         && !string_is_empty(st->cur_dest))
   {
      char dir[PATH_MAX_LENGTH];
      fill_pathname_basedir(dir, st->cur_dest, sizeof(dir));
      if (!string_is_empty(dir) && !path_is_directory(dir))
         path_mkdir(dir);
      if (filestream_write_file(st->cur_dest, data->data, data->len))
         st->completed++;
      else
         st->failed++;
   }
   else
      st->failed++;

   st->cur_dest[0] = '\0';
   romm_sync_advance(st);
}

static void romm_sync_upload_cb(retro_task_t *task, void *task_data,
      void *user_data, const char *err)
{
   http_transfer_data_t *data = (http_transfer_data_t*)task_data;
   romm_sync_state_t    *st   = (romm_sync_state_t*)user_data;

   if (!st)
      return;
   if (!err && data && data->status >= 200 && data->status < 300)
      st->completed++;
   else
      st->failed++;
   romm_sync_advance(st);
}

/* ---------- saves phase (negotiate ops) ---------- */

static const char *romm_sync_local_for(romm_sync_state_t *st, const char *file)
{
   size_t i;
   if (string_is_empty(file))
      return NULL;
   for (i = 0; i < st->save_count; i++)
   {
      const char *base = path_basename(st->saves[i]);
      if (base && string_is_equal(base, file))
         return st->saves[i];
   }
   return NULL;
}

static void romm_sync_save_download(romm_sync_state_t *st, romm_sync_op_t *op)
{
   const char *savedir = dir_get_ptr(RARCH_DIR_SAVEFILE);
   char url[2048];

   if (    st->direction == ROMM_SYNC_PUSH || op->save_id <= 0
         || string_is_empty(op->file_name)
         || string_is_empty(savedir) || string_is_equal(savedir, "default"))
   {
      st->op_index++;
      romm_sync_run_op(st);
      return;
   }
   fill_pathname_join_special(st->cur_dest, savedir, op->file_name,
         sizeof(st->cur_dest));
   romm_url_save_content(url, sizeof(url), op->save_id);
   task_push_http_transfer_with_headers(url, true, NULL, st->headers,
         romm_sync_download_cb, st);
}

static void romm_sync_save_upload(romm_sync_state_t *st, romm_sync_op_t *op)
{
   const char *local;
   const char *base;
   char   url[2048];
   char  *body     = NULL;
   size_t body_len = 0;

   if (    st->direction == ROMM_SYNC_PULL
         || !(local = romm_sync_local_for(st, op->file_name)))
   {
      st->op_index++;
      romm_sync_run_op(st);
      return;
   }

   base = path_basename(local);
   if (    !romm_mp_append_file(&body, &body_len, "saveFile",
               base ? base : "save", "application/octet-stream", local)
         || !romm_mp_finish(&body, &body_len))
   {
      if (body)
         free(body);
      st->failed++;
      st->op_index++;
      romm_sync_run_op(st);
      return;
   }

   romm_url_saves_upload(url, sizeof(url), st->rom_id, st->emulator,
         st->session_id);
   task_push_http_transfer_with_content(url, "POST", body, body_len,
         "multipart/form-data; boundary=" ROMM_SYNC_BOUNDARY,
         true, false, st->headers, romm_sync_upload_cb, st);
   free(body);
}

static void romm_sync_run_op(romm_sync_state_t *st)
{
   romm_sync_op_t *op;

   if (st->op_index >= st->op_count)
   {
      romm_sync_states_begin(st);
      return;
   }

   op = &st->ops[st->op_index];
   switch (op->type)
   {
      case ROMM_OP_DOWNLOAD:
         romm_sync_save_download(st, op);
         break;
      case ROMM_OP_UPLOAD:
         romm_sync_save_upload(st, op);
         break;
      case ROMM_OP_CONFLICT:
      {
         /* No server-side resolution field: resolve newest-wins locally. */
         const char *local = romm_sync_local_for(st, op->file_name);
         long long   lmt   = 0;
         struct stat sb;
         if (local && stat(local, &sb) == 0)
            lmt = (long long)sb.st_mtime;
         if (lmt > romm_iso_to_unix(op->server_updated_at))
            romm_sync_save_upload(st, op);
         else
            romm_sync_save_download(st, op);
         break;
      }
      case ROMM_OP_NOOP:
      default:
         st->op_index++;
         romm_sync_run_op(st);
         break;
   }
}

/* ---------- states phase (client-side list diff) ---------- */

static void romm_sync_state_upload(romm_sync_state_t *st, romm_state_op_t *o)
{
   char  *body     = NULL;
   size_t body_len = 0;
   char   png[PATH_MAX_LENGTH];

   if (!romm_mp_append_file(&body, &body_len, "stateFile",
            o->file_name, "application/octet-stream", o->local_path))
   {
      if (body)
         free(body);
      st->failed++;
      st->state_op_index++;
      romm_sync_run_state_op(st);
      return;
   }

   /* Attach the savestate screenshot (<state>.png) when present. */
   strlcpy(png, o->local_path, sizeof(png));
   strlcat(png, ".png", sizeof(png));
   if (path_is_valid(png))
   {
      char pngname[300];
      strlcpy(pngname, o->file_name, sizeof(pngname));
      strlcat(pngname, ".png", sizeof(pngname));
      romm_mp_append_file(&body, &body_len, "screenshotFile", pngname,
            "image/png", png);
   }

   if (!romm_mp_finish(&body, &body_len))
   {
      if (body)
         free(body);
      st->failed++;
      st->state_op_index++;
      romm_sync_run_state_op(st);
      return;
   }

   task_push_http_transfer_with_content(o->url, "POST", body, body_len,
         "multipart/form-data; boundary=" ROMM_SYNC_BOUNDARY,
         true, false, st->headers, romm_sync_upload_cb, st);
   free(body);
}

static void romm_sync_run_state_op(romm_sync_state_t *st)
{
   romm_state_op_t *o;

   if (st->state_op_index >= st->state_op_count)
   {
      romm_sync_finalize(st);
      return;
   }

   o = &st->state_ops[st->state_op_index];
   if (o->is_upload)
      romm_sync_state_upload(st, o);
   else
   {
      strlcpy(st->cur_dest, o->local_path, sizeof(st->cur_dest));
      task_push_http_transfer_with_headers(o->url, true, NULL, st->headers,
            romm_sync_download_cb, st);
   }
}

static void romm_sync_states_list_cb(retro_task_t *task, void *task_data,
      void *user_data, const char *err)
{
   http_transfer_data_t *data     = (http_transfer_data_t*)task_data;
   romm_sync_state_t    *st       = (romm_sync_state_t*)user_data;
   romm_state_asset_t   *srv      = NULL;
   size_t                srv_count = 0;
   const char           *statedir = dir_get_ptr(RARCH_DIR_SAVESTATE);
   size_t                i, j;

   if (!st)
      return;
   if (    !err && data && data->data && data->len
         && data->status >= 200 && data->status < 300)
      srv_count = romm_parse_states(data->data, data->len, &srv);

   /* Local -> upload (local-only, or local newer than the server copy). */
   if (st->direction != ROMM_SYNC_PULL)
   {
      for (i = 0; i < st->state_count
            && st->state_op_count < ROMM_SYNC_MAX_FILES * 2; i++)
      {
         const char *base = path_basename(st->states[i]);
         struct stat sb;
         long long   lmt  = 0;
         bool        need = true;
         if (!base)
            continue;
         if (stat(st->states[i], &sb) == 0)
            lmt = (long long)sb.st_mtime;
         for (j = 0; j < srv_count; j++)
         {
            if (string_is_equal(srv[j].file_name, base))
            {
               if (lmt <= romm_iso_to_unix(srv[j].updated_at))
                  need = false;
               break;
            }
         }
         if (need)
         {
            romm_state_op_t *o = &st->state_ops[st->state_op_count++];
            o->is_upload = true;
            strlcpy(o->local_path, st->states[i], sizeof(o->local_path));
            strlcpy(o->file_name, base, sizeof(o->file_name));
            romm_url_states_upload(o->url, sizeof(o->url), st->rom_id,
                  st->emulator);
         }
      }
   }

   /* Server -> download (server-only, or server newer than the local copy). */
   if (    st->direction != ROMM_SYNC_PUSH
         && !string_is_empty(statedir) && !string_is_equal(statedir, "default"))
   {
      for (j = 0; j < srv_count
            && st->state_op_count < ROMM_SYNC_MAX_FILES * 2; j++)
      {
         long long smt  = romm_iso_to_unix(srv[j].updated_at);
         bool      need = true;
         if (    string_is_empty(srv[j].file_name)
               || string_is_empty(srv[j].download_path))
            continue;
         for (i = 0; i < st->state_count; i++)
         {
            const char *base = path_basename(st->states[i]);
            if (base && string_is_equal(base, srv[j].file_name))
            {
               struct stat sb;
               long long   lmt = 0;
               if (stat(st->states[i], &sb) == 0)
                  lmt = (long long)sb.st_mtime;
               if (lmt >= smt)
                  need = false;
               break;
            }
         }
         if (need)
         {
            romm_state_op_t *o = &st->state_ops[st->state_op_count++];
            o->is_upload = false;
            fill_pathname_join_special(o->local_path, statedir,
                  srv[j].file_name, sizeof(o->local_path));
            strlcpy(o->file_name, srv[j].file_name, sizeof(o->file_name));
            romm_url_from_download_path(o->url, sizeof(o->url),
                  srv[j].download_path);
         }
      }
   }

   free(srv);
   st->phase          = ROMM_PHASE_STATES;
   st->state_op_index = 0;
   romm_sync_run_state_op(st);
}

static void romm_sync_states_begin(romm_sync_state_t *st)
{
   char url[2048];
   romm_url_states_by_rom(url, sizeof(url), st->rom_id);
   if (string_is_empty(url))
   {
      romm_sync_finalize(st);
      return;
   }
   task_push_http_transfer_with_headers(url, true, NULL, st->headers,
         romm_sync_states_list_cb, st);
}

/* ---------- finalize (complete session + playtime) ---------- */

static void romm_sync_complete_cb(retro_task_t *task, void *task_data,
      void *user_data, const char *err)
{
   romm_sync_state_t *st = (romm_sync_state_t*)user_data;
   if (!st)
      return;
   if (st->completed > 0)
      romm_sync_msg("RomM save sync complete.");
   romm_sync_done(st);
}

static void romm_sync_finalize(romm_sync_state_t *st)
{
   char   url[2048];
   char  *body;
   char   sessions[256];
   size_t cap = 512;

   sessions[0] = '\0';
   romm_url_sync_complete(url, sizeof(url), st->session_id);
   if (string_is_empty(url) || string_is_empty(st->session_id))
   {
      if (st->completed > 0)
         romm_sync_msg("RomM save sync complete.");
      romm_sync_done(st);
      return;
   }

   /* Report the just-played session's runtime (current-content sync only). */
   if (st->is_current && st->play_usec > 0)
   {
      long long end   = (long long)time(NULL);
      long long durms = st->play_usec / 1000;
      long long start = end - (st->play_usec / 1000000);
      char siso[40], eiso[40];
      if (start >= end)
         start = end - 1;
      romm_unix_to_iso(start, siso, sizeof(siso));
      romm_unix_to_iso(end,   eiso, sizeof(eiso));
      snprintf(sessions, sizeof(sessions),
            "{\"rom_id\":%d,\"start_time\":\"%s\",\"end_time\":\"%s\","
            "\"duration_ms\":%lld}",
            st->rom_id, siso, eiso, durms);
   }

   if (!(body = (char*)malloc(cap)))
   {
      romm_sync_done(st);
      return;
   }
   snprintf(body, cap,
         "{\"operations_completed\":%d,\"operations_failed\":%d,"
         "\"play_sessions\":[%s]}",
         st->completed, st->failed, sessions);

   task_push_http_transfer_with_content(url, "POST", body, strlen(body),
         "application/json", true, false, st->headers,
         romm_sync_complete_cb, st);
   free(body);
}

/* ---------- negotiate ---------- */

static void romm_sync_negotiate_cb(retro_task_t *task, void *task_data,
      void *user_data, const char *err)
{
   http_transfer_data_t *data = (http_transfer_data_t*)task_data;
   romm_sync_state_t    *st   = (romm_sync_state_t*)user_data;

   if (!st)
      return;
   if (    err || !data || !data->data || !data->len
         || data->status < 200 || data->status >= 300)
   {
      RARCH_ERR("[RomM] negotiate failed (status %d).\n",
            data ? data->status : -1);
      /* Saves couldn't be negotiated; still diff states so a pull can proceed.
       * Without a session_id, finalize simply skips /complete. */
      romm_sync_states_begin(st);
      return;
   }

   st->op_count = romm_parse_negotiate(data->data, data->len,
         st->session_id, sizeof(st->session_id), &st->ops);
   st->op_index = 0;
   st->phase    = ROMM_PHASE_SAVES;
   romm_sync_run_op(st);
}

static void romm_sync_negotiate(romm_sync_state_t *st)
{
   char  url[2048];
   char *body = romm_sync_build_negotiate_body(st);

   if (!body)
   {
      romm_sync_done(st);
      return;
   }
   romm_url_negotiate(url, sizeof(url));
   task_push_http_transfer_with_content(url, "POST", body, strlen(body),
         "application/json", true, false, st->headers,
         romm_sync_negotiate_cb, st);
   free(body);
}

/* ---------- device registration ---------- */

static void romm_sync_device_cb(retro_task_t *task, void *task_data,
      void *user_data, const char *err)
{
   http_transfer_data_t *data     = (http_transfer_data_t*)task_data;
   romm_sync_state_t    *st       = (romm_sync_state_t*)user_data;
   settings_t           *settings = config_get_ptr();

   if (!st)
      return;
   if (    err || !data || !data->data || !data->len
         || data->status < 200 || data->status >= 300
         || !romm_parse_device_id(data->data, data->len,
               st->device_id, sizeof(st->device_id)))
   {
      RARCH_ERR("[RomM] device registration failed.\n");
      romm_sync_done(st);
      return;
   }

   if (settings)
   {
      strlcpy(settings->arrays.romm_device_id, st->device_id,
            sizeof(settings->arrays.romm_device_id));
      command_event(CMD_EVENT_MENU_SAVE_CURRENT_CONFIG, NULL);
   }
   romm_sync_negotiate(st);
}

/* ---------- chain entry ---------- */

static void romm_sync_start(romm_sync_state_t *st)
{
   settings_t *settings = config_get_ptr();
   const char *core     = path_get(RARCH_PATH_CORE);

   romm_build_auth_headers(st->headers, sizeof(st->headers));
   if (settings)
      strlcpy(st->device_id, settings->arrays.romm_device_id,
            sizeof(st->device_id));

   /* emulator tag = RetroArch core id (core filename minus extension and the
    * _libretro suffix). RomM's own web player (EmulatorJS) names the Virtual
    * Boy core "beetle_vb" whereas RetroArch's core id is "mednafen_vb", so
    * remap that single case. Only reliable for the current-content sync; a
    * path-based sync may have no core loaded (uploads there are best-effort). */
   if (!string_is_empty(core))
   {
      char *suffix;
      strlcpy(st->emulator, path_basename(core), sizeof(st->emulator));
      path_remove_extension(st->emulator);
      if ((suffix = strstr(st->emulator, "_libretro")))
         *suffix = '\0';
      if (string_is_equal(st->emulator, "mednafen_vb"))
         strlcpy(st->emulator, "beetle_vb", sizeof(st->emulator));
   }

   if (st->is_current)
      romm_sync_gather_current(st);
   else
      romm_sync_gather_path(st, st->content_path);

   if (string_is_empty(st->device_id))
   {
      char *dbody = romm_sync_build_device_body();
      char  url[2048];
      if (!dbody)
      {
         romm_sync_done(st);
         return;
      }
      romm_url_devices(url, sizeof(url));
      task_push_http_transfer_with_content(url, "POST", dbody, strlen(dbody),
            "application/json", true, false, st->headers,
            romm_sync_device_cb, st);
      free(dbody);
      return;
   }

   romm_sync_negotiate(st);
}

/* ---------- public entry points ---------- */

void task_push_romm_sync(int direction)
{
   settings_t        *settings = config_get_ptr();
   const char        *content  = path_get(RARCH_PATH_CONTENT);
   romm_sync_state_t *st;
   int rom_id = 0;

   if (!settings || !romm_is_configured())
      return;
   /* Only sync content that came from RomM (identified via its cache sidecar). */
   if (    string_is_empty(content)
         || !romm_cache_is_cached_content(content, &rom_id))
      return;

   if (!(st = (romm_sync_state_t*)calloc(1, sizeof(*st))))
      return;
   st->rom_id     = rom_id;
   st->direction  = direction;
   st->is_current = true;
   st->play_usec  = (long long)runloop_state_get_ptr()->core_runtime_usec;
   romm_sync_msg("Syncing RomM saves...");
   romm_sync_start(st);
}

/* Sync a specific cached content path (may not be loaded). on_done, when set,
 * ALWAYS fires on completion/failure - used by pull-before-load to resume the
 * deferred launch. */
void task_push_romm_sync_path(const char *content_path, int direction,
      void (*on_done)(void*), void *on_done_data)
{
   settings_t        *settings = config_get_ptr();
   romm_sync_state_t *st;
   int rom_id = 0;

   if (    !settings || !romm_is_configured()
         || string_is_empty(content_path)
         || !romm_cache_is_cached_content(content_path, &rom_id))
   {
      if (on_done)
         on_done(on_done_data);
      return;
   }

   if (!(st = (romm_sync_state_t*)calloc(1, sizeof(*st))))
   {
      if (on_done)
         on_done(on_done_data);
      return;
   }
   st->rom_id       = rom_id;
   st->direction    = direction;
   st->on_done      = on_done;
   st->on_done_data = on_done_data;
   strlcpy(st->content_path, content_path, sizeof(st->content_path));
   romm_sync_start(st);
}

/* Reconcile every cached RomM rom (used at startup, mostly pulling other
 * devices' changes). Fires an independent sync chain per cached rom. */
void task_push_romm_sync_all_cached(int direction)
{
   struct string_list *list;
   size_t i;

   if (!romm_is_configured())
      return;
   if (!(list = romm_cache_enumerate()))
      return;
   for (i = 0; i < list->size; i++)
      task_push_romm_sync_path(list->elems[i].data, direction, NULL, NULL);
   string_list_free(list);
}
