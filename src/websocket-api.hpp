/*
Replay Cue for OBS Studio
Copyright (C) 2026 Replay Cue contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include <obs.h>

namespace rc {

class ReplayController;

/* Registers the "ReplayCue" obs-websocket vendor so Stream Deck, Bitfocus
 * Companion and other controllers can drive replays. Call from
 * obs_module_post_load(), once obs-websocket has loaded. */
void registerWebsocketVendor(ReplayController *controller);
void unregisterWebsocketVendor();

/* Broadcasts a vendor event to websocket clients (no-op without obs-websocket). */
void emitVendorEvent(const char *name, obs_data_t *data);

} // namespace rc
