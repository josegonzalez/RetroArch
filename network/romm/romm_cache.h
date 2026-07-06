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

#ifndef __ROMM_CACHE_H__
#define __ROMM_CACHE_H__

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>

#include "romm_api.h"

RETRO_BEGIN_DECLS

/* Resolved base cache directory (configured romm_cache_dir, else a default
 * under directory_core_assets). Creates it if missing. */
bool romm_cache_dir(char *s, size_t len);

/* Per-platform cache dir <cache>/<platform_slug>. */
void romm_cache_platform_dir(char *s, size_t len, const char *platform_slug);

/* Cache path for a single-file rom: <cache>/<platform_slug>/<fs_name>. */
void romm_cache_rom_path(char *s, size_t len,
      const char *platform_slug, const char *fs_name);

enum romm_cache_mode romm_cache_get_mode(void);
unsigned romm_cache_max_size_mb(void);

/* Sidecar (.romm.<basename>.txt next to the cached content) maps a cached file
 * back to its RomM rom_id/platform_id and records a last-access timestamp. */
bool romm_cache_write_sidecar(const char *content_path, int rom_id,
      int platform_id);
bool romm_cache_read_sidecar(const char *content_path, int *rom_id,
      int *platform_id);
/* True if content_path lives in the RomM cache and has a sidecar. */
bool romm_cache_is_cached_content(const char *content_path, int *rom_id);

/* Refresh a cached file's last-access timestamp (LRU bookkeeping). */
void romm_cache_touch(const char *content_path);
/* Delete a cached file (and its sidecar). For STREAM mode on content unload. */
void romm_cache_remove(const char *content_path);
/* In LRU mode, delete least-recently-used files until the cache plus
 * incoming_bytes fits under the configured cap. No-op in other modes. */
void romm_cache_evict_if_needed(int64_t incoming_bytes);

/* Enumerate every cached RomM content file (those carrying a .romm sidecar).
 * Returns a newly-allocated string_list of content paths (or NULL); the caller
 * frees it with string_list_free(). Used by the startup reconcile. */
struct string_list *romm_cache_enumerate(void);

/* Disk cache of raw API JSON responses, keyed by an arbitrary string (the key
 * is sanitized into a filename under <cache>/responses). Gated by the
 * romm_response_cache_* settings and a wall-clock TTL embedded in each file. */

/* Returns a malloc'd copy of the cached body (NUL-terminated, *out_len set) if
 * caching is enabled and a non-expired cache file exists; NULL otherwise.
 * Caller frees the result. */
char *romm_cache_response_load(const char *key, size_t *out_len);

/* Write a response body to the cache (no-op when caching is disabled). */
void  romm_cache_response_store(const char *key, const char *data, size_t len);

RETRO_END_DECLS

#endif
