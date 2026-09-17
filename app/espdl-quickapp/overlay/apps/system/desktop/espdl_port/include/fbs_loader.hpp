/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "fbs_model.hpp"
namespace fbs {
/* Only Model(FbsModel*) is used. Files are opened by the bounded, hash-checked
 * NuttX model store. Unused IDF loader constructors are discarded by --gc-sections. */
class FbsLoader {
public:
    FbsLoader(const char *, model_location_type_t) {}
    ~FbsLoader() = default;
    FbsModel *load(const uint8_t *, bool) { return nullptr; }
    FbsModel *load(int, const uint8_t *, bool) { return nullptr; }
    FbsModel *load(const char *, const uint8_t *, bool) { return nullptr; }
    const char *get_model_location_string() { return "verified PSRAM model"; }
};
}
