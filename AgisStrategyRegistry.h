#pragma once

#include <memory>
#include <iostream>
#include <unordered_map>
#include <functional>
#include "AgisStrategy.h" 

/// <summary>
/// A strategy registry to hold strategy classes inherited from AgisStrategy. This
/// is used when dynamiclly loading compiled strategies from Nexus. This is a way to create
/// instances of the strategies without knowing about them at compile time.
/// </summary>
class StrategyRegistry {
public:
    /// <summary>
    /// A function that returns a unique pointer to a new AgisStrategy given a reference
    /// to a portfolio.
    /// </summary>
    using CreateInstanceFunc = std::function<std::unique_ptr<AgisStrategy>(
        PortfolioPtr const&
        )>;

    /// <summary>
    /// A map between strategy id and the corresponding function that create the strategy.
    /// </summary>
    using RegistryMap = std::unordered_map<std::string, CreateInstanceFunc>;
    
    /// <summary>
    /// A map between a strategy id and the corresponding portfolio it belongs to.
    /// </summary>
    using PortfolioIdMap = std::unordered_map<std::string, std::string>;

    /// <summary>
    /// Return a static reference to the strategy registry
    /// </summary>
    /// <returns></returns>
    static RegistryMap& getRegistry() {
        static RegistryMap registry;
        return registry;
    }

    /// <summary>
    /// Return a static refernce to the strategy portfolio id regsitry 
    /// </summary>
    /// <returns></returns>
    static PortfolioIdMap& getIDMap() {
        static PortfolioIdMap id_registry;
        return id_registry;
    }

    /// <summary>
    /// Register a new strategy to the registery
    /// </summary>
    /// <param name="className">Name of the new strategy to register</param>
    /// <param name="createFunc">A function that create the new strategy</param>
    /// <param name="portfolio_id">Portfolio id of the strategy in question</param>
    /// <returns></returns>
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