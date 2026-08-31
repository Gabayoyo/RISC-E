#pragma once

#include "risc-e/harness/component.hpp"

#include <memory>
#include <string_view>
#include <type_traits>
#include <vector>

// Factory for one component implementation.
using ComponentFactory = std::unique_ptr<Component> (*)();

// Declares a component family (a type). Call once per type, before its
// implementations; registration order defines the --list ordering.
void register_type(std::string_view type);

// For register_component; not meant to be called directly.
void register_component_impl(std::string_view type, std::string_view name,
                             ComponentFactory factory);

// Registers one implementation of a type. Compile-time checked: T must
// extend Component and the type's base class, and the factory must return a
// Component.
template <typename Base, typename T, typename Factory>
inline void register_component(std::string_view type, std::string_view name, Factory factory) {
    static_assert(std::is_base_of_v<Component, T>, "components must extend Component");
    static_assert(std::is_base_of_v<Base, T>,
                  "an implementation must extend the base class of its type");
    static_assert(std::is_convertible_v<Factory, ComponentFactory>,
                  "the factory must return a Component");
    register_component_impl(type, name, factory);
}

// Registered type names, in registration order.
std::vector<std::string_view> component_types();

// All registered component names, optionally filtered by type.
std::vector<std::string_view> component_names();
std::vector<std::string_view> component_names(std::string_view type);

// Builds a component from its CLI name; nullptr for unknown names.
std::unique_ptr<Component> make_component(std::string_view name);
