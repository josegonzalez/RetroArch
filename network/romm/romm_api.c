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

#include <string/stdstring.h>
#include <compat/strl.h>
#include <formats/rjson.h>

#include "romm_api.h"
#include "../../configuration.h"

/* ============================================================
 * CONFIG / AUTH / URL HELPERS
 * ============================================================ */

bool romm_is_configured(void)
{
   settings_t *settings = config_get_ptr();
   if (!settings || !settings->bools.romm_enable)
      return false;
   return !string_is_empty(settings->arrays.romm_url)
       && !string_is_empty(settings->arrays.romm_api_token);
}

bool romm_build_auth_headers(char *s, size_t len)
{
   settings_t *settings = config_get_ptr();
   if (!s || len == 0)
      return false;
   s[0] = '\0';
   if (!settings || string_is_empty(settings->arrays.romm_api_token))
      return false;
   snprintf(s, len, "Authorization: Bearer %s\r\n",
         settings->arrays.romm_api_token);
   return true;
}

bool romm_base_url(char *s, size_t len)
{
   settings_t *settings = config_get_ptr();
   size_t l;
   if (!s || len == 0)
      return false;
   s[0] = '\0';
   if (!settings || string_is_empty(settings->arrays.romm_url))
      return false;
   strlcpy(s, settings->arrays.romm_url, len);
   /* Strip any trailing slashes */
   l = strlen(s);
   while (l > 0 && s[l - 1] == '/')
      s[--l] = '\0';
   return true;
}

/* Percent-encode src into dst (RFC 3986 unreserved chars pass through). */
static void romm_url_encode(char *dst, size_t dstlen, const char *src)
{
   static const char hex[] = "0123456789ABCDEF";
   size_t o = 0;
   if (!dst || dstlen == 0)
      return;
   dst[0] = '\0';
   if (!src)
      return;
   for (; *src && o + 4 < dstlen; src++)
   {
      unsigned char c = (unsigned char)*src;
      if (     (c >= 'A' && c <= 'Z')
            || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9')
            || c == '-' || c == '_' || c == '.' || c == '~')
         dst[o++] = (char)c;
      else
      {
         dst[o++] = '%';
         dst[o++] = hex[(c >> 4) & 0xF];
         dst[o++] = hex[c & 0xF];
      }
   }
   dst[o] = '\0';
}

static const char *romm_collection_param(enum romm_collection_kind kind)
{
   switch (kind)
   {
      case ROMM_COLLECTION_VIRTUAL: return "virtual_collection_id";
      case ROMM_COLLECTION_SMART:   return "smart_collection_id";
      case ROMM_COLLECTION_REGULAR:
      default:                      return "collection_id";
   }
}

void romm_url_platforms(char *s, size_t len)
{
   char base[NAME_MAX_LENGTH];
   if (!romm_base_url(base, sizeof(base)))
   {
      if (s && len) s[0] = '\0';
      return;
   }
   snprintf(s, len, "%s/api/platforms", base);
}

void romm_url_collections(char *s, size_t len, enum romm_collection_kind kind)
{
   char base[NAME_MAX_LENGTH];
   if (!romm_base_url(base, sizeof(base)))
   {
      if (s && len) s[0] = '\0';
      return;
   }
   switch (kind)
   {
      case ROMM_COLLECTION_VIRTUAL:
         snprintf(s, len, "%s/api/collections/virtual", base);
         break;
      case ROMM_COLLECTION_SMART:
         snprintf(s, len, "%s/api/collections/smart", base);
         break;
      case ROMM_COLLECTION_REGULAR:
      default:
         snprintf(s, len, "%s/api/collections", base);
         break;
   }
}

static void romm_append_roms_query(char *s, size_t len,
      const char *search_term, unsigned limit, unsigned offset)
{
   size_t l = strlen(s);
   snprintf(s + l, len - l, "&limit=%u&offset=%u", limit, offset);
   if (!string_is_empty(search_term))
   {
      char enc[512];
      romm_url_encode(enc, sizeof(enc), search_term);
      l = strlen(s);
      snprintf(s + l, len - l, "&search_term=%s", enc);
   }
}

