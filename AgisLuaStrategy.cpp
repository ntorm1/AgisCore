#include "AgisLuaStrategy.h"
#include "AgisLuaStrategy.h"
#include "AgisLuaStrategy.h"
#include "pch.h"
#include <fstream>
#include "AbstractStrategyTree.h"
#include "AgisLuaStrategy.h"

SOL_BASE_CLASSES(AgisLuaStrategy, AgisStrategy);
SOL_DERIVED_CLASSES(AgisLuaStrategy, AgisLuaStrategy);

#ifdef USE_LUAJIT
sol::state* AgisLuaStrategy::lua_ptr = nullptr;
#endif


//============================================================================
void init_lua_enums(sol::state& lua)
{
	lua.new_enum<ExchangeQueryType>("ExchangeQueryType",
		{
			{"Default", ExchangeQueryType::Default},
			{"NLargest", ExchangeQueryType::NLargest},
			{"NSmallest", ExchangeQueryType::NSmallest},
			{"NExtreme", ExchangeQueryType::NExtreme}
		}
	);
	lua.new_enum<ExchangeViewOpp>("ExchangeViewOpp",
		{
			{"UNIFORM", ExchangeViewOpp::UNIFORM},
			{"LINEAR_DECREASE", ExchangeViewOpp::LINEAR_DECREASE},
			{"LINEAR_INCREASE", ExchangeViewOpp::LINEAR_INCREASE},
			{"CONDITIONAL_SPLIT", ExchangeViewOpp::CONDITIONAL_SPLIT},
			{"UNIFORM_SPLIT", ExchangeViewOpp::UNIFORM_SPLIT},
			{"CONSTANT", ExchangeViewOpp::CONSTANT}
		}
	);
	lua.new_enum<AllocType>("AllocType",
		{
			{"UNITS", AllocType::UNITS},
			{"DOLLARS", AllocType::DOLLARS},
			{"PCT", AllocType::PCT},
		}
	);
}

