#pragma once

#include <memory>
#include <iostream>
#include <unordered_map>
#include <functional>
#include "AgisStrategy.h" // Assuming this header contains the base class AgisStrategy

class StrategyRegistry {
public:
    using CreateInstanceFunc = std::function<std::unique_ptr<AgisStrategy>(
        PortfolioPtr const&
        )>;
    using RegistryMap = std::unordered_map<std::string, CreateInstanceFunc>;
    using PortfolioIdMap = std::unordered_map<std::string, std::string>;


    static RegistryMap& getRegistry() {
        static RegistryMap registry;
        return registry;
    }

    static PortfolioIdMap& getIDMap() {
        static PortfolioIdMap id_registry;
        return id_registry;
    }

    static bool registerStrategy(
        const std::string& className, 
        CreateInstanceFunc createFunc,
        const std::string& portfolio_id) {
        getRegistry().emplace(className, createFunc);
        getIDMap().emplace(className, portfolio_id);
        return true;
    }

private:
    // Define a custom hash function for std::pair<const std::string, CreateInstanceFunc>
    struct PairHash {
        template <class T1, class T2>
        std::size_t operator()(const std::pair<T1, T2>& p) const {
            return std::hash<T1>()(p.first) ^ std::hash<T2>()(p.second);
        }
    };
};