#pragma once
#include "exact_program_pool.h"
#include <thirdparty/re-spirv/narrow-optimizer-inputs.h>
#include <cstring>
#include <optional>
#include <utility>
#define XXH_INLINE_ALL
#include <thirdparty/zstd/common/xxhash.h>
#undef XXH_INLINE_ALL

struct RespirvSharedProgram {
    respv::Shader shader;
    respv::NarrowOptimizerInputs optimizer_inputs;
    // Own the existing encoded stage buffer. In the driver this owner is a
    // const Godot Vector copy: COW shares its bytes with the shader container.
    // Do not retain another full decoded source or recompress it for the index.
    std::shared_ptr<const void> source_owner;
    const uint8_t *encoded_source=nullptr;
    size_t encoded_size=0;
    uint32_t compression_flags=0;
    int64_t decompressed_size=0;
    RespirvSharedProgram(const void *decoded,size_t decoded_size,std::shared_ptr<const void> owner,
            const uint8_t *encoded,size_t size,uint32_t flags,int64_t unpacked_size):
            shader(decoded,decoded_size,true),optimizer_inputs(shader),source_owner(std::move(owner)),encoded_source(encoded),
            encoded_size(size),compression_flags(flags),decompressed_size(unpacked_size) {}
    bool matches(const void *data,size_t size,uint32_t flags,int64_t unpacked_size) const {
        return compression_flags==flags && decompressed_size==unpacked_size && encoded_size==size &&
            (size==0 || std::memcmp(encoded_source,data,size)==0);
    }
    static std::string bucket(const void *data,size_t size) {
        uint64_t hash=XXH64(data,size,0);
        return std::string(reinterpret_cast<const char*>(&hash),sizeof(hash));
    }
};
