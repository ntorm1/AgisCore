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
#include "AgisStrategy.h"

void init_lua_interface(sol::state& lua);


class AgisLuaStrategy : public AgisStrategy {
public:
	AGIS_API AgisLuaStrategy(
		PortfolioPtr const& portfolio_,
		std::string const& strategy_id,
		double allocation,
		std::string const& script
	);

	void next() override {};
	void reset() override {};
	void build() override;

	AGIS_API static void set_lua_ptr(sol::state * lua_ptr_) { lua_ptr = lua_ptr_; }

private:
	AGIS_API static sol::state* lua_ptr;
};

#endif