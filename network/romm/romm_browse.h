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

#ifndef __ROMM_BROWSE_H__
#define __ROMM_BROWSE_H__

#include <stddef.h>
#include <boolean.h>
#include <retro_common_api.h>

#include "romm_api.h"

RETRO_BEGIN_DECLS

/* Loading state for each remote list, used to choose between a
 * "Loading..." placeholder, an error row, or the real rows. */
enum romm_load_status
{
   ROMM_LOAD_IDLE = 0,
   ROMM_LOAD_LOADING,
   ROMM_LOAD_DONE,
   ROMM_LOAD_ERROR
};

/* In-memory cache of whatever the browser is currently showing. This is
 * driver-agnostic so both the shared displaylists and the streamlined views
 * can read from it. Only one platform/collection list and one games list are
 * held at a time (bounded to the current view). */
typedef struct
{
   romm_platform_t      *platforms;
   size_t                platform_count;
   enum romm_load_status platform_status;

   /* Regular, virtual and smart collections merged into one list. */
   romm_collection_t    *collections;
   size_t                collection_count;
   enum romm_load_status collection_status;
   int                   collection_pending; /* kind fetches still in flight */

   /* Games (roms) for the currently opened platform/collection. */
   romm_rom_t           *roms;
   size_t                rom_count;
   size_t                rom_total;
   enum romm_load_status rom_status;
   char                  container_name[256]; /* title for the games view */
   char                  rom_container_key[160]; /* identifies cached container */

   /* Wall-clock (usec) of the last successful fetch of each list, for the
    * optional response cache (reuse a list until older than the TTL). */
   int64_t               platform_fetched_usec;
   int64_t               collection_fetched_usec;
   int64_t               rom_fetched_usec;

   /* Bumped whenever a new fetch starts; stale task callbacks compare against
    * it and discard their results if the user has navigated away. */
   unsigned              request_token;
} romm_state_t;

romm_state_t *romm_state_get_ptr(void);

void romm_state_free_platforms(void);
void romm_state_free_collections(void);
void romm_state_free_roms(void);

/* Append n collections (of any kind) to the merged list. Returns false on OOM. */
bool romm_state_append_collections(const romm_collection_t *arr, size_t n);

/* Bump and return the request token, invalidating any in-flight fetches. */
unsigned romm_state_new_token(void);

/* Current monotonic-ish time in microseconds (for cache timestamps). */
int64_t romm_state_now_usec(void);

/* True when the response cache is enabled and a list fetched at fetched_usec
 * (with the given load status) is still within the configured TTL. */
bool romm_state_cache_fresh(int64_t fetched_usec, enum romm_load_status status);

/* Parse a raw JSON response body into the state and mark it loaded. Used by
 * both the network callbacks and the disk-cache load path. */
void romm_state_apply_platforms(const char *data, size_t len);
void romm_state_apply_collections(const char *data, size_t len);
void romm_state_apply_roms(const char *data, size_t len);

RETRO_END_DECLS

#endif
