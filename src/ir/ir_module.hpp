#pragma once

#include "src/ir/basic_block.hpp"
#include "src/ir/ir_function.hpp"

#include <ostream>
#include <string>

class IRModule {
public:
    std::string                               name;
    std::vector<std::unique_ptr<IRFunction>>  functions;

    IRFunction* addFunction(const std::string& name, uint32_t entry) {
        auto fn = std::make_unique<IRFunction>();
        fn->name      = name;
        fn->entryAddr = entry;
        fn->parent    = this;
        auto* ptr = fn.get();
        functions.push_back(std::move(fn));
        return ptr;
    }

    IRFunction* findFunction(uint32_t addr) {
        for (auto& fn : functions)
            if (fn->entryAddr == addr) return fn.get();
        return nullptr;
    }

    void print(std::ostream& os) const {
        os << "module " << name
        << "  (" << functions.size() << " function(s)) {" << '\n';

        for (const auto& fn : functions)
            fn->print(os, 2);
        os << "}\n";
    }

    // Convenience: lets you write   std::cout << module;
    friend std::ostream& operator<<(std::ostream& os, const IRModule& m) {
        m.print(os);
        return os;
    }
};