void romm_url_roms_by_platform(char *s, size_t len, int platform_id,
      const char *search_term, unsigned limit, unsigned offset)
{
   char base[NAME_MAX_LENGTH];
   if (!romm_base_url(base, sizeof(base)))
   {
      if (s && len) s[0] = '\0';
      return;
   }
   snprintf(s, len, "%s/api/roms?platform_ids=%d", base, platform_id);
   romm_append_roms_query(s, len, search_term, limit, offset);
}

void romm_url_roms_by_collection(char *s, size_t len,
      enum romm_collection_kind kind, const char *collection_id,
      const char *search_term, unsigned limit, unsigned offset)
{
   char base[NAME_MAX_LENGTH];
   char enc_id[256];
   if (!romm_base_url(base, sizeof(base)))
   {
      if (s && len) s[0] = '\0';
      return;
   }
   romm_url_encode(enc_id, sizeof(enc_id), collection_id ? collection_id : "");
   snprintf(s, len, "%s/api/roms?%s=%s", base,
         romm_collection_param(kind), enc_id);
   romm_append_roms_query(s, len, search_term, limit, offset);
}

void romm_url_rom_content(char *s, size_t len, int rom_id, const char *file_name)
{
   char base[NAME_MAX_LENGTH];
   char enc[512];
   if (!romm_base_url(base, sizeof(base)))
   {
      if (s && len) s[0] = '\0';
      return;
   }
   romm_url_encode(enc, sizeof(enc), file_name ? file_name : "");
   snprintf(s, len, "%s/api/roms/%d/content/%s", base, rom_id, enc);
}

void romm_url_saves_by_platform(char *s, size_t len, int platform_id)
{
   char base[NAME_MAX_LENGTH];
   if (!romm_base_url(base, sizeof(base)))
   {
      if (s && len) s[0] = '\0';
      return;
   }
   snprintf(s, len, "%s/api/saves?platform_id=%d", base, platform_id);
}

void romm_url_states_by_platform(char *s, size_t len, int platform_id)
{
   char base[NAME_MAX_LENGTH];
   if (!romm_base_url(base, sizeof(base)))
   {
      if (s && len) s[0] = '\0';
      return;
   }
   snprintf(s, len, "%s/api/states?platform_id=%d", base, platform_id);
}

/* ============================================================
 * JSON PARSERS (rjson handler-based, depth-guarded)
 * ============================================================ */

static bool romm_grow(void **arr, size_t *cap, size_t count, size_t elemsize)
{
   void  *n;
   size_t ncap;
   if (count < *cap)
      return true;
   ncap = *cap ? (*cap * 2) : 16;
   if (!(n = realloc(*arr, ncap * elemsize)))
      return false;
   *arr = n;
   *cap = ncap;
   return true;
}

/* ---- platforms: top-level array of objects (rows at depth 2) ---- */

typedef struct
{
   romm_platform_t *arr;
   size_t           count;
   size_t           cap;
   romm_platform_t  cur;
   char             key[64];
   int              depth;
} plat_ctx_t;

static bool plat_start_array(void *ctx) { ((plat_ctx_t*)ctx)->depth++; return true; }
static bool plat_end_array(void *ctx)   { ((plat_ctx_t*)ctx)->depth--; return true; }

static bool plat_start_object(void *ctx)
{
   plat_ctx_t *c = (plat_ctx_t*)ctx;
   c->depth++;
   if (c->depth == 2)
   {
      memset(&c->cur, 0, sizeof(c->cur));
      c->key[0] = '\0';
   }
   return true;
}

static bool plat_end_object(void *ctx)
{
   plat_ctx_t *c = (plat_ctx_t*)ctx;
   if (c->depth == 2)
   {
      if (romm_grow((void**)&c->arr, &c->cap, c->count, sizeof(romm_platform_t)))
         c->arr[c->count++] = c->cur;
   }
   c->depth--;
   return true;
}

static bool plat_member(void *ctx, const char *s, size_t len)
{
   plat_ctx_t *c = (plat_ctx_t*)ctx;
   if (c->depth == 2)
      strlcpy(c->key, s, sizeof(c->key));
   return true;
}

