# RomM integration

RetroArch can act as a client for a [RomM](https://romm.app) server: browse your remote library by
platform or collection, download a game to play it, and sync your saves and save states back to the
server. Settings live under **Settings → Saving → Cloud Sync → RomM**.

## Setup

1. **RomM Integration** - turn this on.
2. **RomM Server URL** - your server's base URL, e.g. `https://romm.example.com` (no trailing `/api`).
3. **RomM API Token** - a client API token (`rmm_…`) pasted into the field.
4. **Test Connection** - confirms the URL and token work; it reports how many platforms were found.

### Getting a token

In the RomM web UI go to **Administration → Client API Tokens** and create a token. Give it a name and
select the permissions listed below. A token looks like `rmm_` followed by 64 hex characters and is shown
only once, so copy it immediately, then paste it into the **RomM API Token** field in RetroArch.

## Required API token scopes

RomM uses fine-grained, dot-notation scopes. The integration needs the following (and **only** these -
it never edits your library, so `roms.write` / `platforms.write` are not required):

| Scope | Why it's needed |
|---|---|
| `platforms.read` | List platforms when browsing. |
| `collections.read` | List collections (regular, virtual, and smart). |
| `roms.read` | List games and download game files. |
| `assets.read` | Read your save files and save states. |
| `assets.write` | Upload save files and save states. |
| `devices.read` | Identify this device during save sync. |
| `devices.write` | Register this device and run sync sessions. |

Shortcut: in RomM's token **Permissions** picker, start from the read-only default (which already grants
the read scopes above) and then additionally enable **`assets.write`** and **`devices.write`**. RetroArch
has no permission or role concept of its own - it only stores the token and sends it as a bearer header;
all scope selection happens on the RomM server when the token is created.

If you only want to browse and play (no save sync yet), the three `*.read` scopes
`platforms.read`, `collections.read`, and `roms.read` are enough.

## Cache settings

Downloaded games are stored in a local cache so they play instantly next time.

- **RomM Cache Directory** - where downloads are kept (defaults to a `romm` folder under the core-assets
  directory).
- **Cache Mode**:
  - **Keep** - downloads stay until you remove them.
  - **Limit Size (LRU)** - automatically deletes the least-recently-played downloads once the cache
    exceeds the size limit below.
  - **Stream** - deletes a game's download after you stop playing it.
- **Cache Size Limit (MB)** - the cap used by **Limit Size (LRU)** mode.
- **Game List Page Size** - how many games RetroArch fetches per request when browsing a large platform
  (default 1000; RomM allows up to 10000).

## References

- Client API tokens: RomM docs,
  [API Authentication](https://docs.romm.app/4.9.0/developers/api-authentication/).
- Device registration & save-sync protocol:
  [docs/developers/device-sync-protocol.md](https://github.com/rommapp/docs/blob/main/docs/developers/device-sync-protocol.md).
- API token scopes: defined in the RomM backend
  ([`backend/handler/auth/constants.py`](https://github.com/rommapp/romm/blob/master/backend/handler/auth/constants.py)).
- Endpoints/schemas above were taken from the live `openapi.json` plus the backend routers
  (`endpoints/collections.py`, `saves.py`, `states.py`) and response schemas
  (`endpoints/responses/rom.py`, `assets.py`, `platform.py`, `collection.py`).
