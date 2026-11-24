// util/drop_kits.h
//
// Built-in catalogue of third-party drop kits DropPoint's config can be
// validated against (docs/DROP-KITS.md is the prose source of truth — the
// ids below must appear there as `id`, enforced by a unit test). Every
// listed kit is a self-contained DJI payload with its own drop button, so
// "support" means: which aircraft it fits, how much it weighs and carries,
// and which release.backend values make sense with it.
//
// DJI-free by design — pure data, unit-tested on any OS.
#pragma once

#include <string>
#include <vector>

#include "util/config.h"

namespace droppoint::util {

struct DropKit {
    std::string id;                          // `release.kit` value
    std::string maker;
    std::string model;                       // the model to buy
    std::vector<std::string> aircraft_models;
    std::string interface;                   // what it physically connects to
    double weight_kg = 0.0;
    int hooks = 0;
    double max_load_per_hook_kg = 0.0;       // 0 = not specified by the maker
    std::string trigger;                     // how the operator fires it
    std::vector<config::ReleaseBackend> backends;
    std::string note;
};

// The full catalogue, in a stable order (M4-series kits first, then M400,
// then the build-your-own option).
const std::vector<DropKit>& DropKitCatalogue();

// nullptr when `id` is not in the catalogue.
const DropKit* FindDropKit(const std::string& id);

// Catalogue entries whose aircraft_models contain `model`, in catalogue
// order.
std::vector<const DropKit*> KitsForModel(const std::string& model);

// A plain-text table of the whole catalogue for `--list-kits`. When
// `model_filter` names a known aircraft model, a leading column marks the
// kits that fit it with '*'.
std::string FormatKitTable(const std::string& model_filter = "");

}  // namespace droppoint::util