static bool plat_string(void *ctx, const char *s, size_t len)
{
   plat_ctx_t *c = (plat_ctx_t*)ctx;
   if (c->depth != 2)
      return true;
   if (string_is_equal(c->key, "slug"))
      strlcpy(c->cur.slug, s, sizeof(c->cur.slug));
   else if (string_is_equal(c->key, "name"))
      strlcpy(c->cur.name, s, sizeof(c->cur.name));
   return true;
}

static bool plat_number(void *ctx, const char *s, size_t len)
{
   plat_ctx_t *c = (plat_ctx_t*)ctx;
   if (c->depth != 2)
      return true;
   if (string_is_equal(c->key, "id"))
      c->cur.id = (int)strtol(s, NULL, 10);
   else if (string_is_equal(c->key, "rom_count"))
      c->cur.rom_count = (int)strtol(s, NULL, 10);
   return true;
}

size_t romm_parse_platforms(const char *data, size_t len, romm_platform_t **out)
{
   plat_ctx_t ctx;
   rjson_t   *json;
   if (out)
      *out = NULL;
   if (!data || len == 0 || !out)
      return 0;
   memset(&ctx, 0, sizeof(ctx));
   if (!(json = rjson_open_buffer(data, len)))
      return 0;
   rjson_parse(json, &ctx,
         plat_member, plat_string, plat_number,
         plat_start_object, plat_end_object,
         plat_start_array, plat_end_array,
         NULL, NULL);
   rjson_free(json);
   *out = ctx.arr;
   return ctx.count;
}

/* ---- collections: top-level array of objects (rows at depth 2) ---- */

typedef struct
{
   romm_collection_t        *arr;
   size_t                    count;
   size_t                    cap;
   romm_collection_t         cur;
   enum romm_collection_kind kind;
   char                      key[64];
   int                       depth;
} coll_ctx_t;

static bool coll_start_array(void *ctx) { ((coll_ctx_t*)ctx)->depth++; return true; }
static bool coll_end_array(void *ctx)   { ((coll_ctx_t*)ctx)->depth--; return true; }

static bool coll_start_object(void *ctx)
{
   coll_ctx_t *c = (coll_ctx_t*)ctx;
   c->depth++;
   if (c->depth == 2)
   {
      memset(&c->cur, 0, sizeof(c->cur));
      c->cur.kind = c->kind;
      c->key[0]   = '\0';
   }
   return true;
}

static bool coll_end_object(void *ctx)
{
   coll_ctx_t *c = (coll_ctx_t*)ctx;
   if (c->depth == 2)
   {
      if (romm_grow((void**)&c->arr, &c->cap, c->count, sizeof(romm_collection_t)))
         c->arr[c->count++] = c->cur;
   }
   c->depth--;
   return true;
}

static bool coll_member(void *ctx, const char *s, size_t len)
{
   coll_ctx_t *c = (coll_ctx_t*)ctx;
   if (c->depth == 2)
      strlcpy(c->key, s, sizeof(c->key));
   return true;
}

static bool coll_string(void *ctx, const char *s, size_t len)
{
   coll_ctx_t *c = (coll_ctx_t*)ctx;
   if (c->depth != 2)
      return true;
   if (string_is_equal(c->key, "id"))
      strlcpy(c->cur.id, s, sizeof(c->cur.id));
   else if (string_is_equal(c->key, "name"))
      strlcpy(c->cur.name, s, sizeof(c->cur.name));
   return true;
}

static bool coll_number(void *ctx, const char *s, size_t len)
{
   coll_ctx_t *c = (coll_ctx_t*)ctx;
   if (c->depth != 2)
      return true;
   if (string_is_equal(c->key, "id"))
      strlcpy(c->cur.id, s, sizeof(c->cur.id));
   else if (string_is_equal(c->key, "rom_count"))
      c->cur.rom_count = (int)strtol(s, NULL, 10);
   return true;
}

size_t romm_parse_collections(const char *data, size_t len,
      enum romm_collection_kind kind, romm_collection_t **out)
{
   coll_ctx_t ctx;
   rjson_t   *json;
   if (out)
      *out = NULL;
   if (!data || len == 0 || !out)
      return 0;
   memset(&ctx, 0, sizeof(ctx));
   ctx.kind = kind;
   if (!(json = rjson_open_buffer(data, len)))
      return 0;
   rjson_parse(json, &ctx,
         coll_member, coll_string, coll_number,
         coll_start_object, coll_end_object,
         coll_start_array, coll_end_array,
         NULL, NULL);
   rjson_free(json);
   *out = ctx.arr;
   return ctx.count;
}