//============================================================================
void init_lua_interface(sol::state* lua_ptr) {
	auto& lua = *lua_ptr;
	lua.open_libraries(sol::lib::base);
	init_lua_enums(lua);

	// register ast nodes
	lua.new_usertype<AbstractAssetLambdaRead>("AbstractAssetLambdaRead",
		sol::no_constructor
	);
	lua.new_usertype<AbstractAssetLambdaOpp>("AbstractAssetLambdaOpp",
		sol::no_constructor
	);
	//lua.new_usertype<AbstractAssetLambdaFilter>("AbstractAssetLambdaFilter",
	//	sol::no_constructor
	//);
	lua.new_usertype<AbstractExchangeNode>("AbstractExchangeNode",
		sol::no_constructor
	);
	lua.new_usertype<AbstractExchangeViewNode>("AbstractExchangeViewNode",
		sol::no_constructor
	);
	lua.new_usertype<AbstractSortNode>("AbstractSortNode",
				sol::no_constructor
	);
	lua.new_usertype<AbstractGenAllocationNode>("AbstractGenAllocationNode",
		sol::no_constructor
	);
	lua.new_usertype<AbstractStrategyAllocationNode>("AbstractStrategyAllocationNode",
		sol::no_constructor
	);

	//lua.set_function("create_asset_lambda_filter", create_asset_lambda_filter);
	lua.set_function("create_asset_lambda_read", create_asset_lambda_read);
	lua.set_function("create_asset_lambda_opp", create_asset_lambda_opp);
	lua.set_function("create_exchange_node", create_exchange_node);
	lua.set_function("create_exchange_view_node", create_exchange_view_node);
	lua.set_function("create_gen_alloc_node", create_gen_alloc_node);
	lua.set_function("create_sort_node", create_sort_node);
	lua.set_function("create_strategy_alloc_node", create_strategy_alloc_node);

	// Register the AgisResult template and its methods and constructors
	lua.new_usertype<AgisResult<double>>(
		"AgisResult",
		sol::no_constructor,
		"is_exception", &AgisResult<double>::is_exception,
		"unwrap", &AgisResult<double>::unwrap
	);


	// Bind the Asset class with no constructors.
	lua.new_usertype<Asset>("Asset",
		sol::no_constructor,
		"get_asset_feature",
		sol::overload(
			[](const Asset& asset, const std::string& col, int index) {
				return asset.get_asset_feature(col, index);
			},
			[](const Asset& asset, size_t col, int index) {
				return asset.get_asset_feature(col, index);
			}
		)
	);

	// Bind the Exchange class with no constructors.
	lua.new_usertype<Exchange>("Exchange",
		sol::no_constructor,
		"get_exchange_id" , &Exchange::get_exchange_id
	);

	// Bind the AgisStrategy class with no constructors.
	lua.new_usertype<AgisStrategy>("AgisStrategy",
		sol::no_constructor,
		"set_net_leverage_trace", &AgisStrategy::set_net_leverage_trace,
		"get_exchange", &AgisStrategy::get_exchange
	);


	// Bind the AgisLuaStrategy class with no constructors.
	lua.new_usertype<AgisLuaStrategy>("AgisLuaStrategy",
		sol::no_constructor,
		"set_allocation_node", &AgisLuaStrategy::set_allocation_node,
		"set_net_leverage_trace", &AgisLuaStrategy::set_net_leverage_trace,
		"set_beta_trace", &AgisLuaStrategy::set_beta_trace,

		"get_exchange", &AgisLuaStrategy::get_exchange,
		"exchange_subscribe", &AgisLuaStrategy::exchange_subscribe
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
AgisLuaStrategy::AgisLuaStrategy(
	PortfolioPtr const& portfolio_,
	std::string const& strategy_id,
	double allocation,
	fs::path const& script_path_,
	bool lazy_load
)
	: AgisStrategy(strategy_id, portfolio_, allocation)
{
	this->strategy_type = AgisStrategyType::LUAJIT;
	if (!fs::exists(script_path_)) {
		AGIS_THROW("invalid lua strategy script path: " + script_path_.string());
	}
	if(!lazy_load) this->load_script(script_path_);
	else this->script_path = script_path_;
}


//============================================================================
AGIS_API void AgisLuaStrategy::load_script(fs::path script_path_)
{
	// force garbage collection to remove any references to the old strategy logic
	lua_ptr->collect_garbage();

	// reset allocation node
	this->allocation_node = nullptr;

	// Open the file for reading
	std::ifstream fileStream(script_path_);

	// Check if the file was opened successfully
	if (!fileStream.is_open()) {
		AGIS_THROW("Failed to open file: " + script_path_.string());
	}

	// Read the contents of the file into a string
	std::string script;
	std::string line;
	while (std::getline(fileStream, line)) {
		script += line + "\n"; // Add newline character if needed
	}

	// Close the file
	fileStream.close();
	try {
		lua_ptr->script(script);
	}
	catch (sol::error& e) {
		AGIS_THROW("invalid lua strategy script: " + this->get_strategy_id() + "\n" + e.what());
	}
	this->script_path = script_path_;
	this->loaded = true;
}


//============================================================================
void AgisLuaStrategy::call_lua(const std::string& functionName) {
	// Get the Lua function
	sol::function lua_function = (*AgisLuaStrategy::lua_ptr)[this->get_strategy_id() + functionName];

	// Check if the Lua function is valid
	if (!lua_function.valid()) {
		AGIS_THROW("Invalid lua function call: " + this->get_strategy_id() + functionName);
	}

	// Call the Lua function, passing 'this' pointer
	try {
		lua_function(this);
	}
	catch (sol::error& e) {
		AGIS_THROW("Invalid lua function call: " + this->get_strategy_id() + functionName + "\n" + e.what());
	}
}


//============================================================================
void AgisLuaStrategy::next() {
	if (!this->exchange) {
		if (!this->__is_exchange_subscribed()){
			AGIS_THROW("lua strategy built without subscription: " + this->get_strategy_id());
		}
		this->exchange = this->get_exchange();
	}

	if (this->exchange->__get_exchange_index() < this->warmup) { return; }

	if (this->allocation_node) {
		auto res = this->allocation_node->execute();
		if (res.is_exception()) {
			throw res.get_exception();
		}
	}
	else {
		AGIS_TRY(this->call_lua("_next"););
	}
}


//============================================================================
void AgisLuaStrategy::reset() {
	AGIS_TRY(this->call_lua("_reset"););
}


//============================================================================
void AgisLuaStrategy::build() {
	if (!this->loaded && this->script_path.has_value()) this->load_script(this->script_path.value());
	AGIS_TRY(this->call_lua("_build");)
	if (this->allocation_node) {
		this->warmup = this->allocation_node->get_warmup();
	}
	
}

AGIS_API void AgisLuaStrategy::to_json(json& j) const
{
	if (script_path.has_value()) {
		j["lua_script_path"] = this->script_path.value().string();
	}
	AgisStrategy::to_json(j);
}


//============================================================================
AGIS_API std::string AgisLuaStrategy::get_script_template(std::string const& strategy_id)
{
	std::string script = R"(
function {STRATEGY_ID}_next(strategy)
	-- Custom Lua implementation of next()
end

function {STRATEGY_ID}_reset(strategy)   
 -- Custom Lua implementation of reset()
end

function {STRATEGY_ID}_build(strategy)
    -- Custom Lua implementation of build() 
end
)";
	str_replace_all(script, "{STRATEGY_ID}", strategy_id);
	return script;
}

