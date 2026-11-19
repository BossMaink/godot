#pragma once

#include "re-spirv.h"

#include <memory>

namespace respv {
    // Built before a parsed program is published. Only the optimizer's thread-local
    // copies are mutable. Keep the existing Shader representation for wide inputs.
    class NarrowOptimizerInputs {
        struct NarrowListNode {
            uint16_t instructionIndex;
            uint16_t nextListIndex;
        };

        std::unique_ptr<uint8_t[]> inDegrees;
        std::unique_ptr<uint16_t[]> outDegrees;
        std::unique_ptr<NarrowListNode[]> listNodes;
        size_t listNodeCount = 0;

    public:
        explicit NarrowOptimizerInputs(Shader &shader);

        void copy_to(const Shader &shader, std::vector<uint32_t> &in, std::vector<uint32_t> &out,
                     std::vector<ListNode> &nodes) const;
        size_t payload_capacity_bytes(const Shader &shader) const;
    };

    bool optimize_narrow(const Shader &shader, const NarrowOptimizerInputs &inputs,
                         const SpecConstant *constants, uint32_t count,
                         std::vector<uint8_t> &data, Options options = Options());
}
