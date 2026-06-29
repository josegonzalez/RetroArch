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
#include <string.h>

#include <features/features_cpu.h>

#include "romm_browse.h"
#include "../../configuration.h"

static romm_state_t romm_state;

romm_state_t *romm_state_get_ptr(void)
{
   return &romm_state;
}

void romm_state_free_platforms(void)
{
   free(romm_state.platforms);
   romm_state.platforms      = NULL;
   romm_state.platform_count = 0;
}

void romm_state_free_collections(void)
{
   free(romm_state.collections);
   romm_state.collections      = NULL;
   romm_state.collection_count = 0;
}

void romm_state_free_roms(void)
{
   free(romm_state.roms);
   romm_state.roms      = NULL;
   romm_state.rom_count = 0;
   romm_state.rom_total = 0;
}

bool romm_state_append_collections(const romm_collection_t *arr, size_t n)
{
   romm_collection_t *grown;
   if (!arr || n == 0)
      return true;
   grown = (romm_collection_t*)realloc(romm_state.collections,
         (romm_state.collection_count + n) * sizeof(romm_collection_t));
   if (!grown)
      return false;
   memcpy(grown + romm_state.collection_count, arr,
         n * sizeof(romm_collection_t));
   romm_state.collections       = grown;
   romm_state.collection_count += n;
   return true;
}

unsigned romm_state_new_token(void)
{
   return ++romm_state.request_token;
}

int64_t romm_state_now_usec(void)
{
   return cpu_features_get_time_usec();
}

bool romm_state_cache_fresh(int64_t fetched_usec, enum romm_load_status status)
{
   settings_t *settings = config_get_ptr();
   unsigned    minutes;
   int64_t     ttl_usec;

   if (status != ROMM_LOAD_DONE)
      return false;
   if (!settings || !settings->bools.romm_response_cache_enable)
      return false;
   if ((minutes = settings->uints.romm_response_cache_minutes) == 0)
      return false;
   ttl_usec = (int64_t)minutes * 60 * 1000000;
   return (cpu_features_get_time_usec() - fetched_usec) < ttl_usec;
}

void romm_state_apply_platforms(const char *data, size_t len)
{
   romm_state_free_platforms();
   romm_state.platform_count        = romm_parse_platforms(data, len,
         &romm_state.platforms);
   romm_state.platform_status       = ROMM_LOAD_DONE;
   romm_state.platform_fetched_usec = romm_state_now_usec();
}

void romm_state_apply_collections(const char *data, size_t len)
{
   romm_collection_t *arr = NULL;
   size_t             n   = romm_parse_collections(data, len,
         ROMM_COLLECTION_REGULAR, &arr);
   romm_state_free_collections();
   romm_state_append_collections(arr, n);
   free(arr);
   romm_state.collection_status       = ROMM_LOAD_DONE;
   romm_state.collection_fetched_usec = romm_state_now_usec();
}

void romm_state_apply_roms(const char *data, size_t len)
{
   romm_state_free_roms();
   romm_state.rom_count        = romm_parse_roms(data, len,
         &romm_state.roms, &romm_state.rom_total);
   romm_state.rom_status       = ROMM_LOAD_DONE;
   romm_state.rom_fetched_usec = romm_state_now_usec();
}
