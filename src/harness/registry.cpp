#include "risc-e/harness/registry.hpp"

#include "risc-e/cpu/branch_predictor.hpp"
#include "risc-e/cpu/pipeline.hpp"
#include "risc-e/cpu/profile.hpp"
#include "risc-e/cpu/predictors/always_not_taken.hpp"
#include "risc-e/cpu/predictors/gshare.hpp"
#include "risc-e/cpu/predictors/ras.hpp"
#include "risc-e/cpu/predictors/tournament.hpp"
#include "risc-e/cpu/predictors/two_bit_saturating.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace {

struct ComponentEntry {
    std::string_view name;
    std::string_view type;
    ComponentFactory factory;
};

std::vector<std::string_view>& type_entries() {
    static std::vector<std::string_view> entries;
    return entries;
}

std::vector<ComponentEntry>& component_entries() {
    static std::vector<ComponentEntry> entries;
    return entries;
}

bool registered = false;

// Declares every type and registers every built-in component. Idempotent;
// called on first use of the registry.
void register_all() {
    if (registered) return;
    registered = true;

    register_type("predictor");
    register_type("pipeline");
    register_type("profile");

    register_component<BranchPredictor, TwoBitSaturatingPredictor>(
        "predictor", TwoBitSaturatingPredictor::kName,
        []() -> std::unique_ptr<Component> { return std::make_unique<TwoBitSaturatingPredictor>(); });
    register_component<BranchPredictor, AlwaysNotTakenPredictor>(
        "predictor", AlwaysNotTakenPredictor::kName,
        []() -> std::unique_ptr<Component> { return std::make_unique<AlwaysNotTakenPredictor>(); });
    register_component<BranchPredictor, GsharePredictor>(
        "predictor", GsharePredictor::kName,
        []() -> std::unique_ptr<Component> { return std::make_unique<GsharePredictor>(); });
    register_component<BranchPredictor, TournamentPredictor>(
        "predictor", TournamentPredictor::kName,
        []() -> std::unique_ptr<Component> { return std::make_unique<TournamentPredictor>(); });
    register_component<BranchPredictor, RasPredictor>(
        "predictor", RasPredictor::kName,
        []() -> std::unique_ptr<Component> { return std::make_unique<RasPredictor>(); });
    register_component<PipelineModel, PipelineModel>(
        "pipeline", PipelineModel::kName,
        []() -> std::unique_ptr<Component> { return std::make_unique<PipelineModel>(); });
    register_component<ProfileComponent, ProfileComponent>(
        "profile", ProfileComponent::kName,
        []() -> std::unique_ptr<Component> { return std::make_unique<ProfileComponent>(); });
}

} // namespace

void register_type(std::string_view type) {
    type_entries().push_back(type);
}

void register_component_impl(std::string_view type, std::string_view name,
                             ComponentFactory factory) {
    component_entries().push_back({name, type, factory});
}

std::vector<std::string_view> component_types() {
    register_all();
    return type_entries();
}

std::vector<std::string_view> component_names(std::string_view type) {
    register_all();
    std::vector<std::string_view> names;
    for (const ComponentEntry& e : component_entries()) {
        if (type.empty() || e.type == type) names.push_back(e.name);
    }
    return names;
}

std::vector<std::string_view> component_names() {
    return component_names("");
}

std::unique_ptr<Component> make_component(std::string_view name) {
    register_all();
    for (const ComponentEntry& e : component_entries()) {
        if (e.name == name) return e.factory();
    }
    return nullptr;
}
