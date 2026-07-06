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

/* ============================================================
 * SAVE / STATE SYNC (negotiate engine)
 * ============================================================ */

enum romm_sync_direction
{
   ROMM_SYNC_BIDIRECTIONAL = 0,
   ROMM_SYNC_PUSH,
   ROMM_SYNC_PULL
};

enum romm_sync_op_type
{
   ROMM_OP_NOOP = 0,
   ROMM_OP_UPLOAD,
   ROMM_OP_DOWNLOAD,
   ROMM_OP_CONFLICT
};

/* One operation returned by /api/sync/negotiate (SyncOperationSchema). The
 * server sends no URLs - the client builds them from save_id / rom_id. */
typedef struct
{
   enum romm_sync_op_type type;        /* from "action" */
   int  save_id;                       /* download: GET /api/saves/{save_id}/content */
   int  rom_id;
   char file_name[256];
   char slot[64];
   char emulator[128];
   char server_updated_at[64];         /* ISO 8601; resolves conflicts newest-wins */
} romm_sync_op_t;

/* One server-side save state (StateSchema / BaseAsset) from GET /api/states. */
typedef struct
{
   int     id;
   int64_t file_size_bytes;
   char    file_name[256];
   char    download_path[2048];        /* GET <base><download_path> to fetch */
   char    updated_at[64];             /* ISO 8601 */
} romm_state_asset_t;

/* Sync endpoint URL builders. */
void romm_url_devices(char *s, size_t len);
void romm_url_negotiate(char *s, size_t len);
void romm_url_sync_complete(char *s, size_t len, const char *session_id);
/* GET /api/saves/{save_id}/content (download a save the negotiate op points to). */
void romm_url_save_content(char *s, size_t len, int save_id);
/* POST /api/saves upload URL with query params (rom_id/emulator/overwrite/session_id). */
void romm_url_saves_upload(char *s, size_t len, int rom_id,
      const char *emulator, const char *session_id);
/* GET /api/states?rom_id= (list a rom's server states for the states diff). */
void romm_url_states_by_rom(char *s, size_t len, int rom_id);
/* POST /api/states upload URL with query params (rom_id/emulator). */
void romm_url_states_upload(char *s, size_t len, int rom_id, const char *emulator);
/* Join the configured base URL with a server-relative download_path. */
void romm_url_from_download_path(char *s, size_t len, const char *download_path);

/* Parse the device id (int or string) from a POST /api/devices response. */
bool romm_parse_device_id(const char *data, size_t len, char *out, size_t out_len);

/* Parse a negotiate response into session id + an operations array (caller
 * frees *ops_out with free()). Returns the operation count. */
size_t romm_parse_negotiate(const char *data, size_t len,
      char *session_out, size_t session_len, romm_sync_op_t **ops_out);

/* Parse a GET /api/states array into server-state assets (caller frees *out). */
size_t romm_parse_states(const char *data, size_t len, romm_state_asset_t **out);

RETRO_END_DECLS

#endif
