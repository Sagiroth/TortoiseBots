// Forward-ported from mod-playerbots TellMasterAction.cpp - modern donor
// Source: mod-playerbots@5397110, Shyalya@1f9497e Tortoise 1.18.1 baseline
/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "TellMasterAction.h"

// The Tortoise-compatible implementations are intentionally inline in the
// header so the requester/master target selection stays with the action's
// current host-facing API.  This donor translation unit remains as a
// provenance marker but contributes no second definition.
