/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/

#include "vita.hpp"

#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/videodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <memory>

#include "ConfigManager.hpp"
#include "debug.hpp"
#include "gamestream/sps.h" // clase SpsContext + flags

// Refactored modules
#include "modules/vita_globals.hpp"
// The .cpp files are compiled separately via CMake

static_assert(sizeof(DECODER_RENDERER_CALLBACKS) == (sizeof(void*) * 5 + sizeof(int)), "Tamaño inesperado de DECODER_RENDERER_CALLBACKS (posible padding diferente)");
