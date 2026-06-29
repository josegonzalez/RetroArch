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

RETRO_END_DECLS

#endif