/* ---- roms: page object {items:[...], total:N}; rows at depth 3 ---- */

typedef struct
{
   romm_rom_t *arr;
   size_t      count;
   size_t      cap;
   size_t      total;
   romm_rom_t  cur;
   char        key[64];
   int         depth;
} rom_ctx_t;

static bool rom_start_array(void *ctx) { ((rom_ctx_t*)ctx)->depth++; return true; }
static bool rom_end_array(void *ctx)   { ((rom_ctx_t*)ctx)->depth--; return true; }

static bool rom_start_object(void *ctx)
{
   rom_ctx_t *c = (rom_ctx_t*)ctx;
   c->depth++;
   if (c->depth == 3)
   {
      memset(&c->cur, 0, sizeof(c->cur));
      c->key[0] = '\0';
   }
   return true;
}

static bool rom_end_object(void *ctx)
{
   rom_ctx_t *c = (rom_ctx_t*)ctx;
   if (c->depth == 3)
   {
      if (romm_grow((void**)&c->arr, &c->cap, c->count, sizeof(romm_rom_t)))
         c->arr[c->count++] = c->cur;
   }
   c->depth--;
   return true;
}

static bool rom_member(void *ctx, const char *s, size_t len)
{
   rom_ctx_t *c = (rom_ctx_t*)ctx;
   if (c->depth == 1 || c->depth == 3)
      strlcpy(c->key, s, sizeof(c->key));
   return true;
}

static bool rom_string(void *ctx, const char *s, size_t len)
{
   rom_ctx_t *c = (rom_ctx_t*)ctx;
   if (c->depth != 3)
      return true;
   if (string_is_equal(c->key, "name"))
      strlcpy(c->cur.name, s, sizeof(c->cur.name));
   else if (string_is_equal(c->key, "fs_name"))
      strlcpy(c->cur.fs_name, s, sizeof(c->cur.fs_name));
   else if (string_is_equal(c->key, "platform_slug"))
      strlcpy(c->cur.platform_slug, s, sizeof(c->cur.platform_slug));
   else if (string_is_equal(c->key, "url_cover"))
      strlcpy(c->cur.url_cover, s, sizeof(c->cur.url_cover));
   return true;
}

static bool rom_number(void *ctx, const char *s, size_t len)
{
   rom_ctx_t *c = (rom_ctx_t*)ctx;
   if (c->depth == 1)
   {
      if (string_is_equal(c->key, "total"))
         c->total = (size_t)strtoull(s, NULL, 10);
      return true;
   }
   if (c->depth != 3)
      return true;
   if (string_is_equal(c->key, "id"))
      c->cur.id = (int)strtol(s, NULL, 10);
   else if (string_is_equal(c->key, "platform_id"))
      c->cur.platform_id = (int)strtol(s, NULL, 10);
   else if (string_is_equal(c->key, "fs_size_bytes"))
      c->cur.fs_size_bytes = (int64_t)strtoll(s, NULL, 10);
   return true;
}

static bool rom_boolean(void *ctx, bool value)
{
   rom_ctx_t *c = (rom_ctx_t*)ctx;
   if (c->depth == 3 && string_is_equal(c->key, "has_multiple_files"))
      c->cur.has_multiple_files = value;
   return true;
}

size_t romm_parse_roms(const char *data, size_t len,
      romm_rom_t **out, size_t *total_out)
{
   rom_ctx_t ctx;
   rjson_t  *json;
   if (out)
      *out = NULL;
   if (total_out)
      *total_out = 0;
   if (!data || len == 0 || !out)
      return 0;
   memset(&ctx, 0, sizeof(ctx));
   if (!(json = rjson_open_buffer(data, len)))
      return 0;
   rjson_parse(json, &ctx,
         rom_member, rom_string, rom_number,
         rom_start_object, rom_end_object,
         rom_start_array, rom_end_array,
         rom_boolean, NULL);
   rjson_free(json);
   *out = ctx.arr;
   if (total_out)
      *total_out = ctx.total ? ctx.total : ctx.count;
   return ctx.count;
}
