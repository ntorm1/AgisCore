#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#ifdef _DEBUG
#define SOL_ALL_SAFETIES_ON 1
#endif

#include "pch.h"
#ifdef USE_LUAJIT
#include "AbstractStrategyTree.h"

void init_lua_interface(sol::state* lua);


class AgisLuaStrategy : public AgisStrategy {
public:
	AGIS_API AgisLuaStrategy(
		PortfolioPtr const& portfolio_,
		std::string const& strategy_id,
		double allocation,
		std::string const& script
	);

	void next() override;
	void reset() override;
	void build() override;

	AGIS_API static void set_lua_ptr(sol::state * lua_ptr_) { lua_ptr = lua_ptr_; }
	AGIS_API void set_allocation_node(std::unique_ptr<AbstractStrategyAllocationNode>& allocation_node_) { this->allocation_node = std::move(allocation_node_); }
	AGIS_API void __override_warmup(size_t warmup_) { this->warmup = warmup_; }
protected:
	void call_lua(const std::string& functionName);

private:
	ExchangePtr exchange;
	std::unique_ptr<AbstractStrategyAllocationNode> allocation_node= nullptr;
	size_t warmup = 0;
	AGIS_API static sol::state* lua_ptr;
};

#endif