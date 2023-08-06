#pragma once

#include <memory>
#include <iostream>
#include <unordered_map>
#include <functional>
#include "AgisStrategy.h" // Assuming this header contains the base class AgisStrategy

class StrategyRegistry {
public:
    using CreateInstanceFunc = std::function<std::unique_ptr<AgisStrategy>(
        std::string,
        PortfolioPtr const&,
        double)>;
    using RegistryMap = std::unordered_map<std::string, CreateInstanceFunc>;

    static RegistryMap& getRegistry() {
        static RegistryMap registry;
        return registry;
    }

    static bool registerStrategy(const std::string& className, CreateInstanceFunc createFunc) {
        getRegistry().emplace(className, createFunc);
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