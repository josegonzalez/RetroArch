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

#ifndef __ROMM_API_H__
#define __ROMM_API_H__

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>

RETRO_BEGIN_DECLS

/* RomM has three kinds of collections, each with its own list endpoint and its
 * own rom-filter query parameter (collection_id / virtual_collection_id /
 * smart_collection_id). Regular and smart ids are ints; virtual ids are strings,
 * so all collection ids are stored as strings here. */
enum romm_collection_kind
{
   ROMM_COLLECTION_REGULAR = 0,
   ROMM_COLLECTION_VIRTUAL,
   ROMM_COLLECTION_SMART
};

/* Matches settings->uints.romm_cache_mode. */
enum romm_cache_mode
{
   ROMM_CACHE_KEEP = 0,
   ROMM_CACHE_LRU,
   ROMM_CACHE_STREAM
};

typedef struct
{
   int  id;
   int  rom_count;
   char slug[128];
   char name[256];
} romm_platform_t;

typedef struct
{
   enum romm_collection_kind kind;
   int  rom_count;
   char id[128];      /* string: virtual ids are non-numeric */
   char name[256];
} romm_collection_t;

typedef struct
{
   int     id;
   int     platform_id;
   int64_t fs_size_bytes;
   bool    has_multiple_files;
   char    name[256];
   char    fs_name[256];
   char    platform_slug[128];
   char    url_cover[512];
} romm_rom_t;

/* True when RomM is enabled and a server URL + token are configured. */
bool romm_is_configured(void);

/* Writes "Authorization: Bearer <token>\r\n" into s. Returns false (and empty s)
 * when no token is set. Header string is suitable for task_push_http_*_with_headers. */
bool romm_build_auth_headers(char *s, size_t len);

/* Configured server base URL with any trailing slash stripped. */
bool romm_base_url(char *s, size_t len);

/* URL builders (write a full absolute URL into s). */
void romm_url_platforms(char *s, size_t len);
void romm_url_collections(char *s, size_t len, enum romm_collection_kind kind);
void romm_url_roms_by_platform(char *s, size_t len, int platform_id,
      const char *search_term, unsigned limit, unsigned offset);
void romm_url_roms_by_collection(char *s, size_t len,
      enum romm_collection_kind kind, const char *collection_id,
      const char *search_term, unsigned limit, unsigned offset);
void romm_url_rom_content(char *s, size_t len, int rom_id, const char *file_name);
void romm_url_saves_by_platform(char *s, size_t len, int platform_id);
void romm_url_states_by_platform(char *s, size_t len, int platform_id);

/* Response parsers. On success return the element count and set *out to a
 * malloc'd array (caller frees with free()); on failure return 0 and *out=NULL.
 * romm_parse_roms additionally reports the server-side total via *total_out. */
size_t romm_parse_platforms(const char *data, size_t len, romm_platform_t **out);
size_t romm_parse_collections(const char *data, size_t len,
      enum romm_collection_kind kind, romm_collection_t **out);
size_t romm_parse_roms(const char *data, size_t len,
      romm_rom_t **out, size_t *total_out);

RETRO_END_DECLS

#endif
