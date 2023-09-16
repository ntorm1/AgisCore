#include "pch.h"
#include "AgisLuaStrategy.h"

SOL_BASE_CLASSES(AgisLuaStrategy, AgisStrategy);
SOL_DERIVED_CLASSES(AgisLuaStrategy, AgisLuaStrategy);

#ifdef USE_LUAJIT
sol::state* AgisLuaStrategy::lua_ptr = nullptr;
#endif

//============================================================================
void init_lua_interface(sol::state& lua) {
	lua.open_libraries(sol::lib::base);

	// Bind the AgisStrategy class with no constructors.
	lua.new_usertype<AgisStrategy>("AgisStrategy",
		sol::no_constructor,
		"set_net_leverage_trace", &AgisStrategy::set_net_leverage_trace
	);

	// Bind the AgisLuaStrategy class with no constructors.
	lua.new_usertype<AgisLuaStrategy>("AgisLuaStrategy",
		sol::no_constructor,
		"set_net_leverage_trace", &AgisLuaStrategy::set_net_leverage_trace,
		sol::base_classes, sol::bases<AgisStrategy>()
	);
}

//============================================================================
AgisLuaStrategy::AgisLuaStrategy(
	PortfolioPtr const& portfolio_,
	std::string const& strategy_id, 
	double allocation,
	std::string const& script
) : AgisStrategy(strategy_id, portfolio_, allocation)
{
	this->strategy_type = AgisStrategyType::LUAJIT;
	try {
		lua_ptr->script(script);
	}
	catch (sol::error& e) {
		AGIS_THROW("invalid lua strategy script: " + this->get_strategy_id() + "\n" + e.what());
	}
}


//============================================================================
void AgisLuaStrategy::build()
{	
	// get the build method
	sol::function lua_function = (*AgisLuaStrategy::lua_ptr)[this->get_strategy_id() + "_build"];
	if (!lua_function.valid()) {
		AGIS_THROW("invalid lua strategy build: " + this->get_strategy_id());
	}

	// call the build method
	try {
		lua_function(this);
	}
	catch (sol::error& e) {
		AGIS_THROW("invalid lua strategy build: " + this->get_strategy_id() + "\n" + e.what());
	}
}
